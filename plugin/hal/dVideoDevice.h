/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
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
#pragma once

#include "dsVideoDevice.h"
#include "dsError.h"
#include "dsUtl.h"
#include "dsTypes.h"

#include "rfcapi.h"

#include <core/Portability.h>
#include <WPEFramework/interfaces/IDeviceSettingsVideoDevice.h>
#include "DeviceSettingsTypes.h"

#include <cstdint>

namespace hal {
namespace dVideoDevice {

    class IPlatform {

    public:
        virtual ~IPlatform() {} 
        virtual void InitialiseHAL() = 0;
        virtual void DeInitialiseHAL() = 0;
        virtual void setAllCallbacks(const CallbackBundle& bundle) = 0;
        virtual void getPersistenceValue() = 0;

        // VideoDevice Platform interface methods - all pure virtual
        virtual uint32_t GetVideoDeviceHandle(const int32_t index, int32_t& handle) = 0;
        virtual uint32_t SetVideoDeviceDFC(const int32_t handle, const VideoDeviceZoom zoomSetting) = 0;
        virtual uint32_t GetVideoDeviceDFC(const int32_t handle, VideoDeviceZoom& zoomSetting) = 0;
        virtual uint32_t GetHDRCapabilities(const int32_t handle, int32_t& capabilities) = 0;
        virtual uint32_t GetSupportedVideoCodingFormats(const int32_t handle, int32_t& supportedFormats) = 0;
        virtual uint32_t GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo) = 0;
        virtual uint32_t DisableHDR(const int32_t handle, const bool disable) = 0;
        virtual uint32_t SetFRFMode(const int32_t handle, const int32_t frfmode) = 0;
        virtual uint32_t GetFRFMode(const int32_t handle, int32_t& frfmode) = 0;
        virtual uint32_t GetCurrentDisplayFrameRate(const int32_t handle, string& framerate) = 0;
        virtual uint32_t SetDisplayFrameRate(const int32_t handle, const string framerate) = 0;
    };

    // CallbackBundle structure to hold all VideoDevice event callbacks
    struct CallbackBundle {
        std::function<void(const VideoDeviceZoom)> OnZoomSettingsChanged;
        std::function<void(const string)> OnDisplayFrameratePreChange;
        std::function<void(const string)> OnDisplayFrameratePostChange;
    };
} // namespace dVideoDevice
} // namespace hal