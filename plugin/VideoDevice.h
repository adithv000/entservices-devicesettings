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

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <core/Portability.h>
#include <core/Proxy.h>
#include <core/Trace.h>

#include <com/com.h>
#include <core/core.h>
#include <plugins/plugins.h>

#include <interfaces/IDeviceSettingsVideoDevice.h>

#include "dsUtl.h"
#include "dsError.h"
#include "dsVideoDevice.h"

#include "hal/dVideoDevice.h"
#include "hal/dVideoDeviceImpl.h"
#include "DeviceSettingsTypes.h"

class VideoDevice {
    using IPlatform = hal::dVideoDevice::IPlatform;
    using DefaultImpl = dVideoDeviceImpl;

    std::shared_ptr<IPlatform> _platform;

public:
    class INotification {

        public:
            virtual ~INotification() = default;
            virtual void OnZoomSettingsChanged(const VideoDeviceZoom zoomSetting) = 0;
            virtual void OnDisplayFrameratePreChange(const string frameRate) = 0;
            virtual void OnDisplayFrameratePostChange(const string frameRate) = 0;
    };

public:

    void Platform_init();
    /** Deferred HAL init — called from DeviceSettingsImp::Configure() */
    void InitialiseHAL() { platform().InitialiseHAL(); }

    uint32_t GetVideoDeviceHandle(const int32_t index, int32_t &handle);
    uint32_t SetVideoDeviceDFC(const int32_t handle, const VideoDeviceZoom zoomSetting);
    uint32_t GetVideoDeviceDFC(const int32_t handle, VideoDeviceZoom &zoomSetting);
    uint32_t GetHDRCapabilities(const int32_t handle, int32_t &capabilities);
    uint32_t GetSupportedVideoCodingFormats(const int32_t handle, int32_t &supportedFormats);
    uint32_t GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo);
    uint32_t DisableHDR(const int32_t handle, const bool disable);
    uint32_t SetFRFMode(const int32_t handle, const int32_t frfmode);
    uint32_t GetFRFMode(const int32_t handle, int32_t &frfmode);
    uint32_t GetCurrentDisplayFrameRate(const int32_t handle, string &framerate);
    uint32_t SetDisplayFrameRate(const int32_t handle, const string framerate);

    // VideoDevice event handling methods - Called by DS HAL to forward events to parent
    void OnZoomSettingsChanged(const VideoDeviceZoom zoomSetting);
    void OnDisplayFrameratePreChange(const string frameRate);
    void OnDisplayFrameratePostChange(const string frameRate);

    /** Runtime factory: picks the AIDL HDMI Output HAL when available, otherwise the legacy DS HAL. */
    static VideoDevice Create(INotification& parent);

    template <typename IMPL = DefaultImpl, typename... Args>
    static VideoDevice Create(INotification& parent, Args&&... args)
    {
        ENTRY_LOG;
        static_assert(std::is_base_of<IPlatform, IMPL>::value, "Impl must derive from hal::dVideoDevice::IPlatform");
        auto impl = std::shared_ptr<IMPL>(new IMPL(std::forward<Args>(args)...));
        ASSERT(impl != nullptr);
        EXIT_LOG;
        return VideoDevice(parent, std::move(impl));
    }
    
    private:
    VideoDevice(INotification& parent, std::shared_ptr<IPlatform> platform);

    inline IPlatform& platform() const
    {
        return *_platform;
    }

    INotification& _parent;
};