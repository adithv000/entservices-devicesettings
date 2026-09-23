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
#include "dVideoDeviceAIDLImpl.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/PropertyValue.h>
#include <com/rdk/hal/hdmioutput/Capabilities.h>
#include <com/rdk/hal/hdmioutput/HDROutputMode.h>
#include <com/rdk/hal/hdmioutput/Property.h>
#include <com/rdk/hal/hdmioutput/State.h>
#include <com/rdk/hal/hdmioutput/VIC.h>

#include <cstdlib>

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

// Frame-rate-only view of the VIC table: which VIC to use to keep the same resolution
// family (1080p/2160p) while switching to a different frame rate (FRF-mode style switch).
struct FrameRateVicEntry {
    HdmiOutput::VIC vic;
    VideoResolution family;
    uint32_t frameRateHz;
};

const FrameRateVicEntry kFrameRateTable[] = {
    { HdmiOutput::VIC::VIC32_1920_1080_P_24_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 24 },
    { HdmiOutput::VIC::VIC33_1920_1080_P_25_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 25 },
    { HdmiOutput::VIC::VIC34_1920_1080_P_30_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 30 },
    { HdmiOutput::VIC::VIC31_1920_1080_P_50_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 50 },
    { HdmiOutput::VIC::VIC16_1920_1080_P_60_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 60 },
    { HdmiOutput::VIC::VIC93_3840_2160_P_24_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 24 },
    { HdmiOutput::VIC::VIC94_3840_2160_P_25_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 25 },
    { HdmiOutput::VIC::VIC95_3840_2160_P_30_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 30 },
    { HdmiOutput::VIC::VIC96_3840_2160_P_50_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 50 },
    { HdmiOutput::VIC::VIC97_3840_2160_P_60_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 60 },
};

const FrameRateVicEntry* FindByVic(HdmiOutput::VIC vic)
{
    for (const auto& entry : kFrameRateTable) {
        if (entry.vic == vic) {
            return &entry;
        }
    }
    return nullptr;
}

const FrameRateVicEntry* FindByFamilyAndRate(VideoResolution family, uint32_t frameRateHz)
{
    for (const auto& entry : kFrameRateTable) {
        if (entry.family == family && entry.frameRateHz == frameRateHz) {
            return &entry;
        }
    }
    return nullptr;
}

int32_t ExtractIntProperty(const std::optional<PropertyValue>& value, int32_t defaultValue)
{
    if (!value.has_value() || !value->value.has_value() || value->value->getTag() != PropertyValue::Value::intValue) {
        return defaultValue;
    }
    return value->value->get<PropertyValue::Value::intValue>();
}
}

class dVideoDeviceAIDLImpl::ControllerListener : public HdmiOutput::BnHDMIOutputControllerListener {
public:
    android::binder::Status onHotPlugDetectStateChanged(bool) override { return android::binder::Status::ok(); }
    android::binder::Status onFrameRateChanged() override
    {
        _parent.OnFrameRateChanged();
        return android::binder::Status::ok();
    }
    android::binder::Status onHDCPStatusChanged(HdmiOutput::HDCPStatus, HdmiOutput::HDCPProtocolVersion) override { return android::binder::Status::ok(); }
    android::binder::Status onEDID(const std::vector<uint8_t>&) override { return android::binder::Status::ok(); }

    explicit ControllerListener(dVideoDeviceAIDLImpl& parent)
        : _parent(parent)
    {
    }

private:
    dVideoDeviceAIDLImpl& _parent;
};

class dVideoDeviceAIDLImpl::EventListener : public HdmiOutput::BnHDMIOutputEventListener {
public:
    explicit EventListener(dVideoDeviceAIDLImpl&) { }
    android::binder::Status onStateChanged(HdmiOutput::State, HdmiOutput::State) override { return android::binder::Status::ok(); }
};

dVideoDeviceAIDLImpl::dVideoDeviceAIDLImpl()
    : _opened(false)
    , _cachedZoom(VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL)
    , _forceDisableHDR(true)
{
    InitialiseHAL();
}

dVideoDeviceAIDLImpl::~dVideoDeviceAIDLImpl()
{
    DeInitialiseHAL();
}

android::sp<dVideoDeviceAIDLImpl::Manager> dVideoDeviceAIDLImpl::GetManager()
{
    return GetService<Manager>();
}

bool dVideoDeviceAIDLImpl::IsAvailable()
{
    return GetManager() != nullptr;
}

bool dVideoDeviceAIDLImpl::ResolveDefaultOutput()
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

void dVideoDeviceAIDLImpl::InitialiseHAL()
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
    _output->registerEventListener(_eventListener, &registered);

    if (!_output->open(_controllerListener, &_controller).isOk() || _controller == nullptr) {
        DSLOG_ERR("open() failed on default HDMI output");
        return;
    }
    if (!_controller->start().isOk()) {
        DSLOG_ERR("HDMIOutputController::start() failed");
        return;
    }

    _opened = true;
    DSLOG_INFO("HDMI Output AIDL HAL initialized for VideoDevice (outputId=%d)", _outputId.value);
}

void dVideoDeviceAIDLImpl::DeInitialiseHAL()
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

void dVideoDeviceAIDLImpl::setAllCallbacks(const CallbackBundle& bundle)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    _callbacks = bundle;
}

void dVideoDeviceAIDLImpl::getPersistenceValue()
{
    // Persistence is owned by the AIDL HAL implementation itself.
}

