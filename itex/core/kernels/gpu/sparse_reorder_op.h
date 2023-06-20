/* Copyright (c) 2023 Intel Corporation

Copyright 2021 The TensorFlow Authors. All Rights Reserved.

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

#ifndef ITEX_CORE_KERNELS_GPU_SPARSE_REORDER_OP_H_
#define ITEX_CORE_KERNELS_GPU_SPARSE_REORDER_OP_H_

#include "itex/core/utils/op_kernel.h"

namespace itex {

namespace functor {

template <typename Device, typename T>
struct SparseReorderFunctor {
  void operator()(OpKernelContext* context, const Tensor& input_ind,
                  const Tensor& input_val, const Tensor& input_shape_in);
};

}  // namespace functor

}  // namespace itex

#endif  // ITEX_CORE_KERNELS_GPU_SPARSE_REORDER_OP_H_
