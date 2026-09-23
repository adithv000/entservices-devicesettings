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

#include <functional>
#include <unistd.h>
#include <memory>

#include <core/IAction.h>
#include <core/Time.h>
#include <core/WorkerPool.h>

#include "secure_wrapper.h"
#include "VideoPort.h"
#include "hal/dVideoPortImpl.h"
#ifdef ENABLE_HDMIOUTPUT_AIDL
#include "hal/dVideoPortAIDLImpl.h"
#endif

VideoPort VideoPort::Create(INotification& parent)
{
    ENTRY_LOG;
#ifdef ENABLE_HDMIOUTPUT_AIDL
    if (dVideoPortAIDLImpl::IsAvailable()) {
        DSLOG_INFO("Using HDMI Output AIDL HAL");
        return VideoPort(parent, std::make_shared<dVideoPortAIDLImpl>());
    }
#endif
    DSLOG_INFO("Using legacy VideoPort DS HAL");
    return VideoPort(parent, std::make_shared<DefaultImpl>());
}

VideoPort::VideoPort(INotification& parent, std::shared_ptr<IPlatform> platform)
    : _platform(std::move(platform))
    , _parent(parent)
{
    Platform_init();
}

void VideoPort::Platform_init()
{
    DSLOG_INFO("VideoPort Init - Setting up event callbacks");
    
    // Set up callback bundle for VideoPort events - using global CallbackBundle pattern
    CallbackBundle bundle;
    
    bundle.OnResolutionPreChange = [this](const ResolutionChange resolution) {
        this->OnResolutionPreChange(resolution);
    };
    bundle.OnResolutionPostChange = [this](const ResolutionChange resolution) {
        this->OnResolutionPostChange(resolution);
    };
    bundle.OnHDCPStatusChange = [this](const VideoPortHdcpStatus hdcpStatus) {
        this->OnHDCPStatusChange(hdcpStatus);
    };
    bundle.OnVideoFormatUpdate = [this](const HDRStandard videoFormatHDR) {
        this->OnVideoFormatUpdate(videoFormatHDR);
    };
    
    if (_platform) {
        // Use interface method directly - no casting needed
        this->platform().setAllCallbacks(bundle);
        this->platform().getPersistenceValue();
    }
    
}

