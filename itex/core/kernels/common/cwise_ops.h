/* Copyright (c) 2021-2022 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef ITEX_CORE_KERNELS_COMMON_CWISE_OPS_H_
#define ITEX_CORE_KERNELS_COMMON_CWISE_OPS_H_

#include <cmath>
#include <functional>
#include <type_traits>

#include "itex/core/utils/bounds_check.h"
#include "itex/core/utils/tensor_types.h"
#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"

namespace Eigen {
namespace internal {
template <typename T>
struct google_floor_mod {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x,
                                                     const T& y) const {
    // EIGEN_STATIC_ASSERT(!NUMERIC_TYPE_MUST_BE_REAL);
    T trunc_mod = Eigen::internal::scalar_mod2_op<T>()(x, y);
    return trunc_mod != T(0) && (y < T(0) != trunc_mod < T(0)) ? trunc_mod + y
                                                               : trunc_mod;
  }
};

template <typename Scalar>
struct functor_traits<google_floor_mod<Scalar>> {
  enum {
    Cost = functor_traits<Eigen::internal::scalar_mod2_op<Scalar>>::Cost +
           NumTraits<Scalar>::AddCost,
    PacketAccess = false
  };
};

template <typename T, typename DivOrMod>
struct safe_div_or_mod_op {
  static_assert(std::is_integral<T>::value, "Integer type expected");

  bool* const error;

  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE safe_div_or_mod_op(bool* error)
      : error(error) {}

  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& a,
                                                     const T& b) const {
    const T safe_b = itex::internal::SubtleMustCopy(b);
    if (ITEX_PREDICT_TRUE(safe_b != 0)) {
      return DivOrMod()(a, safe_b);
    } else {
      *error = true;
      return 0;
    }
  }
};

template <typename Scalar, bool IsInteger = Eigen::NumTraits<Scalar>::IsInteger>
struct scalar_round_half_to_even_op {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Scalar
  operator()(const Scalar& x) const {
    EIGEN_STATIC_ASSERT((!NumTraits<Scalar>::IsComplex),
                        NUMERIC_TYPE_MUST_BE_REAL)
    const Scalar round_val = Eigen::numext::floor(x + Scalar(0.5));
    const Scalar fraction = round_val - x;
    if (ITEX_PREDICT_FALSE(fraction == Scalar(.5))) {
      return Scalar(2) * Eigen::numext::floor(Scalar(.5) * x + Scalar(0.5));
    } else {
      return round_val;
    }
  }

  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x) const {
    Packet half = pset1<Packet>(Scalar(0.5));
    Packet round_val = pfloor(padd(x, half));
    Packet fraction = psub(round_val, x);
    Packet half_mask = pcmp_eq(fraction, half);
    bool any_halves = predux_any(half_mask);

    if (ITEX_PREDICT_FALSE(any_halves)) {
      Packet two = pset1<Packet>(Scalar(2));
      Packet nearest_even = pmul(two, pfloor(pmadd(half, x, half)));
      return pselect(half_mask, nearest_even, round_val);
    } else {
      return round_val;
    }
  }
};

template <typename Scalar>
struct scalar_round_half_to_even_op<Scalar, false> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Scalar
  operator()(const Scalar& x) const {
    return Eigen::numext::rint(x);
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x) const {
    return print(x);
  }
};

template <typename Scalar>
struct functor_traits<scalar_round_half_to_even_op<Scalar>> {
  enum {
    Cost = Eigen::NumTraits<Scalar>::IsInteger ? 0
                                               : 4 * NumTraits<Scalar>::AddCost,
    PacketAccess =
        packet_traits<Scalar>::HasRound && packet_traits<Scalar>::HasRint &&
        packet_traits<Scalar>::HasFloor && packet_traits<Scalar>::HasAdd &&
        packet_traits<Scalar>::HasMul,
  };
};

template <typename Scalar, bool IsInteger = Eigen::NumTraits<Scalar>::IsInteger>
struct scalar_round_up_op {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Scalar
  operator()(const Scalar& x) const {
    EIGEN_STATIC_ASSERT((!NumTraits<Scalar>::IsComplex),
                        NUMERIC_TYPE_MUST_BE_REAL)

    return Eigen::numext::floor(x + Scalar(0.5));
  }

  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x) const {
    return pfloor(padd(x, pset1<Packet>(Scalar(0.5))));
  }
};

template <typename Scalar>
struct scalar_round_up_op<Scalar, true> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Scalar
  operator()(const Scalar& x) const {
    return x;
  }

  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x) const {
    return x;
  }
};

template <typename Scalar, bool IsInteger>
struct functor_traits<scalar_round_up_op<Scalar, IsInteger>> {
  enum {
    Cost = IsInteger ? 0 : 4 * NumTraits<Scalar>::AddCost,
    PacketAccess = IsInteger || packet_traits<Scalar>::HasFloor
  };
};

template <typename T, typename DivOrMod>
struct functor_traits<safe_div_or_mod_op<T, DivOrMod>> {
  enum {
    Cost = functor_traits<DivOrMod>::Cost + NumTraits<T>::AddCost,
    PacketAccess = false,
  };
};

template <typename T, typename Binary>
struct no_nan_op {
  EIGEN_EMPTY_STRUCT_CTOR(no_nan_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& a,
                                                     const T& b) const {
    if (b != T(0)) {
      return Binary()(a, b);
    } else {
      return T(0);
    }
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& a,
                                                        const Packet& b) const {
    const Packet mask = pcmp_eq(b, pzero(b));
    Packet quotient = Binary().packetOp(a, b);
    for (int i = 0; i < Eigen::internal::unpacket_traits<Packet>::size; i++) {
      if (mask[i] != 0) {
        quotient[i] = 0;
      }
    }
    return quotient;
  }
};

template <typename T>
struct div_no_nan_op : public no_nan_op<T, scalar_quotient_op<T>> {
  EIGEN_EMPTY_STRUCT_CTOR(div_no_nan_op)
};

template <typename T>
struct mul_no_nan_op : public no_nan_op<T, scalar_product_op<T>> {
  EIGEN_EMPTY_STRUCT_CTOR(mul_no_nan_op)
};

// similar to std::equal_to, but with the DEVICE_FUNC qualifier
template <class T>
struct equal_to : std::binary_function<T, T, bool> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE bool operator()(const T& x,
                                                        const T& y) const {
    return x == y;
  }
  template <typename OutPacket, typename InPacket>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE OutPacket
  packetOp(const InPacket& x, const InPacket& y) const {
    return boolean_pcmp_eq<OutPacket, InPacket>(x, y);
  }
};

template <class T>
struct functor_traits<equal_to<T>> {
  enum {
    Cost = Eigen::NumTraits<T>::AddCost,
    PacketAccess = packet_traits<T>::HasCmp
  };
};

// similar to std::not_equal_to, but with the DEVICE_FUNC qualifier
template <class T>
struct not_equal_to : std::binary_function<T, T, bool> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE bool operator()(const T& x,
                                                        const T& y) const {
    return x != y;
  }
  template <typename OutPacket, typename InPacket>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE OutPacket
  packetOp(const InPacket& x, const InPacket& y) const {
    return plogical_not(boolean_pcmp_eq<OutPacket, InPacket>(x, y));
  }
};

template <class T>
struct functor_traits<not_equal_to<T>> {
  enum {
    Cost = Eigen::NumTraits<T>::AddCost,
    PacketAccess = packet_traits<T>::HasCmp
  };
};

template <typename Scalar>
struct xdivy_op {
  EIGEN_EMPTY_STRUCT_CTOR(xdivy_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE const Scalar
  operator()(const Scalar& x, const Scalar& y) const {
    if (x == Scalar(0.)) {
      return Scalar(0.);
    }
    return x / y;
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE const Packet
  packetOp(const Packet& x, const Packet& y) const {
    Packet zeros = pzero(x);
    Packet mask = pcmp_eq(x, zeros);
    Packet x_div_y = pdiv(x, y);
    return pselect(mask, x, x_div_y);
  }
};

template <typename Scalar>
struct xlog1py_op {
  EIGEN_EMPTY_STRUCT_CTOR(xlog1py_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Scalar
  operator()(const Scalar& x, const Scalar& y) const {
    if (x == Scalar(0.)) {
      return Scalar(0.);
    }
    return x * numext::log1p(y);
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x,
                                                        const Packet& y) const {
    Packet zeros = pzero(x);
    Packet mask = pcmp_eq(x, zeros);
    scalar_log1p_op<Scalar> log1p_op;
    Packet log1p_y = log1p_op.packetOp(y);
    Packet x_log1p_y = pmul(x, log1p_y);
    return pselect(mask, x, x_log1p_y);
  }
};

template <typename Scalar>
struct xlogy_op {
  EIGEN_EMPTY_STRUCT_CTOR(xlogy_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Scalar
  operator()(const Scalar& x, const Scalar& y) const {
    if (x == Scalar(0.)) {
      return Scalar(0.);
    }
    return x * numext::log(y);
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x,
                                                        const Packet& y) const {
    Packet zeros = pzero(x);
    Packet mask = pcmp_eq(x, zeros);
    scalar_log_op<Scalar> log_op;
    Packet log_y = log_op.packetOp(y);
    Packet x_log_y = pmul(x, log_y);
    return pselect(mask, x, x_log_y);
  }
};

// Functor that enables squared difference functor.
template <typename Scalar>
struct scalar_squared_difference_op {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE const Scalar
  operator()(const Scalar& a, const Scalar& b) const {
    const Scalar v = scalar_difference_op<Scalar>()(a, b);
    return scalar_product_op<Scalar>()(v, scalar_conjugate_op<Scalar>()(v));
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE const Packet
  packetOp(const Packet& a, const Packet& b) const {
    const Packet v = scalar_difference_op<Scalar>().packetOp(a, b);
    return scalar_product_op<Scalar>().packetOp(
        v, scalar_conjugate_op<Scalar>().packetOp(v));
  }
};

template <typename Scalar>
struct functor_traits<scalar_squared_difference_op<Scalar>> {
  enum {
    Cost = functor_traits<scalar_difference_op<Scalar>>::Cost +
           functor_traits<scalar_conjugate_op<Scalar>>::Cost +
           functor_traits<scalar_product_op<Scalar>>::Cost,
    PacketAccess = functor_traits<scalar_difference_op<Scalar>>::PacketAccess &&
                   functor_traits<scalar_conjugate_op<Scalar>>::PacketAccess &&
                   functor_traits<scalar_product_op<Scalar>>::PacketAccess
  };
};

// TODO(b/32239616): This kernel should be moved into Eigen and vectorized.
template <typename T, typename Enable = void>
struct google_floor_div {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x,
                                                     const T& y) const {
    if ((x < T(0)) != (y < T(0))) {
#ifndef INTEL_CPU_ONLY
      T abs_x = sycl::abs(x);
      T abs_y = sycl::abs(y);
#else
      T abs_x = std::abs(x);
      T abs_y = std::abs(y);
#endif
      return -(abs_x + abs_y - 1) / abs_y;
    } else {
      return x / y;
    }
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x,
                                                        const Packet& y) const {
    Packet zeros = pzero(x);
    Packet x_mask = pcmp_lt(x, zeros);
    Packet y_mask = pcmp_lt(y, zeros);
    Packet x_div_y = pdiv(x, y);
    Packet abs_x = pabs(x);
    Packet abs_y = pabs(y);
    Packet ones = pones(x);
    Packet ratio_rounded = pdiv(pnegate(psub(padd(abs_x, abs_y), ones)), abs_y);
    return pselect(pxor(x_mask, y_mask), ratio_rounded, x_div_y);
  }
};

template <typename T>
struct google_floor_div<
    T, typename std::enable_if<std::is_unsigned<T>::value>::type> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x,
                                                     const T& y) const {
    return x / y;
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x,
                                                        const Packet& y) const {
    return pdiv(x, y);
  }
};

template <typename T, typename Enable = void>
struct google_floor_div_real {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x,
                                                     const T& y) const {
    return Eigen::numext::floor(x / y);
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x,
                                                        const Packet& y) const {
    return pfloor(pdiv(x, y));
  }
};

// similar to std::greater, but with the DEVICE_FUNC qualifier
template <class T>
struct greater : std::binary_function<T, T, bool> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE bool operator()(const T& x,
                                                        const T& y) const {
    return x > y;
  }
  template <typename OutPacket, typename InPacket>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE OutPacket
  packetOp(const InPacket& x, const InPacket& y) const {
    return plogical_not(boolean_pcmp_le<OutPacket, InPacket>(x, y));
  }
};

template <class T>
struct functor_traits<greater<T>> {
  enum {
    Cost = Eigen::NumTraits<T>::AddCost,
    PacketAccess = packet_traits<T>::HasCmp
  };
};

// similar to std::less, but with the DEVICE_FUNC qualifier
template <class T>
struct less : std::binary_function<T, T, bool> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE bool operator()(const T& x,
                                                        const T& y) const {
    return x < y;
  }
  template <typename OutPacket, typename InPacket>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE OutPacket
  packetOp(const InPacket& x, const InPacket& y) const {
    return boolean_pcmp_lt<OutPacket, InPacket>(x, y);
  }
};

template <class T>
struct functor_traits<less<T>> {
  enum {
    Cost = Eigen::NumTraits<T>::AddCost,
    PacketAccess = packet_traits<T>::HasCmp
  };
};

// similar to std::greater_equal, but with the DEVICE_FUNC qualifier
template <class T>
struct greater_equal : std::binary_function<T, T, bool> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE bool operator()(const T& x,
                                                        const T& y) const {
    return x >= y;
  }
  template <typename OutPacket, typename InPacket>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE OutPacket
  packetOp(const InPacket& x, const InPacket& y) const {
    return plogical_not(boolean_pcmp_lt<OutPacket, InPacket>(x, y));
  }
};

template <class T>
struct functor_traits<greater_equal<T>> {
  enum {
    Cost = Eigen::NumTraits<T>::AddCost,
    PacketAccess = packet_traits<T>::HasCmp
  };
};

// similar to std::less_equal, but with the DEVICE_FUNC qualifier
template <class T>
struct less_equal : std::binary_function<T, T, bool> {
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE bool operator()(const T& x,
                                                        const T& y) const {
    return x <= y;
  }
  template <typename OutPacket, typename InPacket>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE OutPacket
  packetOp(const InPacket& x, const InPacket& y) const {
    return boolean_pcmp_le<OutPacket, InPacket>(x, y);
  }
};

template <class T>
struct functor_traits<less_equal<T>> {
  enum {
    Cost = Eigen::NumTraits<T>::AddCost,
    PacketAccess = packet_traits<T>::HasCmp
  };
};

// scalar_left and scalar_right are template helpers to partially
// apply a binary function.
//
// Suppose Binary is a binary functor f(x, y), scalar_left<> is a
// unary functor g_x(y) = f(x, y), where x is provided via the
// constructor. Similarly, scalar_right<> is a unary functor g_y(x) =
// f(x, y).

template <typename Tout, typename Tin, typename Binary,
          bool is_scalar_in_host_memory = false>
struct scalar_left : private Binary {
  using result_type = Tout;
  using TinPacket = typename Eigen::internal::packet_traits<Tin>::type;

  const Tin* left;
  TinPacket left_packet;  // initialized iff is_scalar_in_host_memory == true

  EIGEN_DEVICE_FUNC inline scalar_left(const scalar_left& other) = default;

  template <typename... Args>
  EIGEN_DEVICE_FUNC inline explicit scalar_left(const Tin* c, Args... args)
      : Binary(args...), left(c) {
    if (is_scalar_in_host_memory) {
      left_packet = Eigen::internal::pset1<TinPacket>(*left);
    }
  }

  EIGEN_DEVICE_FUNC inline Tout operator()(const Tin& right) const {
    return Binary::operator()(*left, right);
  }

  template <typename Packet,
            typename std::enable_if<!is_scalar_in_host_memory ||
                                        !std::is_same<TinPacket, Packet>::value,
                                    int>::type = 0>
  EIGEN_DEVICE_FUNC inline Packet packetOp(const Packet& right_packet) const {
    const Packet left_packet = Eigen::internal::pset1<Packet>(*left);
    return Binary::packetOp(left_packet, right_packet);
  }

  template <typename Packet,
            typename std::enable_if<is_scalar_in_host_memory &&
                                        std::is_same<TinPacket, Packet>::value,
                                    int>::type = 0>
  EIGEN_DEVICE_FUNC inline Packet packetOp(const Packet& right_packet) const {
    return Binary::packetOp(left_packet, right_packet);
  }
};

template <typename Tout, typename Tin, typename Binary,
          bool is_scalar_in_host_memory>
struct functor_traits<
    scalar_left<Tout, Tin, Binary, is_scalar_in_host_memory>> {
  enum {
    Cost = functor_traits<Binary>::Cost,
    PacketAccess = functor_traits<Binary>::PacketAccess,
  };
};

template <typename Tout, typename Tin, typename Binary,
          bool is_scalar_in_host_memory = false>
struct scalar_right : private Binary {
  using result_type = Tout;
  using TinPacket = typename Eigen::internal::packet_traits<Tin>::type;

  const Tin* right;
  TinPacket right_packet;  // initialized iff is_scalar_in_host_memory == true

  EIGEN_DEVICE_FUNC inline scalar_right(const scalar_right& other) = default;

  template <typename... Args>
  EIGEN_DEVICE_FUNC inline explicit scalar_right(const Tin* c, Args... args)
      : Binary(args...), right(c) {
    if (is_scalar_in_host_memory) {
      right_packet = Eigen::internal::pset1<TinPacket>(*right);
    }
  }

  EIGEN_DEVICE_FUNC inline Tout operator()(const Tin& left) const {
    return Binary::operator()(left, *right);
  }

  template <typename Packet,
            typename std::enable_if<!is_scalar_in_host_memory ||
                                        !std::is_same<TinPacket, Packet>::value,
                                    int>::type = 0>
  EIGEN_DEVICE_FUNC inline Packet packetOp(const Packet& left_packet) const {
    const Packet right_packet = Eigen::internal::pset1<Packet>(*right);
    return Binary::packetOp(left_packet, right_packet);
  }

  template <typename Packet,
            typename std::enable_if<is_scalar_in_host_memory &&
                                        std::is_same<TinPacket, Packet>::value,
                                    int>::type = 0>
  EIGEN_DEVICE_FUNC inline Packet packetOp(const Packet& left_packet) const {
    return Binary::packetOp(left_packet, right_packet);
  }
};

template <typename Tout, typename Tin, typename Binary,
          bool is_scalar_in_host_memory>
struct functor_traits<
    scalar_right<Tout, Tin, Binary, is_scalar_in_host_memory>> {
  enum {
    Cost = functor_traits<Binary>::Cost,
    PacketAccess = functor_traits<Binary>::PacketAccess,
  };
};

template <typename Scalar>
struct bitwise_xor_op {
  EIGEN_EMPTY_STRUCT_CTOR(bitwise_xor_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Scalar
  operator()(const Scalar& x, const Scalar& y) const {
    return x ^ y;
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& a,
                                                        const Packet& b) const {
    return Eigen::internal::pxor(a, b);
  }
};

template <typename Scalar>
struct functor_traits<bitwise_xor_op<Scalar>> {
  enum { Cost = Eigen::NumTraits<Scalar>::AddCost, PacketAccess = true };
};

template <typename T>
struct scalar_erfinv_op {
  EIGEN_EMPTY_STRUCT_CTOR(scalar_erfinv_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x) const {
    constexpr T half = T(0.5);
    T y = numext::ndtri(half * x + half);
    constexpr T half_sqrt = T(M_SQRT1_2);
    return y * half_sqrt;
  }
  template <typename Packet>
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Packet packetOp(const Packet& x) const {
    Packet half = pset1<Packet>(T(0.5));
    Packet y = pndtri<Packet>(pmadd(half, x, half));
    Packet half_sqrt = pset1<Packet>(T(M_SQRT1_2));
    return pmul(y, half_sqrt);
  }
};

template <typename T>
struct functor_traits<scalar_erfinv_op<T>> {
  enum {
    Cost = functor_traits<scalar_ndtri_op<T>>::Cost + NumTraits<T>::AddCost,
    PacketAccess = packet_traits<T>::HasNdtri,
  };
};

}  // namespace internal
}  // namespace Eigen

namespace itex {
namespace functor {

////////////////////////////////////////////////////////////////////////////////
// Helpers
////////////////////////////////////////////////////////////////////////////////

// Base template for functors whose input scalar type is T and
// output scalar type is R.
template <typename T, typename F, typename R = T>
struct base {
  // func defines operator() and its vectorized version packetOp().
  typedef F func;

  // If true, the functor's corresponding binary op will instantiate
  // specialized kernels to perform an optimized broadcast
  // operation. Each functor for which this is enabled increases the
  // code size, so by default this is disabled for binary functors and
  // is enabled on a per-op basis as needed.
  static const bool use_bcast_optimization = false;

  // operator() has the signature:
  //  out_type operator()(in_type in0, in_type in1 ...)
  typedef R out_type;
  typedef T in_type;

  // TensorFlow provides tensor-ized version of "func". Roughly
  // speaking, the tensorflow operation has the signature:
  //   tout_type op(tin_type in0)
  //   tout_type op(tin_type in0, tin_type in1)
  //   tout_type op(tin_type in0, in_type scalar)
  typedef typename TTypes<out_type>::Flat tout_type;
  typedef typename TTypes<in_type>::ConstFlat tin_type;
  typedef typename TTypes<in_type>::ConstScalar tscalar_type;

  // Whether the functor can error out.  Currently applies only to integer
  // div and mod.
  static const bool has_errors = false;
};

// For now, we only apply certain speed optimization for
// float/double's broadcast binary op.
template <typename T>
struct use_bcast_optimization {
  static constexpr bool value = false;
};

template <>
struct use_bcast_optimization<float> {
  static constexpr bool value = true;
};

template <>
struct use_bcast_optimization<double> {
  static constexpr bool value = true;
};

template <typename T>
struct exp : base<T, Eigen::internal::scalar_exp_op<T>> {};

template <typename T>
struct expm1 : base<T, Eigen::internal::scalar_expm1_op<T>> {};

template <typename T>
struct lgamma : base<T, Eigen::internal::scalar_lgamma_op<T>> {};

template <typename T>
struct log : base<T, Eigen::internal::scalar_log_op<T>> {};

template <typename T>
struct log1p : base<T, Eigen::internal::scalar_log1p_op<T>> {};

template <typename T>
struct sign : base<T, Eigen::internal::scalar_sign_op<T>> {};

template <typename T>
struct rint : base<T, Eigen::internal::scalar_rint_op<T>> {};

template <typename T>
struct add : base<T, Eigen::internal::scalar_sum_op<T>> {
  static constexpr bool use_bcast_optimization = true;
};

template <typename T>
struct abs : base<T, Eigen::internal::scalar_abs_op<T>,
                  typename Eigen::internal::scalar_abs_op<T>::result_type> {};

template <typename T>
struct sub : base<T, Eigen::internal::scalar_difference_op<T>> {
  static constexpr bool use_bcast_optimization = true;
};

template <typename T>
struct neg : base<T, Eigen::internal::scalar_opposite_op<T>> {};

template <typename T>
struct sqrt : base<T, Eigen::internal::scalar_sqrt_op<T>> {};

template <typename T>
struct rsqrt : base<T, Eigen::internal::scalar_rsqrt_op<T>> {};

// Flip all bits. Named invert to be consistent with numpy.
template <typename T>
struct invert_op {
  EIGEN_EMPTY_STRUCT_CTOR(invert_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& a) const {
    return ~a;
  }
};

template <typename T>
struct invert : base<T, invert_op<T>> {};

template <typename T>
struct right_shift_op {
  EIGEN_EMPTY_STRUCT_CTOR(right_shift_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x,
                                                     const T& y) const {
    // Avoids UB: don't shift by larger than the bitwidth of T.
    T y_clamped = y;
    if (y_clamped < 0) {
      y_clamped = 0;
    } else if (y_clamped > sizeof(T) * CHAR_BIT - 1) {
      y_clamped = sizeof(T) * CHAR_BIT - 1;
    }
    // Technically right shifts of signed integers are not necessarily
    // arithmetic shifts according to the C++ standard. However in practice most
    // implementations are arithmetic shifts. If this proves to be a problem in
    // practice, we may need to use an alternative implementation.
    return x >> y_clamped;
  }
};

template <typename T>
struct right_shift : base<T, right_shift_op<T>> {};

template <typename T>
struct isinf : base<T, Eigen::internal::scalar_isinf_op<T>, bool> {};

template <typename T>
struct isnan : base<T, Eigen::internal::scalar_isnan_op<T>, bool> {};

template <typename T>
struct floor : base<T, Eigen::internal::scalar_floor_op<T>> {};

template <typename T>
struct round : base<T, Eigen::internal::scalar_round_half_to_even_op<T>> {};

template <typename T>
struct ceil : base<T, Eigen::internal::scalar_ceil_op<T>> {};

template <typename T>
struct isfinite : base<T, Eigen::internal::scalar_isfinite_op<T>, bool> {};

template <typename T>
struct square : base<T, Eigen::internal::scalar_square_op<T>> {};

template <typename T>
struct mul : base<T, Eigen::internal::scalar_product_op<T>> {
  static constexpr bool use_bcast_optimization = true;
};

template <typename T>
struct mul_no_nan : base<T, Eigen::internal::mul_no_nan_op<T>> {};

template <typename T>
struct erf : base<T, Eigen::internal::scalar_erf_op<T>> {};

template <typename T>
struct erfc : base<T, Eigen::internal::scalar_erfc_op<T>> {};

template <typename T>
struct ndtri : base<T, Eigen::internal::scalar_ndtri_op<T>> {};

template <typename T>
struct erfinv : base<T, Eigen::internal::scalar_erfinv_op<T>> {};

template <typename T>
struct sigmoid : base<T, Eigen::internal::scalar_logistic_op<T>> {};

template <typename T>
struct sin : base<T, Eigen::internal::scalar_sin_op<T>> {};

template <typename T>
struct cos : base<T, Eigen::internal::scalar_cos_op<T>> {};

template <typename T>
struct tan : base<T, Eigen::internal::scalar_tan_op<T>> {};

template <typename T>
struct asin : base<T, Eigen::internal::scalar_asin_op<T>> {};

template <typename T>
struct asinh : base<T, Eigen::internal::scalar_asinh_op<T>> {};

template <typename T>
struct sinh : base<T, Eigen::internal::scalar_sinh_op<T>> {};

template <typename T>
struct tanh : base<T, Eigen::internal::scalar_tanh_op<T>> {};

template <typename T>
struct acos : base<T, Eigen::internal::scalar_acos_op<T>> {};

template <typename T>
struct atan : base<T, Eigen::internal::scalar_atan_op<T>> {};

template <typename T>
struct atanh : base<T, Eigen::internal::scalar_atanh_op<T>> {};

template <typename T>
struct acosh : base<T, Eigen::internal::scalar_acosh_op<T>> {};

template <typename T>
struct cosh : base<T, Eigen::internal::scalar_cosh_op<T>> {};

template <typename Scalar>
struct scalar_atan2_op {
  EIGEN_EMPTY_STRUCT_CTOR(scalar_atan2_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE Scalar
  operator()(const Scalar& y, const Scalar& x) const {
    return std::atan2(y, x);
  }
};

template <typename T>
struct atan2 : base<T, scalar_atan2_op<T>> {};

template <typename T>
struct squared_difference
    : base<T, Eigen::internal::scalar_squared_difference_op<T>> {};

template <typename T>
struct div : base<T, Eigen::internal::scalar_quotient_op<T>> {};

template <typename T>
struct div_no_nan : base<T, Eigen::internal::div_no_nan_op<T>> {};

template <typename T>
struct floor_div : base<T, Eigen::internal::google_floor_div<T>> {};

template <typename T>
struct floor_div_real : base<T, Eigen::internal::google_floor_div_real<T>> {};

template <typename T>
struct fmod : base<T, Eigen::internal::scalar_fmod_op<T>> {};

template <typename T>
struct safe_mod : base<T, Eigen::internal::safe_div_or_mod_op<
                              T, Eigen::internal::scalar_mod2_op<T>>> {
  static constexpr bool has_errors = true;
};

template <typename T>
struct safe_floor_mod : base<T, Eigen::internal::safe_div_or_mod_op<
                                    T, Eigen::internal::google_floor_mod<T>>> {
  static constexpr bool has_errors = true;
};

template <typename T>
struct xdivy : base<T, Eigen::internal::xdivy_op<T>> {};

template <typename T>
struct xlog1py : base<T, Eigen::internal::xlog1py_op<T>> {};

template <typename T>
struct xlogy : base<T, Eigen::internal::xlogy_op<T>> {};

template <typename T>
struct zeta : base<T, Eigen::internal::scalar_zeta_op<T>> {};

template <typename T>
struct polygamma : base<T, Eigen::internal::scalar_polygamma_op<T>> {};

template <typename T>
struct digamma : base<T, Eigen::internal::scalar_digamma_op<T>> {};

template <typename T>
struct less : base<T, Eigen::internal::less<T>, bool> {};

template <typename T>
struct less_equal : base<T, Eigen::internal::less_equal<T>, bool> {};

template <typename T>
struct pow : base<T, Eigen::internal::scalar_pow_op<T, T>> {};

template <typename T>
struct greater : base<T, Eigen::internal::greater<T>, bool> {};

template <typename T>
struct greater_equal : base<T, Eigen::internal::greater_equal<T>, bool> {};

struct logical_and : base<bool, Eigen::internal::scalar_boolean_and_op> {};

struct logical_not : base<bool, Eigen::internal::scalar_boolean_not_op<bool>> {
};

struct logical_or : base<bool, Eigen::internal::scalar_boolean_or_op> {};

template <typename T>
struct equal_to : base<T, Eigen::internal::equal_to<T>, bool> {};

template <typename T>
struct not_equal_to : base<T, Eigen::internal::not_equal_to<T>, bool> {};

template <typename T>
struct less_with_cast : base<T, Eigen::internal::scalar_cmp_with_cast_op<
                                    T, T, Eigen::internal::cmp_LT>> {};

template <typename T>
struct less_equal_with_cast : base<T, Eigen::internal::scalar_cmp_with_cast_op<
                                          T, T, Eigen::internal::cmp_LE>> {};

template <typename T>
struct greater_with_cast : base<T, Eigen::internal::scalar_cmp_with_cast_op<
                                       T, T, Eigen::internal::cmp_GT>> {};

template <typename T>
struct greater_equal_with_cast
    : base<T, Eigen::internal::scalar_cmp_with_cast_op<
                  T, T, Eigen::internal::cmp_GE>> {};

template <typename T>
struct equal_to_with_cast : base<T, Eigen::internal::scalar_cmp_with_cast_op<
                                        T, T, Eigen::internal::cmp_EQ>> {};

template <typename T>
struct not_equal_to_with_cast
    : base<T, Eigen::internal::scalar_cmp_with_cast_op<
                  T, T, Eigen::internal::cmp_NEQ>> {};

template <typename T>
struct bitwise_and_op {
  EIGEN_EMPTY_STRUCT_CTOR(bitwise_and_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x,
                                                     const T& y) const {
    return x & y;
  }
};

template <typename T>
struct bitwise_or_op {
  EIGEN_EMPTY_STRUCT_CTOR(bitwise_or_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x,
                                                     const T& y) const {
    return x | y;
  }
};

template <typename T>
struct bitwise_and : base<T, bitwise_and_op<T>> {};

template <typename T>
struct bitwise_or : base<T, bitwise_or_op<T>> {};

template <typename T>
struct bitwise_xor : base<T, Eigen::internal::bitwise_xor_op<T>> {};

template <typename T>
struct left_shift_op {
  EIGEN_EMPTY_STRUCT_CTOR(left_shift_op)
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE T operator()(const T& x,
                                                     const T& y) const {
    // Avoids UB: don't shift by larger than the bitwidth of T, and
    // performs left shifts as unsigned shifts.
    T y_clamped = y;
    if (y_clamped < 0) {
      y_clamped = 0;
    } else if (y_clamped > sizeof(T) * CHAR_BIT - 1) {
      y_clamped = sizeof(T) * CHAR_BIT - 1;
    }
    using U = typename std::make_unsigned<T>::type;
    return static_cast<T>(static_cast<U>(x) << static_cast<U>(y_clamped));
  }
};

template <typename T>
struct left_shift : base<T, left_shift_op<T>> {};

template <typename T>
struct maximum : base<T, Eigen::internal::scalar_max_op<T>> {};

template <typename T>
struct minimum : base<T, Eigen::internal::scalar_min_op<T>> {};

template <typename T>
struct igamma : base<T, Eigen::internal::scalar_igamma_op<T>> {};

template <typename T>
struct random_gamma_grad
    : base<T, Eigen::internal::scalar_gamma_sample_der_alpha_op<T>> {};

template <typename T>
struct igammac : base<T, Eigen::internal::scalar_igammac_op<T>> {};

template <typename T>
struct inverse : base<T, Eigen::internal::scalar_inverse_op<T>> {};

template <typename T>
struct make_complex_func {
  typedef std::complex<T> result_type;
  EIGEN_DEVICE_FUNC EIGEN_STRONG_INLINE result_type operator()(T real,
                                                               T imag) const {
    return std::complex<T>(real, imag);
  }
};

template <typename T>
struct make_complex : base<T, make_complex_func<T>, std::complex<T>> {};

template <typename T>
struct get_angle
    : base<T, Eigen::internal::scalar_arg_op<T>, typename T::value_type> {};

template <typename T>
struct conj : base<T, Eigen::internal::scalar_conjugate_op<T>> {};

template <typename T>
struct get_real
    : base<T, Eigen::internal::scalar_real_op<T>, typename T::value_type> {};

template <typename T>
struct get_imag
    : base<T, Eigen::internal::scalar_imag_op<T>, typename T::value_type> {};

template <typename Device, typename Functor>
struct UnaryFunctor {
  // Computes on device "d": out[i] = Functor(in[i])
  void operator()(const Device& d, typename Functor::tout_type out,
                  typename Functor::tin_type in);
};

template <typename Device, typename Functor, int NDIMS,
          bool has_errors = Functor::has_errors>
struct BinaryFunctor {
  // Computes on device "d": out[i] = Functor(in0[i], in1[i])
  void operator()(const Device& d, typename Functor::tout_type out,
                  typename Functor::tin_type in0,
                  typename Functor::tin_type in1, bool* error);

  // Computes on device "d": out[i] = Functor(scalar[0], in[i])
  void Left(const Device& d, typename Functor::tout_type out,
            typename Functor::tscalar_type scalar,
            typename Functor::tin_type in, bool* error);

  // Computes on device "d": out[i] = Functor(in[i], scalar[0])
  void Right(const Device& d, typename Functor::tout_type out,
             typename Functor::tin_type in,
             typename Functor::tscalar_type scalar, bool* error);

  // Computes on device "d":
  //   out = Functor(in0.broadcast(bcast0), in1.broadcast(bcast1))
  void BCast(const Device& d,
             typename TTypes<typename Functor::out_type, NDIMS>::Tensor out,
             typename TTypes<typename Functor::in_type, NDIMS>::ConstTensor in0,
             typename Eigen::array<Eigen::DenseIndex, NDIMS> bcast0,
             typename TTypes<typename Functor::in_type, NDIMS>::ConstTensor in1,
             typename Eigen::array<Eigen::DenseIndex, NDIMS> bcast1,
             bool* error);
};

template <typename Device, typename T>
struct ApproximateEqual {
  void operator()(const Device& d, typename TTypes<T>::ConstFlat x,
                  typename TTypes<T>::ConstFlat y, T tolerance,
                  typename TTypes<bool>::Flat z);
};

template <int NDIMS>
bool AllOne(const typename Eigen::array<Eigen::DenseIndex, NDIMS>& a) {
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != 1) return false;
  }
  return true;
}

}  // namespace functor
}  // namespace itex

#endif  // ITEX_CORE_KERNELS_COMMON_CWISE_OPS_H_
