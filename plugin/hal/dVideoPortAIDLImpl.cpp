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
#include "dVideoPortAIDLImpl.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/PropertyValue.h>
#include <com/rdk/hal/hdmioutput/Capabilities.h>
#include <com/rdk/hal/hdmioutput/HDCPProtocolVersion.h>
#include <com/rdk/hal/hdmioutput/HDCPStatus.h>
#include <com/rdk/hal/hdmioutput/HDROutputMode.h>
#include <com/rdk/hal/hdmioutput/PixelFormat.h>
#include <com/rdk/hal/hdmioutput/Property.h>
#include <com/rdk/hal/hdmioutput/PropertyKVPair.h>
#include <com/rdk/hal/hdmioutput/State.h>
#include <com/rdk/hal/hdmioutput/VIC.h>

#include <cstring>

namespace HdmiOutput = com::rdk::hal::hdmioutput;
using PropertyValue = com::rdk::hal::PropertyValue;

namespace {
template <typename Interface>
android::sp<Interface> GetService()
{
    const android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    if (serviceManager == nullptr) {
        return nullptr;
    }
    const android::sp<android::IBinder> binder = serviceManager->checkService(
        android::String16(Interface::serviceName().c_str()));
    return binder == nullptr ? nullptr : android::interface_cast<Interface>(binder);
}

// Subset of VICs mapped to the resolution names understood by the rest of the plugin.
// Matches the resolution families produced by dVideoPortImpl::buildResolutionFromName().
struct VicResolutionEntry {
    HdmiOutput::VIC vic;
    const char* name;
    VideoResolution pixelResolution;
    VideoAspectRatio aspectRatio;
    VideoFrameRate frameRate;
    bool interlaced;
};

const VicResolutionEntry kVicTable[] = {
    { HdmiOutput::VIC::VIC3_720_480_P_60_16_9,     "480p",      VideoResolution::DS_VIDEO_PIXELRES_720X480,   VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, false },
    { HdmiOutput::VIC::VIC18_720_576_P_50_16_9,    "576p50",    VideoResolution::DS_VIDEO_PIXELRES_720X576,   VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, false },
    { HdmiOutput::VIC::VIC4_1280_720_P_60_16_9,    "720p",      VideoResolution::DS_VIDEO_PIXELRES_1280X720,  VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, false },
    { HdmiOutput::VIC::VIC19_1280_720_P_50_16_9,   "720p50",    VideoResolution::DS_VIDEO_PIXELRES_1280X720,  VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, false },
    { HdmiOutput::VIC::VIC5_1920_1080_I_60_16_9,   "1080i",     VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, true  },
    { HdmiOutput::VIC::VIC20_1920_1080_I_50_16_9,  "1080i50",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, true  },
    { HdmiOutput::VIC::VIC16_1920_1080_P_60_16_9,  "1080p",     VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, false },
    { HdmiOutput::VIC::VIC32_1920_1080_P_24_16_9,  "1080p24",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_24, false },
    { HdmiOutput::VIC::VIC33_1920_1080_P_25_16_9,  "1080p25",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_25, false },
    { HdmiOutput::VIC::VIC34_1920_1080_P_30_16_9,  "1080p30",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_30, false },
    { HdmiOutput::VIC::VIC31_1920_1080_P_50_16_9,  "1080p50",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, false },
    { HdmiOutput::VIC::VIC93_3840_2160_P_24_16_9,  "2160p24",   VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_24, false },
    { HdmiOutput::VIC::VIC94_3840_2160_P_25_16_9,  "2160p25",   VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_25, false },
    { HdmiOutput::VIC::VIC95_3840_2160_P_30_16_9,  "2160p30",   VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_30, false },
    { HdmiOutput::VIC::VIC96_3840_2160_P_50_16_9,  "2160p50",   VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, false },
    { HdmiOutput::VIC::VIC97_3840_2160_P_60_16_9,  "2160p",     VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, false },
};

int32_t ExtractIntProperty(const std::optional<PropertyValue>& value, int32_t defaultValue)
{
    if (!value.has_value() || !value->value.has_value() || value->value->getTag() != PropertyValue::Value::intValue) {
        return defaultValue;
    }
    return value->value->get<PropertyValue::Value::intValue>();
}
}

