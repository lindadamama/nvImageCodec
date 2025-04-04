/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "jpeg2k_prog_order.h"

namespace nvimgcodec {

void Jpeg2kProgOrder::exportToPython(py::module& m)
{
    // clang-format off
    py::enum_<nvimgcodecJpeg2kProgOrder_t>(m, "Jpeg2kProgOrder", "Enum representing progression orders in the JPEG2000 standard.")
        .value("LRCP", NVIMGCODEC_JPEG2K_PROG_ORDER_LRCP, 
            R"pbdoc(
            Layer-Resolution-Component-Position progression order.

            This progression order encodes data by layer first, then by resolution, 
            component, and position, optimizing for scalability in quality.
            )pbdoc")
        .value("RLCP", NVIMGCODEC_JPEG2K_PROG_ORDER_RLCP,
            R"pbdoc(
            Resolution-Layer-Component-Position progression order.

            This progression order encodes data by resolution first, followed by layer,
            component, and position, optimizing for scalability in resolution.
            )pbdoc")
        .value("RPCL", NVIMGCODEC_JPEG2K_PROG_ORDER_RPCL,
            R"pbdoc(
            Resolution-Position-Component-Layer progression order.

            This progression order encodes data by resolution first, then by position,
            component, and layer, which is useful for progressive transmission by 
            resolution.
            )pbdoc")
        .value("PCRL", NVIMGCODEC_JPEG2K_PROG_ORDER_PCRL,
            R"pbdoc(
            Position-Component-Resolution-Layer progression order.

            This progression order encodes data by position first, followed by component,
            resolution, and layer. It is beneficial for progressive transmission by spatial area.
            )pbdoc")
        .value("CPRL", NVIMGCODEC_JPEG2K_PROG_ORDER_CPRL,
            R"pbdoc(
            Component-Position-Resolution-Layer progression order.

            This progression order encodes data by component first, then by position,
            resolution, and layer, optimizing for scalability in component access.
            )pbdoc")
        .export_values();
    // clang-format on
}

} // namespace nvimgcodec
