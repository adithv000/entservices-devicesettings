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

#include <interfaces/IDeviceSettingsVideoPort.h>

#include "dsUtl.h"
#include "dsError.h"
#include "dsDisplay.h"
#include "dsVideoPort.h"

#include "hal/dVideoPort.h"
#include "hal/dVideoPortImpl.h"
#include "DeviceSettingsTypes.h"

class VideoPort {
    using IPlatform = hal::dVideoPort::IPlatform;
    using DefaultImpl = dVideoPortImpl;

    std::shared_ptr<IPlatform> _platform;

public:
    class INotification {

        public:
            virtual ~INotification() = default;
            virtual void OnResolutionPreChange(const ResolutionChange resolution) = 0;
            virtual void OnResolutionPostChange(const ResolutionChange resolution) = 0;
            virtual void OnHDCPStatusChange(const VideoPortHdcpStatus hdcpStatus) = 0;
            virtual void OnVideoFormatUpdate(const HDRStandard videoFormatHDR) = 0;
    };

public:

    void Platform_init();
    /** Deferred HAL init — called from DeviceSettingsImp::Configure() */
    void InitialiseHAL() { platform().InitialiseHAL(); }

    uint32_t GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t &handle);
    uint32_t IsVideoPortEnabled(const int32_t handle, bool &enabled);
    uint32_t EnableVideoPort(const int32_t handle, const bool enabled);
    uint32_t IsVideoPortDisplayConnected(const int32_t handle, bool &connected);
    uint32_t IsVideoPortActive(const int32_t handle, bool &active);
    uint32_t GetVideoPortResolution(const int32_t handle, VideoPortResolution &resolution);
    uint32_t SetVideoPortResolution(const int32_t handle, const VideoPortResolution& resolution, const bool persist, const bool forceCompatibility);
    uint32_t getIgnoreEDIDStatus(const int32_t handle, bool &ignoreEDID);
    uint32_t GetColorDepth(const int32_t handle, uint32_t &colorDepth);
    uint32_t SetVideoPortColorDepth(const int32_t handle, const uint32_t colorDepth);
    uint32_t GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange &quantizationRange);
    uint32_t SetVideoPortQuantizationRange(const int32_t handle, const VideoPortQuantizationRange quantizationRange);
    uint32_t GetColorSpace(const int32_t handle, VideoPortColorSpace &colorSpace);
    uint32_t SetColorSpace(const int32_t handle, const VideoPortColorSpace colorSpace);
    uint32_t GetVideoPortFrameRate(const int32_t handle, uint32_t &frameRate);
    uint32_t SetVideoPortFrameRate(const int32_t handle, const uint32_t frameRate);
    uint32_t GetVideoPortHDCPStatus(const int32_t handle, VideoPortHdcpStatus &hdcpStatus);
    uint32_t GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion);
    uint32_t GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion);
    uint32_t GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion);
    uint32_t EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t* hdcpKey, const uint16_t hdcpKeySize);
    uint32_t IsHDCPEnabledOnVideoPort(const int32_t handle, bool &hdcpEnabled);
    uint32_t GetTVHDRCapabilities(const int32_t handle, int32_t &capabilities);
    uint32_t GetTVSupportedResolutions(const int32_t handle, int32_t &resolutions);
    uint32_t SetForceDisable4K(const int32_t handle, const bool disable);
    uint32_t GetForceDisable4K(const int32_t handle, bool &disabled);
    uint32_t IsVideoPortOutputHDR(const int32_t handle, bool &isHDR);
    uint32_t ResetVideoPortOutputToSDR();
    uint32_t GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion);
    uint32_t SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion);
    uint32_t GetVideoEOTF(const int32_t handle, HDRStandard &hdrStandard);
    uint32_t GetMatrixCoefficients(const int32_t handle, DisplayMatrixCoefficients &matrixCoefficients);
    uint32_t IsVideoPortDisplaySurround(const int32_t handle, bool &surround);
    uint32_t GetVideoPortDisplaySurroundMode(const int32_t handle, VideoPortSurroundMode &surroundMode);
    uint32_t GetCurrentOutputSettings(const int32_t handle, DSOutputSettings &outputSettings);
    uint32_t SetBackgroundColor(const int32_t handle, const VideoBackgroundColor backgroundColor);
    uint32_t SetForceHDRMode(const int32_t handle, const HDRStandard hdrMode);
    uint32_t GetColorDepthCapabilities(const int32_t handle, uint32_t &colorDepthCapabilities);
    uint32_t GetPreferredColorDepth(const int32_t handle, DisplayColorDepth &colorDepth, const bool persist);
    uint32_t SetPreferredColorDepth(const int32_t handle, const DisplayColorDepth colorDepth, const bool persist);

    // VideoPort event handling methods - Called by DS HAL to forward events to parent
    void OnResolutionPreChange(const ResolutionChange resolution);
    void OnResolutionPostChange(const ResolutionChange resolution);
    void OnHDCPStatusChange(const VideoPortHdcpStatus hdcpStatus);
    void OnVideoFormatUpdate(const HDRStandard videoFormatHDR);

    /** Runtime factory: picks the AIDL HDMI Output HAL when available, otherwise the legacy DS HAL. */
    static VideoPort Create(INotification& parent);

    template <typename IMPL = DefaultImpl, typename... Args>
    static VideoPort Create(INotification& parent, Args&&... args)
    {
        ENTRY_LOG;
        static_assert(std::is_base_of<IPlatform, IMPL>::value, "Impl must derive from hal::dVideoPort::IPlatform");
        auto impl = std::shared_ptr<IMPL>(new IMPL(std::forward<Args>(args)...));
        ASSERT(impl != nullptr);
        EXIT_LOG;
        return VideoPort(parent, std::move(impl));
    }
    
    private:
    VideoPort(INotification& parent, std::shared_ptr<IPlatform> platform);

    inline IPlatform& platform() const
    {
        return *_platform;
    }

    INotification& _parent;
};