class dVideoPortAIDLImpl::ControllerListener : public HdmiOutput::BnHDMIOutputControllerListener {
public:
    explicit ControllerListener(dVideoPortAIDLImpl& parent)
        : _parent(parent)
    {
    }

    android::binder::Status onHotPlugDetectStateChanged(bool state) override
    {
        _parent.OnHotPlugDetectStateChanged(state);
        return android::binder::Status::ok();
    }

    android::binder::Status onFrameRateChanged() override
    {
        _parent.OnFrameRateChanged();
        return android::binder::Status::ok();
    }

    android::binder::Status onHDCPStatusChanged(HdmiOutput::HDCPStatus hdcpStatus, HdmiOutput::HDCPProtocolVersion hdcpProtocolVersion) override
    {
        _parent.OnHDCPStatusChanged(hdcpStatus, hdcpProtocolVersion);
        return android::binder::Status::ok();
    }

    android::binder::Status onEDID(const std::vector<uint8_t>&) override
    {
        return android::binder::Status::ok();
    }

private:
    dVideoPortAIDLImpl& _parent;
};

class dVideoPortAIDLImpl::EventListener : public HdmiOutput::BnHDMIOutputEventListener {
public:
    explicit EventListener(dVideoPortAIDLImpl& parent)
        : _parent(parent)
    {
    }

    android::binder::Status onStateChanged(HdmiOutput::State oldState, HdmiOutput::State newState) override
    {
        _parent.OnStateChanged(oldState, newState);
        return android::binder::Status::ok();
    }

private:
    dVideoPortAIDLImpl& _parent;
};

dVideoPortAIDLImpl::dVideoPortAIDLImpl()
    : _opened(false)
{
    InitialiseHAL();
}

dVideoPortAIDLImpl::~dVideoPortAIDLImpl()
{
    DeInitialiseHAL();
}

android::sp<dVideoPortAIDLImpl::Manager> dVideoPortAIDLImpl::GetManager()
{
    return GetService<Manager>();
}

bool dVideoPortAIDLImpl::IsAvailable()
{
    return GetManager() != nullptr;
}

bool dVideoPortAIDLImpl::ResolveDefaultOutput()
{
    if (_manager == nullptr) {
        return false;
    }

    std::vector<Output::Id> ids;
    if (!_manager->getHDMIOutputIds(&ids).isOk() || ids.empty()) {
        DSLOG_ERR("getHDMIOutputIds failed or returned no HDMI outputs");
        return false;
    }

    _outputId = ids.front();
    if (!_manager->getHDMIOutput(_outputId, &_output).isOk() || _output == nullptr) {
        DSLOG_ERR("getHDMIOutput failed for id=%d", _outputId.value);
        return false;
    }
    return true;
}

void dVideoPortAIDLImpl::InitialiseHAL()
{
    std::lock_guard<std::mutex> lock(_adminLock);
    if (_opened) {
        return;
    }

    _manager = GetManager();
    if (_manager == nullptr) {
        DSLOG_ERR("HDMI Output AIDL service is not available");
        return;
    }
    android::ProcessState::self()->startThreadPool();

    if (!ResolveDefaultOutput()) {
        _manager.clear();
        return;
    }

    _controllerListener = new ControllerListener(*this);
    _eventListener = new EventListener(*this);
    bool registered = false;
    if (!_output->registerEventListener(_eventListener, &registered).isOk() || !registered) {
        DSLOG_ERR("registerEventListener failed on default HDMI output");
    }

    if (!_output->open(_controllerListener, &_controller).isOk() || _controller == nullptr) {
        DSLOG_ERR("open() failed on default HDMI output");
        return;
    }
    if (!_controller->start().isOk()) {
        DSLOG_ERR("HDMIOutputController::start() failed");
        return;
    }

    _opened = true;
    DSLOG_INFO("HDMI Output AIDL HAL initialized (outputId=%d)", _outputId.value);
}