uint32_t dVideoDeviceAIDLImpl::GetVideoDeviceHandle(const int32_t index, int32_t& handle)
{
    if (!_opened || index != 0) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    handle = kDefaultHandle;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::SetVideoDeviceDFC(const int32_t handle, const VideoDeviceZoom zoomSetting)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    DSLOG_WARN("Zoom/DFC (ASPECT_RATIO) is a PlaneControl AIDL property, not exposed by IHDMIOutput; caching value only");
    _cachedZoom = zoomSetting;
    if (_callbacks.OnZoomSettingsChanged) {
        _callbacks.OnZoomSettingsChanged(zoomSetting);
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetVideoDeviceDFC(const int32_t handle, VideoDeviceZoom& zoomSetting)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    zoomSetting = _cachedZoom;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetHDRCapabilities(const int32_t handle, int32_t& capabilities)
{
    if (!IsSupportedHandle(handle)) {
        capabilities = 0x0; // dsHDRSTANDARD_NONE
        return WPEFramework::Core::ERROR_NONE;
    }
    HdmiOutput::Capabilities caps;
    if (!_output->getCapabilities(&caps).isOk()) {
        capabilities = 0x0;
        return WPEFramework::Core::ERROR_NONE;
    }
    int32_t bitmask = 0x0;
    if (!_forceDisableHDR) {
        for (const auto& mode : caps.supportedHDROutputModes) {
            switch (mode) {
            case HdmiOutput::HDROutputMode::HLG:          bitmask |= 0x02; break;
            case HdmiOutput::HDROutputMode::HDR10:        bitmask |= 0x01; break;
            case HdmiOutput::HDROutputMode::HDR10_PLUS:   bitmask |= 0x10; break;
            case HdmiOutput::HDROutputMode::DOLBY_VISION: bitmask |= 0x04; break;
            default: break;
            }
        }
    }
    capabilities = bitmask;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetSupportedVideoCodingFormats(const int32_t handle, int32_t& supportedFormats)
{
    (void)handle;
    // Codec support is a video-decoder capability (IVideoDecoderManager), not exposed by IHDMIOutput.
    supportedFormats = 0x0;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo)
{
    (void)handle;
    (void)videoCodec;
    codecInfo = nullptr;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoDeviceAIDLImpl::DisableHDR(const int32_t handle, const bool disable)
{
    _forceDisableHDR = disable;
    if (IsSupportedHandle(handle) && disable) {
        PropertyValue value;
        value.value = PropertyValue::Value::make<PropertyValue::Value::intValue>(static_cast<int32_t>(HdmiOutput::HDROutputMode::AUTO));
        bool applied = false;
        _controller->setProperty(HdmiOutput::Property::HDR_OUTPUT_MODE, value, &applied);
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::SetFRFMode(const int32_t handle, const int32_t frfmode)
{
    (void)handle;
    (void)frfmode;
    DSLOG_WARN("FRF mode is an IPanelOutput property, not exposed by IHDMIOutput");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoDeviceAIDLImpl::GetFRFMode(const int32_t handle, int32_t& frfmode)
{
    (void)handle;
    frfmode = 0;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoDeviceAIDLImpl::GetCurrentDisplayFrameRate(const int32_t handle, string& framerate)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    std::optional<PropertyValue> value;
    if (!_output->getProperty(HdmiOutput::Property::VIC, &value).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    const auto vic = static_cast<HdmiOutput::VIC>(ExtractIntProperty(value, 0));
    const FrameRateVicEntry* entry = FindByVic(vic);
    framerate = entry != nullptr ? std::to_string(entry->frameRateHz) : "60";
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::SetDisplayFrameRate(const int32_t handle, const string framerate)
{
    if (!IsSupportedHandle(handle)) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    if (framerate.empty()) {
        return WPEFramework::Core::ERROR_BAD_REQUEST;
    }

    const uint32_t requestedRate = static_cast<uint32_t>(std::atoi(framerate.c_str()));
    if (_callbacks.OnDisplayFrameratePreChange) {
        _callbacks.OnDisplayFrameratePreChange(framerate);
    }

    std::optional<PropertyValue> value;
    if (!_output->getProperty(HdmiOutput::Property::VIC, &value).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    const auto currentVic = static_cast<HdmiOutput::VIC>(ExtractIntProperty(value, 0));
    const FrameRateVicEntry* current = FindByVic(currentVic);
    if (current == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    const FrameRateVicEntry* target = FindByFamilyAndRate(current->family, requestedRate);
    if (target == nullptr) {
        DSLOG_ERR("No VIC found for family=%d frameRate=%u", static_cast<int>(current->family), requestedRate);
        return WPEFramework::Core::ERROR_BAD_REQUEST;
    }

    PropertyValue vicValue;
    vicValue.value = PropertyValue::Value::make<PropertyValue::Value::intValue>(static_cast<int32_t>(target->vic));
    bool applied = false;
    const uint32_t result = (_controller->setProperty(HdmiOutput::Property::VIC, vicValue, &applied).isOk() && applied)
        ? WPEFramework::Core::ERROR_NONE
        : WPEFramework::Core::ERROR_GENERAL;

    if (result == WPEFramework::Core::ERROR_NONE && _callbacks.OnDisplayFrameratePostChange) {
        _callbacks.OnDisplayFrameratePostChange(framerate);
    }
    return result;
}

void dVideoDeviceAIDLImpl::OnFrameRateChanged()
{
    string framerate;
    if (GetCurrentDisplayFrameRate(kDefaultHandle, framerate) == WPEFramework::Core::ERROR_NONE
        && _callbacks.OnDisplayFrameratePostChange) {
        _callbacks.OnDisplayFrameratePostChange(framerate);
    }
}
