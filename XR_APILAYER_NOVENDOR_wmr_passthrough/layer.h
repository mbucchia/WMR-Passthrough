// MIT License
//
// Copyright(c) 2022 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "framework/dispatch.gen.h"

namespace passthrough {

// Comment out the definition below to restore OPAQUE as the preferred blend mode.
#define XR_WMR_PASSTHROUGH_PREFER_ALPHA_BLEND

// Uncomment the definition below to tweak the passthrough camera color to "all business blue".
//#define XR_WMR_PASSTHROUGH_COLOR_ADJUSTMENT 0.f, 161.f / 255.f, 241.f / 255.f

// Uncomment the definition below to tweak the passthrough camera color to sepia.
//#define XR_WMR_PASSTHROUGH_COLOR_ADJUSTMENT 112.f / 255.f, 66.f / 255.f, 20.f / 255.f

// Uncomment the definition below to tweak the passthrough camera color to gray.
#define XR_WMR_PASSTHROUGH_COLOR_ADJUSTMENT 0.75f, 0.75f, 0.75f

    const std::string LayerName = "XR_APILAYER_NOVENDOR_wmr_passthrough";
    const uint32_t VersionMajor = 0;
    const uint32_t VersionMinor = 0;
    const uint32_t VersionPatch = 0;
    const std::string VersionString = "Unreleased";

    // Singleton accessor.
    OpenXrApi* GetInstance();

    // A function to reset (delete) the singleton.
    void ResetInstance();

    extern std::filesystem::path dllHome;
    extern std::filesystem::path localAppData;

} // namespace passthrough