void dVideoPortAIDLImpl::DeInitialiseHAL()
{
    std::lock_guard<std::mutex> lock(_adminLock);
    if (_controller != nullptr) {
        _controller->stop();
    }
    if (_output != nullptr) {
        if (_controller != nullptr) {
            bool closed = false;
            _output->close(_controller, &closed);
        }
        if (_eventListener != nullptr) {
            bool unregistered = false;
            _output->unregisterEventListener(_eventListener, &unregistered);
        }
    }
    _controller.clear();
    _controllerListener.clear();
    _eventListener.clear();
    _output.clear();
    _manager.clear();
    _opened = false;
}

void dVideoPortAIDLImpl::setAllCallbacks(const CallbackBundle& bundle)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    _callbacks = bundle;
}

void dVideoPortAIDLImpl::getPersistenceValue()
{
    // Persistence (resolution/color depth caching) is owned by the AIDL HAL implementation itself.
}

uint32_t dVideoPortAIDLImpl::GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t& handle)
{
    if (!_opened || index != 0) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // Only the HDMI/INTERNAL default output is backed by the AIDL HAL.
    if (videoPort != VideoPortType::DS_VIDEO_PORT_TYPE_HDMI && videoPort != VideoPortType::DS_VIDEO_PORT_TYPE_INTERNAL) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    handle = kDefaultHandle;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::IsVideoPortEnabled(const int32_t handle, bool& enabled)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::State state;
    if (!_output->getState(&state).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    enabled = (state != HdmiOutput::State::CLOSED);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::EnableVideoPort(const int32_t handle, const bool enabled)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // The AIDL HAL keeps the controller started for the lifetime of this object (see open()/start()).
    return enabled ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::IsVideoPortDisplayConnected(const int32_t handle, bool& connected)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    if (!_controller->getHotPlugDetectState(&connected).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::IsVideoPortActive(const int32_t handle, bool& active)
{
    return IsVideoPortDisplayConnected(handle, active);
}

bool dVideoPortAIDLImpl::VicToResolution(HdmiOutput::VIC vic, VideoPortResolution& resolution)
{
    for (const auto& entry : kVicTable) {
        if (entry.vic == vic) {
            resolution.name = entry.name;
            resolution.pixelResolution = entry.pixelResolution;
            resolution.aspectRatio = entry.aspectRatio;
            resolution.stereoScopicMode = VideoStereoScopicMode::DS_VIDEO_SSMODE_2D;
            resolution.frameRate = entry.frameRate;
            resolution.interlaced = entry.interlaced;
            return true;
        }
    }
    return false;
}

bool dVideoPortAIDLImpl::ResolutionNameToVic(const std::string& name, HdmiOutput::VIC& vic)
{
    for (const auto& entry : kVicTable) {
        if (name == entry.name) {
            vic = entry.vic;
            return true;
        }
    }
    return false;
}

uint32_t dVideoPortAIDLImpl::GetVideoPortResolution(const int32_t handle, VideoPortResolution& resolution)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    std::optional<PropertyValue> value;
    if (!_output->getProperty(HdmiOutput::Property::VIC, &value).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    const HdmiOutput::VIC vic = static_cast<HdmiOutput::VIC>(ExtractIntProperty(value, 0));
    if (!VicToResolution(vic, resolution)) {
        DSLOG_WARN("VIC=%d has no mapped resolution name, defaulting to 1080p", static_cast<int>(vic));
        VicToResolution(HdmiOutput::VIC::VIC16_1920_1080_P_60_16_9, resolution);
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetVideoPortResolution(const int32_t handle, const VideoPortResolution resolution, const bool persist, const bool forceCompatibility)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::VIC vic;
    if (!ResolutionNameToVic(resolution.name, vic)) {
        DSLOG_ERR("Unsupported resolution name '%s' for AIDL HDMI Output HAL", resolution.name.c_str());
        return WPEFramework::Core::ERROR_BAD_REQUEST;
    }

    bool connected = false;
    if (_controller->getHotPlugDetectState(&connected).isOk() && !connected) {
        DSLOG_INFO("Port not connected, ignoring resolution request");
        return WPEFramework::Core::ERROR_GENERAL;
    }

    PropertyValue value;
    value.value = PropertyValue::Value::make<PropertyValue::Value::intValue>(static_cast<int32_t>(vic));
    bool applied = false;
    if (!_controller->setProperty(HdmiOutput::Property::VIC, value, &applied).isOk() || !applied) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    // persist/forceCompatibility mirror the legacy DS HAL semantics; AIDL HAL owns persistence itself.
    (void)persist;
    (void)forceCompatibility;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::getIgnoreEDIDStatus(const int32_t handle, bool& ignoreEDID)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    ignoreEDID = false;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetColorDepth(const int32_t handle, uint32_t& colorDepth)
{
    (void)handle;
    colorDepth = 0;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetVideoPortColorDepth(const int32_t handle, const uint32_t colorDepth)
{
    (void)handle;
    (void)colorDepth;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange& quantizationRange)
{
    (void)handle;
    quantizationRange = VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_UNKNOWN;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetVideoPortQuantizationRange(const int32_t handle, const VideoPortQuantizationRange quantizationRange)
{
    (void)handle;
    (void)quantizationRange;
    DSLOG_WARN("quantization range is a read-only sink attribute; not supported by AIDL HAL");
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetColorSpace(const int32_t handle, VideoPortColorSpace& colorSpace)
{
    (void)handle;
    colorSpace = VideoPortColorSpace::DS_DISPLAY_COLORSPACE_UNKNOWN;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetColorSpace(const int32_t handle, const VideoPortColorSpace colorSpace)
{
    (void)handle;
    (void)colorSpace;
    DSLOG_WARN("color space is a read-only, EDID-negotiated attribute; not supported by AIDL HAL");
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetVideoPortFrameRate(const int32_t handle, uint32_t& frameRate)
{
    VideoPortResolution resolution;
    const uint32_t result = GetVideoPortResolution(handle, resolution);
    if (result != WPEFramework::Core::ERROR_NONE) {
        return result;
    }
    switch (resolution.frameRate) {
    case VideoFrameRate::DS_VIDEO_FRAMERATE_24: frameRate = 24; break;
    case VideoFrameRate::DS_VIDEO_FRAMERATE_25: frameRate = 25; break;
    case VideoFrameRate::DS_VIDEO_FRAMERATE_30: frameRate = 30; break;
    case VideoFrameRate::DS_VIDEO_FRAMERATE_50: frameRate = 50; break;
    case VideoFrameRate::DS_VIDEO_FRAMERATE_60: frameRate = 60; break;
    default: frameRate = 60; break;
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetVideoPortFrameRate(const int32_t handle, const uint32_t frameRate)
{
    (void)handle;
    (void)frameRate;
    DSLOG_WARN("frame rate is implicit in the VIC/resolution; use SetVideoPortResolution");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

VideoPortHdcpStatus dVideoPortAIDLImpl::ConvertHdcpStatus(HdmiOutput::HDCPStatus status)
{
    switch (status) {
    case HdmiOutput::HDCPStatus::UNAUTHENTICATED: return VideoPortHdcpStatus::DS_HDCP_STATUS_UNAUTHENTICATED;
    case HdmiOutput::HDCPStatus::AUTHENTICATION_IN_PROGRESS: return VideoPortHdcpStatus::DS_HDCP_STATUS_INPROGRESS;
    case HdmiOutput::HDCPStatus::AUTHENTICATION_FAILURE: return VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATIONFAILURE;
    case HdmiOutput::HDCPStatus::AUTHENTICATED: return VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATED;
    default: return VideoPortHdcpStatus::DS_HDCP_STATUS_UNPOWERED;
    }
}

VideoPortHdcpProtocolVersion dVideoPortAIDLImpl::ConvertHdcpVersion(HdmiOutput::HDCPProtocolVersion version)
{
    return version == HdmiOutput::HDCPProtocolVersion::VERSION_2_X
        ? VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_2X
        : VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X;
}

HdmiOutput::HDCPProtocolVersion dVideoPortAIDLImpl::ConvertHdcpVersion(const VideoPortHdcpProtocolVersion version)
{
    return version == VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_2X
        ? HdmiOutput::HDCPProtocolVersion::VERSION_2_X
        : HdmiOutput::HDCPProtocolVersion::VERSION_1_X;
}

uint32_t dVideoPortAIDLImpl::GetVideoPortHDCPStatus(const int32_t handle, VideoPortHdcpStatus& hdcpStatus)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::HDCPStatus status;
    if (!_controller->getHDCPStatus(&status).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    hdcpStatus = ConvertHdcpStatus(status);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::Capabilities capabilities;
    if (!_output->getCapabilities(&capabilities).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    bool supports2x = false;
    for (const auto& supported : capabilities.supportedHDCPProtocolVersions) {
        supports2x = supports2x || (supported == HdmiOutput::HDCPProtocolVersion::VERSION_2_X);
    }
    hdcpVersion = supports2x ? VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_2X : VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::HDCPProtocolVersion version;
    if (!_controller->getHDCPReceiverVersion(&version).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    hdcpVersion = ConvertHdcpVersion(version);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::HDCPProtocolVersion version;
    if (!_controller->getHDCPCurrentVersion(&version).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    hdcpVersion = ConvertHdcpVersion(version);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t* hdcpKey, const uint16_t hdcpKeySize)
{
    (void)handle;
    (void)hdcpEnable;
    (void)hdcpKey;
    (void)hdcpKeySize;
    DSLOG_WARN("HDCP enable/disable is not exposed by the AIDL HDMI Output controller");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::IsHDCPEnabledOnVideoPort(const int32_t handle, bool& hdcpEnabled)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::HDCPStatus status;
    if (!_controller->getHDCPStatus(&status).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    hdcpEnabled = (status != HdmiOutput::HDCPStatus::UNKNOWN);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetTVHDRCapabilities(const int32_t handle, int32_t& capabilities)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::Capabilities caps;
    if (!_output->getCapabilities(&caps).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    int32_t bitmask = 0x0; // dsHDRSTANDARD_NONE
    for (const auto& mode : caps.supportedHDROutputModes) {
        switch (mode) {
        case HdmiOutput::HDROutputMode::HLG:          bitmask |= 0x02; break;
        case HdmiOutput::HDROutputMode::HDR10:        bitmask |= 0x01; break;
        case HdmiOutput::HDROutputMode::HDR10_PLUS:   bitmask |= 0x10; break;
        case HdmiOutput::HDROutputMode::DOLBY_VISION: bitmask |= 0x04; break;
        default: break;
        }
    }
    capabilities = bitmask;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetTVSupportedResolutions(const int32_t handle, int32_t& resolutions)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::Capabilities caps;
    if (!_output->getCapabilities(&caps).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    int32_t bitmask = 0;
    for (const auto& vic : caps.supportedVICs) {
        VideoPortResolution resolution;
        if (VicToResolution(vic, resolution)) {
            bitmask |= (1 << static_cast<int32_t>(resolution.pixelResolution));
        }
    }
    resolutions = bitmask;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetForceDisable4K(const int32_t handle, const bool disable)
{
    (void)handle;
    (void)disable;
    DSLOG_WARN("force-disable-4K is not implemented by the AIDL HDMI Output HAL");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::GetForceDisable4K(const int32_t handle, bool& disabled)
{
    (void)handle;
    disabled = false;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

HDRStandard dVideoPortAIDLImpl::ConvertHdrOutputMode(HdmiOutput::HDROutputMode mode)
{
    switch (mode) {
    case HdmiOutput::HDROutputMode::HLG:          return HDRStandard::DS_HDRSTANDARD_HLG;
    case HdmiOutput::HDROutputMode::HDR10:        return HDRStandard::DS_HDRSTANDARD_HDR10;
    case HdmiOutput::HDROutputMode::HDR10_PLUS:   return HDRStandard::DS_HDRSTANDARD_HDR10PLUS;
    case HdmiOutput::HDROutputMode::DOLBY_VISION: return HDRStandard::DS_HDRSTANDARD_DOLBYVISION;
    default:                                      return HDRStandard::DS_HDRSTANDARD_SDR;
    }
}

HdmiOutput::HDROutputMode dVideoPortAIDLImpl::ConvertHdrStandard(const HDRStandard hdrStandard)
{
    switch (hdrStandard) {
    case HDRStandard::DS_HDRSTANDARD_HLG:         return HdmiOutput::HDROutputMode::HLG;
    case HDRStandard::DS_HDRSTANDARD_HDR10:       return HdmiOutput::HDROutputMode::HDR10;
    case HDRStandard::DS_HDRSTANDARD_HDR10PLUS:   return HdmiOutput::HDROutputMode::HDR10_PLUS;
    case HDRStandard::DS_HDRSTANDARD_DOLBYVISION: return HdmiOutput::HDROutputMode::DOLBY_VISION;
    default:                                      return HdmiOutput::HDROutputMode::AUTO;
    }
}

uint32_t dVideoPortAIDLImpl::IsVideoPortOutputHDR(const int32_t handle, bool& isHDR)
{
    HDRStandard hdrStandard;
    const uint32_t result = GetVideoEOTF(handle, hdrStandard);
    if (result != WPEFramework::Core::ERROR_NONE) {
        return result;
    }
    isHDR = (hdrStandard != HDRStandard::DS_HDRSTANDARD_SDR && hdrStandard != HDRStandard::DS_HDRSTANDARD_NONE);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::ResetVideoPortOutputToSDR()
{
    if (!_opened) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    PropertyValue value;
    value.value = PropertyValue::Value::make<PropertyValue::Value::intValue>(static_cast<int32_t>(HdmiOutput::HDROutputMode::AUTO));
    bool applied = false;
    if (!_controller->setProperty(HdmiOutput::Property::HDR_OUTPUT_MODE, value, &applied).isOk() || !applied) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion)
{
    (void)handle;
    hdcpVersion = VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion)
{
    (void)handle;
    (void)hdcpVersion;
    DSLOG_WARN("HDMI HDCP preference is not exposed by the AIDL HDMI Output HAL");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::GetVideoEOTF(const int32_t handle, HDRStandard& hdrStandard)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    std::optional<PropertyValue> value;
    if (!_output->getProperty(HdmiOutput::Property::HDR_OUTPUT_MODE, &value).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    const auto mode = static_cast<HdmiOutput::HDROutputMode>(ExtractIntProperty(value, static_cast<int32_t>(HdmiOutput::HDROutputMode::AUTO)));
    hdrStandard = ConvertHdrOutputMode(mode);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetMatrixCoefficients(const int32_t handle, DisplayMatrixCoefficients& matrixCoefficients)
{
    (void)handle;
    matrixCoefficients = DisplayMatrixCoefficients::DS_DISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::IsVideoPortDisplaySurround(const int32_t handle, bool& surround)
{
    (void)handle;
    surround = false;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::GetVideoPortDisplaySurroundMode(const int32_t handle, VideoPortSurroundMode& surroundMode)
{
    (void)handle;
    surroundMode = VideoPortSurroundMode::DS_VIDEO_PORT_SURROUNDMODE_NONE;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::GetCurrentOutputSettings(const int32_t handle, DSOutputSettings& outputSettings)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HDRStandard hdrStandard = HDRStandard::DS_HDRSTANDARD_SDR;
    GetVideoEOTF(handle, hdrStandard);
    outputSettings.videoEotf = hdrStandard;
    outputSettings.matrixCoefficients = DisplayMatrixCoefficients::DS_DISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
    outputSettings.colorSpace = VideoPortColorSpace::DS_DISPLAY_COLORSPACE_UNKNOWN;
    outputSettings.quantizationRange = VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_UNKNOWN;

    uint32_t colorDepthCapabilities = 0;
    GetColorDepthCapabilities(handle, colorDepthCapabilities);
    outputSettings.colorDepth = colorDepthCapabilities & 0x01 ? 8 : (colorDepthCapabilities & 0x02 ? 10 : 0);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetBackgroundColor(const int32_t handle, const VideoBackgroundColor backgroundColor)
{
    (void)handle;
    (void)backgroundColor;
    DSLOG_WARN("background color is not supported by the AIDL HDMI Output HAL");
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetForceHDRMode(const int32_t handle, const HDRStandard hdrMode)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    PropertyValue value;
    value.value = PropertyValue::Value::make<PropertyValue::Value::intValue>(static_cast<int32_t>(ConvertHdrStandard(hdrMode)));
    bool applied = false;
    if (!_controller->setProperty(HdmiOutput::Property::HDR_OUTPUT_MODE, value, &applied).isOk() || !applied) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetColorDepthCapabilities(const int32_t handle, uint32_t& colorDepthCapabilities)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::Capabilities caps;
    if (!_output->getCapabilities(&caps).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    uint32_t bitmask = 0x08; // dsDISPLAY_COLORDEPTH_AUTO
    for (const int32_t depth : caps.supportedColorDepths) {
        switch (depth) {
        case 8:  bitmask |= 0x01; break;
        case 10: bitmask |= 0x02; break;
        case 12: bitmask |= 0x04; break;
        default: break;
        }
    }
    colorDepthCapabilities = bitmask;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetPreferredColorDepth(const int32_t handle, DisplayColorDepth& colorDepth, const bool persist)
{
    (void)handle;
    (void)persist;
    colorDepth = DisplayColorDepth::DS_DISPLAY_COLORDEPTH_UNKNOWN;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetPreferredColorDepth(const int32_t handle, const DisplayColorDepth colorDepth, const bool persist)
{
    (void)handle;
    (void)colorDepth;
    (void)persist;
    DSLOG_WARN("preferred color depth is not exposed by the AIDL HDMI Output HAL");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

void dVideoPortAIDLImpl::OnHotPlugDetectStateChanged(bool connected)
{
    DSLOG_INFO("connected=%s", connected ? "true" : "false");
    // No hotplug slot exists in hal::dVideoPort::CallbackBundle; the Display plugin owns hotplug reporting.
}

void dVideoPortAIDLImpl::OnFrameRateChanged()
{
    VideoPortResolution resolution;
    if (GetVideoPortResolution(kDefaultHandle, resolution) != WPEFramework::Core::ERROR_NONE) {
        return;
    }
    if (_callbacks.OnResolutionPostChange) {
        ResolutionChange change;
        switch (resolution.pixelResolution) {
        case VideoResolution::DS_VIDEO_PIXELRES_720X480:   change.width = 720;  change.height = 480;  break;
        case VideoResolution::DS_VIDEO_PIXELRES_720X576:   change.width = 720;  change.height = 576;  break;
        case VideoResolution::DS_VIDEO_PIXELRES_1280X720:  change.width = 1280; change.height = 720;  break;
        case VideoResolution::DS_VIDEO_PIXELRES_1920X1080: change.width = 1920; change.height = 1080; break;
        case VideoResolution::DS_VIDEO_PIXELRES_3840X2160: change.width = 3840; change.height = 2160; break;
        default:                                           change.width = 1920; change.height = 1080; break;
        }
        _callbacks.OnResolutionPostChange(change);
    }
}

void dVideoPortAIDLImpl::OnHDCPStatusChanged(HdmiOutput::HDCPStatus status, HdmiOutput::HDCPProtocolVersion /*version*/)
{
    if (_callbacks.OnHDCPStatusChange) {
        _callbacks.OnHDCPStatusChange(ConvertHdcpStatus(status));
    }
}

void dVideoPortAIDLImpl::OnStateChanged(HdmiOutput::State oldState, HdmiOutput::State newState)
{
    DSLOG_INFO("oldState=%d, newState=%d", static_cast<int>(oldState), static_cast<int>(newState));
}
