/* Copyright (c) 2023 Intel Corporation

Copyright 2022 The TensorFlow Authors. All Rights Reserved.

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

#ifndef ITEX_CORE_IR_IMPORTEXPORT_GRAPHDEF_EXPORT_H_
#define ITEX_CORE_IR_IMPORTEXPORT_GRAPHDEF_EXPORT_H_

#include "itex/core/utils/status.h"
#include "mlir/IR/BuiltinOps.h"  // from @llvm-project
#include "protos/graph.pb.h"

namespace mlir {
namespace tfg {
// Convert a TFG graph directly to GraphDef. Graph functions in the module are
// added to the GraphDef's function library.
itex::Status ConvertToGraphDef(ModuleOp module, itex::GraphDef* graph);
}  // namespace tfg
}  // namespace mlir

#endif  // ITEX_CORE_IR_IMPORTEXPORT_GRAPHDEF_EXPORT_H_