uint32_t VideoPort::GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t &handle) {
    DSLOG_INFO("videoPort=%d, index=%d", static_cast<int>(videoPort), index);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVideoPort(videoPort, index, handle);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully, handle=%d", handle);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::IsVideoPortEnabled(const int32_t handle, bool &enabled) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().IsVideoPortEnabled(handle, enabled);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - enabled=%s", enabled ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::EnableVideoPort(const int32_t handle, const bool enabled) {
    DSLOG_INFO("handle=%d, enabled=%s", handle, enabled ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().EnableVideoPort(handle, enabled);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::IsVideoPortDisplayConnected(const int32_t handle, bool &connected) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().IsVideoPortDisplayConnected(handle, connected);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - connected=%s", connected ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::IsVideoPortActive(const int32_t handle, bool &active) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().IsVideoPortActive(handle, active);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - active=%s", active ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetVideoPortResolution(const int32_t handle, VideoPortResolution &resolution) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVideoPortResolution(handle, resolution);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::getIgnoreEDIDStatus(const int32_t handle, bool &ignoreEDID) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().getIgnoreEDIDStatus(handle, ignoreEDID);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - ignoreEDID=%d", static_cast<int>(ignoreEDID));
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetColorDepth(const int32_t handle, uint32_t &colorDepth) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetColorDepth(handle, colorDepth);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - colorDepth=%u", colorDepth);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetVideoPortColorDepth(const int32_t handle, const uint32_t colorDepth) {
    DSLOG_INFO("handle=%d, colorDepth=%u", handle, colorDepth);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetVideoPortColorDepth(handle, colorDepth);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange &quantizationRange) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetQuantizationRange(handle, quantizationRange);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetVideoPortQuantizationRange(const int32_t handle, const VideoPortQuantizationRange quantizationRange) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetVideoPortQuantizationRange(handle, quantizationRange);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetColorSpace(const int32_t handle, VideoPortColorSpace &colorSpace) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetColorSpace(handle, colorSpace);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetColorSpace(const int32_t handle, const VideoPortColorSpace colorSpace) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetColorSpace(handle, colorSpace);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetVideoPortFrameRate(const int32_t handle, uint32_t &frameRate) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVideoPortFrameRate(handle, frameRate);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - frameRate=%u", frameRate);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetVideoPortFrameRate(const int32_t handle, const uint32_t frameRate) {
    DSLOG_INFO("handle=%d, frameRate=%u", handle, frameRate);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetVideoPortFrameRate(handle, frameRate);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetVideoPortHDCPStatus(const int32_t handle, VideoPortHdcpStatus &hdcpStatus) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVideoPortHDCPStatus(handle, hdcpStatus);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDCPProtocolVersionOnVideoPort(handle, hdcpVersion);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDCPReceiverProtocolVersionOnVideoPort(handle, hdcpVersion);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDCPCurrentProtocolVersionOnVideoPort(handle, hdcpVersion);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetVideoPortResolution(const int32_t handle, const VideoPortResolution& resolution, const bool persist, const bool forceCompatibility) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetVideoPortResolution(handle, resolution, persist, forceCompatibility);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t* hdcpKey, const uint16_t hdcpKeySize) {
    DSLOG_INFO("handle=%d, hdcpEnable=%s", handle, hdcpEnable ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().EnableHDCPOnVideoPort(handle, hdcpEnable, hdcpKey, hdcpKeySize);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::IsHDCPEnabledOnVideoPort(const int32_t handle, bool &hdcpEnabled) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().IsHDCPEnabledOnVideoPort(handle, hdcpEnabled);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - hdcpEnabled=%s", hdcpEnabled ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetTVHDRCapabilities(const int32_t handle, int32_t &capabilities) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetTVHDRCapabilities(handle, capabilities);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - capabilities=0x%x", capabilities);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetTVSupportedResolutions(const int32_t handle, int32_t &resolutions) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetTVSupportedResolutions(handle, resolutions);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - resolutions=0x%x", resolutions);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetForceDisable4K(const int32_t handle, const bool disable) {
    DSLOG_INFO("handle=%d, disable=%s", handle, disable ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetForceDisable4K(handle, disable);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetForceDisable4K(const int32_t handle, bool &disabled) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetForceDisable4K(handle, disabled);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - disabled=%s", disabled ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::IsVideoPortOutputHDR(const int32_t handle, bool &isHDR) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().IsVideoPortOutputHDR(handle, isHDR);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - isHDR=%s", isHDR ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::ResetVideoPortOutputToSDR() {
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().ResetVideoPortOutputToSDR();
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIPreference(handle, hdcpVersion);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - hdcpVersion=%d", static_cast<int>(hdcpVersion));
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion) {
    DSLOG_INFO("handle=%d, hdcpVersion=%d", handle, static_cast<int>(hdcpVersion));
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetHDMIPreference(handle, hdcpVersion);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetVideoEOTF(const int32_t handle, HDRStandard &hdrStandard) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVideoEOTF(handle, hdrStandard);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - hdrStandard=%d", static_cast<int>(hdrStandard));
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetMatrixCoefficients(const int32_t handle, DisplayMatrixCoefficients &matrixCoefficients) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetMatrixCoefficients(handle, matrixCoefficients);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - matrixCoefficients=%d", static_cast<int>(matrixCoefficients));
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::IsVideoPortDisplaySurround(const int32_t handle, bool &surround) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().IsVideoPortDisplaySurround(handle, surround);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - surround=%s", surround ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetVideoPortDisplaySurroundMode(const int32_t handle, VideoPortSurroundMode &surroundMode) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVideoPortDisplaySurroundMode(handle, surroundMode);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - surroundMode=%d", static_cast<int>(surroundMode));
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetCurrentOutputSettings(const int32_t handle, DSOutputSettings &outputSettings) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetCurrentOutputSettings(handle, outputSettings);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetBackgroundColor(const int32_t handle, const VideoBackgroundColor backgroundColor) {
    DSLOG_INFO("handle=%d, backgroundColor=%d", handle, static_cast<int>(backgroundColor));
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetBackgroundColor(handle, backgroundColor);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetForceHDRMode(const int32_t handle, const HDRStandard hdrMode) {
    DSLOG_INFO("handle=%d, hdrMode=%d", handle, static_cast<int>(hdrMode));
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetForceHDRMode(handle, hdrMode);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetColorDepthCapabilities(const int32_t handle, uint32_t &colorDepthCapabilities) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetColorDepthCapabilities(handle, colorDepthCapabilities);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - colorDepthCapabilities=0x%x", colorDepthCapabilities);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::GetPreferredColorDepth(const int32_t handle, DisplayColorDepth &colorDepth, const bool persist) {
    DSLOG_INFO("handle=%d, persist=%s", handle, persist ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetPreferredColorDepth(handle, colorDepth, persist);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - colorDepth=%d", static_cast<int>(colorDepth));
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoPort::SetPreferredColorDepth(const int32_t handle, const DisplayColorDepth colorDepth, const bool persist) {
    DSLOG_INFO("handle=%d, colorDepth=%d, persist=%s", handle, static_cast<int>(colorDepth), persist ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetPreferredColorDepth(handle, colorDepth, persist);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

// VideoPort event handling methods - Forward DS HAL events to parent notification system
void VideoPort::OnResolutionPreChange(const ResolutionChange resolution)
{
    DSLOG_INFO("forwarding to parent");
    _parent.OnResolutionPreChange(resolution);
}

void VideoPort::OnResolutionPostChange(const ResolutionChange resolution)
{
    DSLOG_INFO("forwarding to parent");
    _parent.OnResolutionPostChange(resolution);
}

void VideoPort::OnHDCPStatusChange(const VideoPortHdcpStatus hdcpStatus)
{
    DSLOG_INFO("forwarding to parent");
    _parent.OnHDCPStatusChange(hdcpStatus);
}

void VideoPort::OnVideoFormatUpdate(const HDRStandard videoFormatHDR)
{
    DSLOG_INFO("forwarding to parent");
    _parent.OnVideoFormatUpdate(videoFormatHDR);
}