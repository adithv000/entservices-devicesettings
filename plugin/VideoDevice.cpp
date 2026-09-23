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
#include "VideoDevice.h"
#include "hal/dVideoDeviceImpl.h"
#ifdef ENABLE_HDMIOUTPUT_AIDL
#include "hal/dVideoDeviceAIDLImpl.h"
#endif

VideoDevice VideoDevice::Create(INotification& parent)
{
    ENTRY_LOG;
#ifdef ENABLE_HDMIOUTPUT_AIDL
    if (dVideoDeviceAIDLImpl::IsAvailable()) {
        DSLOG_INFO("Using HDMI Output AIDL HAL");
        return VideoDevice(parent, std::make_shared<dVideoDeviceAIDLImpl>());
    }
#endif
    DSLOG_INFO("Using legacy VideoDevice DS HAL");
    return VideoDevice(parent, std::make_shared<DefaultImpl>());
}

VideoDevice::VideoDevice(INotification& parent, std::shared_ptr<IPlatform> platform)
    : _platform(std::move(platform))
    , _parent(parent)
{
    Platform_init();
}

void VideoDevice::Platform_init()
{
    DSLOG_INFO("VideoDevice Init - Setting up event callbacks");
    
    // Set up callback bundle for VideoDevice events - using global CallbackBundle pattern
    CallbackBundle bundle;
    
    bundle.OnZoomSettingsChanged = [this](const VideoDeviceZoom zoomSetting) {
        this->OnZoomSettingsChanged(zoomSetting);
    };
    bundle.OnDisplayFrameratePreChange = [this](const string frameRate) {
        this->OnDisplayFrameratePreChange(frameRate);
    };
    bundle.OnDisplayFrameratePostChange = [this](const string frameRate) {
        this->OnDisplayFrameratePostChange(frameRate);
    };
    
    if (_platform) {
        // Use interface method directly - no casting needed
        this->platform().setAllCallbacks(bundle);
        this->platform().getPersistenceValue();
    }
    
}

uint32_t VideoDevice::GetVideoDeviceHandle(const int32_t index, int32_t &handle) {
    DSLOG_INFO("index=%d", index);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVideoDeviceHandle(index, handle);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully, handle=%d", handle);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::SetVideoDeviceDFC(const int32_t handle, const VideoDeviceZoom zoomSetting) {
    DSLOG_INFO("handle=%d, zoomSetting=%d", handle, static_cast<int>(zoomSetting));
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetVideoDeviceDFC(handle, zoomSetting);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::GetVideoDeviceDFC(const int32_t handle, VideoDeviceZoom &zoomSetting) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVideoDeviceDFC(handle, zoomSetting);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - zoomSetting=%d", static_cast<int>(zoomSetting));
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::GetHDRCapabilities(const int32_t handle, int32_t &capabilities) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDRCapabilities(handle, capabilities);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - capabilities=0x%x", capabilities);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::GetSupportedVideoCodingFormats(const int32_t handle, int32_t &supportedFormats) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetSupportedVideoCodingFormats(handle, supportedFormats);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - supportedFormats=0x%x", supportedFormats);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo) {
    DSLOG_INFO("handle=%d, videoCodec=%d", handle, static_cast<int>(videoCodec));
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetCodecInfo(handle, videoCodec, codecInfo);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - codecInfo returned");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::DisableHDR(const int32_t handle, const bool disable) {
    DSLOG_INFO("handle=%d, disable=%s", handle, disable ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().DisableHDR(handle, disable);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::SetFRFMode(const int32_t handle, const int32_t frfmode) {
    DSLOG_INFO("handle=%d, frfmode=%d", handle, frfmode);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetFRFMode(handle, frfmode);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::GetFRFMode(const int32_t handle, int32_t &frfmode) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetFRFMode(handle, frfmode);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - frfmode=%d", frfmode);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::GetCurrentDisplayFrameRate(const int32_t handle, string &framerate) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetCurrentDisplayFrameRate(handle, framerate);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - framerate=%s", framerate.c_str());
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t VideoDevice::SetDisplayFrameRate(const int32_t handle, const string framerate) {
    DSLOG_INFO("handle=%d, framerate=%s", handle, framerate.c_str());
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetDisplayFrameRate(handle, framerate);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

// VideoDevice event handlers - called by DS HAL to forward events to parent
void VideoDevice::OnZoomSettingsChanged(const VideoDeviceZoom zoomSetting) {
    DSLOG_INFO("DS HAL OnZoomSettingsChanged event: zoomSetting=%d", static_cast<int>(zoomSetting));
    _parent.OnZoomSettingsChanged(zoomSetting);
}

void VideoDevice::OnDisplayFrameratePreChange(const string frameRate) {
    DSLOG_INFO("DS HAL OnDisplayFrameratePreChange event: frameRate=%s", frameRate.c_str());
    _parent.OnDisplayFrameratePreChange(frameRate);
}

void VideoDevice::OnDisplayFrameratePostChange(const string frameRate) {
    DSLOG_INFO("DS HAL OnDisplayFrameratePostChange event: frameRate=%s", frameRate.c_str());
    _parent.OnDisplayFrameratePostChange(frameRate);
}