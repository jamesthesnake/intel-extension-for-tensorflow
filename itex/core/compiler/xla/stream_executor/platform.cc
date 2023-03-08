/* Copyright (c) 2023 Intel Corporation

Copyright 2015 The TensorFlow Authors. All Rights Reserved.

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

#include "itex/core/compiler/xla/stream_executor/platform.h"

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "itex/core/compiler/xla/stream_executor/lib/error.h"
#include "itex/core/compiler/xla/stream_executor/platform/logging.h"
#include "itex/core/compiler/xla/stream_executor/platform/port.h"
#include "itex/core/compiler/xla/stream_executor/stream_executor_pimpl.h"

namespace stream_executor {

std::string PlatformKindString(PlatformKind kind) {
  switch (kind) {
    case PlatformKind::kSycl:
      return "SYCL";
    case PlatformKind::kCuda:
      return "CUDA";
    case PlatformKind::kROCm:
      return "ROCm";
    case PlatformKind::kOpenCL:
      return "OpenCL";
    case PlatformKind::kHost:
      return "Host";
    case PlatformKind::kMock:
      return "Mock";
    default:
      return absl::StrCat("InvalidPlatformKind(", static_cast<int>(kind), ")");
  }
}

PlatformKind PlatformKindFromString(std::string kind) {
  for (int i = 0; i < static_cast<int>(PlatformKind::kSize); ++i) {
    if (kind == PlatformKindString(static_cast<PlatformKind>(i))) {
      return static_cast<PlatformKind>(i);
    }
  }

  return PlatformKind::kInvalid;
}

bool PlatformIsRunnable(PlatformKind kind) {
  switch (kind) {
    case PlatformKind::kSycl:
    case PlatformKind::kCuda:
    case PlatformKind::kROCm:
    case PlatformKind::kOpenCL:
    case PlatformKind::kHost:
      return true;
    default:
      return false;
  }
}

bool PlatformIsRunnableOnDevice(PlatformKind kind) {
  switch (kind) {
    case PlatformKind::kSycl:
    case PlatformKind::kCuda:
    case PlatformKind::kROCm:
    case PlatformKind::kOpenCL:
      return true;
    default:
      return false;
  }
}

void CheckPlatformKindIsValid(PlatformKind kind) {
  ITEX_CHECK(static_cast<int>(PlatformKind::kCuda) <= static_cast<int>(kind) &&
             static_cast<int>(kind) <= static_cast<int>(PlatformKind::kMock))
      << "invalid GPU executor kind: " << PlatformKindString(kind);
}

StreamExecutorConfig::StreamExecutorConfig()
    : ordinal(-1), device_options(DeviceOptions::Default()) {}

StreamExecutorConfig::StreamExecutorConfig(int ordinal_in)
    : ordinal(ordinal_in), device_options(DeviceOptions::Default()) {}

Platform::~Platform() {}

bool Platform::Initialized() const { return true; }

port::Status Platform::Initialize(
    const std::map<std::string, std::string>& platform_options) {
  if (!platform_options.empty()) {
    return port::Status(itex::error::UNIMPLEMENTED,
                        "this platform does not support custom initialization");
  }
  return itex::Status::OK();
}

port::Status Platform::ForceExecutorShutdown() {
  return port::Status(itex::error::UNIMPLEMENTED,
                      "executor shutdown is not supported on this platform");
}

std::unique_ptr<Platform::PeerAccessMap> Platform::GetPeerAccessMap() {
  auto* map = new PeerAccessMap;

  int device_count = VisibleDeviceCount();
  for (int i = 0; i < device_count; ++i) {
    for (int j = 0; j < device_count; ++j) {
      StreamExecutor* from = ExecutorForDevice(i).value();
      StreamExecutor* to = ExecutorForDevice(j).value();
      (*map)[{i, j}] = from->CanEnablePeerAccessTo(to);
    }
  }

  return std::unique_ptr<Platform::PeerAccessMap>{map};
}

port::Status Platform::EnablePeerAccess() {
  auto peer_access_map = GetPeerAccessMap();
  for (const auto& access : *peer_access_map) {
    auto devices = access.first;
    if (access.second) {
      StreamExecutor* from = ExecutorForDevice(devices.first).value();
      StreamExecutor* to = ExecutorForDevice(devices.second).value();
      auto status = from->EnablePeerAccessTo(to);
      if (!status.ok()) {
        return status;
      }
    } else {
      ITEX_LOG(INFO) << "cannot enable peer access from device ordinal "
                     << devices.first << " to device ordinal "
                     << devices.second;
    }
  }
  return itex::Status::OK();
}

}  // namespace stream_executor
