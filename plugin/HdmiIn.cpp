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

#include "HdmiIn.h"
#include "DeviceSettingsTypes.h"

using IPlatform = hal::dHdmiIn::IPlatform;
using DefaultImpl = dHdmiInImpl;

#include "hal/dHdmiIn.h"
namespace hal {
namespace dHdmiIn {
    IPlatform::~IPlatform() {}
}
}

HdmiIn::HdmiIn(INotification& parent, std::shared_ptr<IPlatform> platform)
    : _platform(std::move(platform))
    , _parent(parent)
{
    Platform_init();
}

void HdmiIn::Platform_init()
{
    CallbackBundle bundle;
    bundle.OnHDMIInHotPlugEvent = [this](HDMIInPort port, bool isConnected) {
        this->OnHDMIInHotPlugEvent(port, isConnected);
    };
    bundle.OnHDMIInSignalStatusEvent = [this](HDMIInPort port, HDMIInSignalStatus signalStatus) {
        this->OnHDMIInSignalStatusEvent(port, signalStatus);
    };
    bundle.OnHDMIInStatusEvent = [this](HDMIInPort port, bool isConnected) {
        this->OnHDMIInStatusEvent(port, isConnected);
    };
    bundle.OnHDMIInVideoModeUpdateEvent = [this](HDMIInPort port, HDMIVideoPortResolution videoPortResolution) {
        this->OnHDMIInVideoModeUpdateEvent(port, std::move(videoPortResolution));
    };
    bundle.OnHDMIInAllmStatusEvent = [this](HDMIInPort port, bool allmStatus) {
        this->OnHDMIInAllmStatusEvent(port, allmStatus);
    };
    bundle.OnHDMIInAVIContentTypeEvent = [this](HDMIInPort port, HDMIInAviContentType aviContentType) {
        this->OnHDMIInAVIContentTypeEvent(port, aviContentType);
    };
    bundle.OnHDMIInAVLatencyEvent = [this](int32_t audioDelay, int32_t videoDelay) {
        this->OnHDMIInAVLatencyEvent(audioDelay, videoDelay);
    };
    bundle.OnHDMIInVRRStatusEvent = [this](HDMIInPort port, HDMIInVRRType vrrType) {
        this->OnHDMIInVRRStatusEvent(port, vrrType);
    };
    if (_platform) {
        this->platform().setAllCallbacks(bundle);
        this->platform().getPersistenceValue();
    }
}

void HdmiIn::OnHDMIInHotPlugEvent(const HDMIInPort port, const bool isConnected)
{
    _parent.OnHDMIInEventHotPlugNotification(port, isConnected);
}

void HdmiIn::OnHDMIInSignalStatusEvent(const HDMIInPort port, const HDMIInSignalStatus signalStatus)
{
    _parent.OnHDMIInEventSignalStatusNotification(port, signalStatus);
}

void HdmiIn::OnHDMIInStatusEvent(const HDMIInPort activePort, const bool isPresented)
{
    _parent.OnHDMIInEventStatusNotification(activePort, isPresented);
}

void HdmiIn::OnHDMIInVideoModeUpdateEvent(const HDMIInPort port, HDMIVideoPortResolution videoPortResolution)
{
    _parent.OnHDMIInVideoModeUpdateNotification(port, std::move(videoPortResolution));
}

void HdmiIn::OnHDMIInAllmStatusEvent(const HDMIInPort port, const bool allmStatus)
{
    _parent.OnHDMIInAllmStatusNotification(port, allmStatus);
}

void HdmiIn::OnHDMIInAVIContentTypeEvent(const HDMIInPort port, const HDMIInAviContentType aviContentType)
{
    _parent.OnHDMIInAVIContentTypeNotification(port, aviContentType);
}

void HdmiIn::OnHDMIInAVLatencyEvent(const int32_t audioDelay, const int32_t videoDelay)
{
    _parent.OnHDMIInAVLatencyNotification(audioDelay, videoDelay);
}

void HdmiIn::OnHDMIInVRRStatusEvent(const HDMIInPort port, const HDMIInVRRType vrrType)
{
    _parent.OnHDMIInVRRStatusNotification(port, vrrType);
}

uint32_t HdmiIn::GetHDMIInNumberOfInputs(int32_t &count) {

    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIInNumberOfInputs(count);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - count=%d", count);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetHDMIInStatus(HDMIInStatus &hdmiStatus, IHDMIInPortConnectionStatusIterator*& portConnectionStatus) {

    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIInStatus(hdmiStatus, portConnectionStatus);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::SelectHDMIInPort(const HDMIInPort port, const bool requestAudioMix, const bool topMostPlane, const HDMIVideoPlaneType videoPlaneType) {

    DSLOG_INFO("port=%d, requestAudioMix=%s, topMostPlane=%s, videoPlaneType=%d",
        port, requestAudioMix ? "true" : "false", topMostPlane ? "true" : "false", videoPlaneType);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SelectHDMIInPort(port, requestAudioMix, topMostPlane, videoPlaneType);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::ScaleHDMIInVideo(const HDMIInVideoRectangle videoPosition) {

    DSLOG_INFO("x=%d, y=%d, w=%d, h=%d", videoPosition.x, videoPosition.y, videoPosition.width, videoPosition.height);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().ScaleHDMIInVideo(videoPosition);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::SelectHDMIZoomMode(const HDMIInVideoZoom zoomMode) {

    DSLOG_INFO("zoomMode=%d", zoomMode);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SelectHDMIZoomMode(zoomMode);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetSupportedGameFeaturesList(IHDMIInGameFeatureListIterator *& gameFeatureList) {

    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetSupportedGameFeaturesList(gameFeatureList);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetHDMIInAVLatency(uint32_t &videoLatency, uint32_t &audioLatency) {

    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIInAVLatency(videoLatency, audioLatency);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - videoLatency=%u, audioLatency=%u", videoLatency, audioLatency);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetHDMIInAllmStatus(const HDMIInPort port, bool &allmStatus) {

    DSLOG_INFO("port=%d", port);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIInAllmStatus(port, allmStatus);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - port=%d, allmStatus=%s", port, allmStatus ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetHDMIInEdid2AllmSupport(const HDMIInPort port, bool &allmSupport) {

    DSLOG_INFO("port=%d", port);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIInEdid2AllmSupport(port, allmSupport);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - port=%d, allmSupport=%s", port, allmSupport ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::SetHDMIInEdid2AllmSupport(const HDMIInPort port, bool allmSupport) {

    DSLOG_INFO("port=%d, allmSupport=%s", port, allmSupport ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetHDMIInEdid2AllmSupport(port, allmSupport);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetEdidBytes(const HDMIInPort port, const uint16_t edidBytesLength, uint8_t edidBytes[]) {

    DSLOG_INFO("port=%d, edidBytesLength=%u", port, edidBytesLength);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetEdidBytes(port, edidBytesLength, edidBytes);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetHDMISPDInformation(const HDMIInPort port, const uint16_t spdBytesLength, uint8_t spdBytes[]) {

    DSLOG_INFO("port=%d, spdBytesLength=%u", port, spdBytesLength);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMISPDInformation(port, spdBytesLength, spdBytes);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetHDMIEdidVersion(const HDMIInPort port, HDMIInEdidVersion &edidVersion) {

    DSLOG_INFO("port=%d", port);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIEdidVersion(port, edidVersion);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - port=%d, edidVersion=%d", port, edidVersion);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::SetHDMIEdidVersion(const HDMIInPort port, const HDMIInEdidVersion edidVersion) {

    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetHDMIEdidVersion(port, edidVersion);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - port=%d, edidVersion=%d", port, edidVersion);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetHDMIVideoMode(HDMIVideoPortResolution &videoPortResolution) {

    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIVideoMode(videoPortResolution);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetHDMIVersion(const HDMIInPort port, HDMIInCapabilityVersion &capabilityVersion) {

    DSLOG_INFO("port=%d", port);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetHDMIVersion(port, capabilityVersion);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - port=%d, capabilityVersion=%d", port, capabilityVersion);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetVRRSupport(const HDMIInPort port, bool &vrrSupport) {

    DSLOG_INFO("port=%d", port);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVRRSupport(port, vrrSupport);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - port=%d, vrrSupport=%s", port, vrrSupport ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::SetVRRSupport(const HDMIInPort port, const bool vrrSupport) {

    DSLOG_INFO("port=%d, vrrSupport=%s", port, vrrSupport ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetVRRSupport(port, vrrSupport);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t HdmiIn::GetVRRStatus(const HDMIInPort port, HDMIInVRRStatus &vrrStatus) {

    DSLOG_INFO("port=%d", port);
    memset(&vrrStatus, 0, sizeof(vrrStatus));
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetVRRStatus(port, vrrStatus);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - port=%d, vrrType=%d", port, vrrStatus.vrrType);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}