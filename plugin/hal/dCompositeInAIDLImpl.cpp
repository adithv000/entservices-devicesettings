#include "dCompositeInAIDLImpl.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/PropertyValue.h>
#include <com/rdk/hal/planecontrol/PropertyKVPair.h>
#include <com/rdk/hal/planecontrol/SourcePlaneMapping.h>

#include <cmath>
#include <sstream>
#include <vector>

namespace CompositeInput = com::rdk::hal::compositeinput;
namespace Plane = com::rdk::hal::planecontrol;

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

WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution ConvertResolution(
    const CompositeInput::VideoResolution& resolution)
{
    WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution output {};
    std::ostringstream name;
    name << resolution.pixelWidth << 'x' << resolution.pixelHeight;
    output.name = name.str();
    output.interlaced = resolution.interlaced;
    output.aspectRatio = (resolution.pixelWidth * 3 == resolution.pixelHeight * 4)
        ? DisplayVideoAspectRatio::DS_DISPLAY_ASPECT_RATIO_4X3
        : DisplayVideoAspectRatio::DS_DISPLAY_ASPECT_RATIO_16X9;

    if (resolution.pixelWidth == 720 && resolution.pixelHeight == 480) {
        output.pixelResolution = static_cast<decltype(output.pixelResolution)>(dsVIDEO_PIXELRES_720x480);
    } else if (resolution.pixelWidth == 720 && resolution.pixelHeight == 576) {
        output.pixelResolution = static_cast<decltype(output.pixelResolution)>(dsVIDEO_PIXELRES_720x576);
    } else if (resolution.pixelWidth == 1280 && resolution.pixelHeight == 720) {
        output.pixelResolution = static_cast<decltype(output.pixelResolution)>(dsVIDEO_PIXELRES_1280x720);
    } else if (resolution.pixelWidth == 3840 && resolution.pixelHeight == 2160) {
        output.pixelResolution = static_cast<decltype(output.pixelResolution)>(dsVIDEO_PIXELRES_3840x2160);
    } else {
        output.pixelResolution = static_cast<decltype(output.pixelResolution)>(dsVIDEO_PIXELRES_1920x1080);
    }

    const int32_t frameRate = static_cast<int32_t>(std::lround(resolution.frameRateInHz));
    switch (frameRate) {
    case 24: output.frameRate = static_cast<decltype(output.frameRate)>(dsVIDEO_FRAMERATE_24); break;
    case 25: output.frameRate = static_cast<decltype(output.frameRate)>(dsVIDEO_FRAMERATE_25); break;
    case 30: output.frameRate = static_cast<decltype(output.frameRate)>(dsVIDEO_FRAMERATE_30); break;
    case 50: output.frameRate = static_cast<decltype(output.frameRate)>(dsVIDEO_FRAMERATE_50); break;
    default: output.frameRate = static_cast<decltype(output.frameRate)>(dsVIDEO_FRAMERATE_60); break;
    }
    return output;
}

Plane::PropertyKVPair PlaneProperty(Plane::Property property, int32_t value)
{
    Plane::PropertyKVPair pair;
    pair.property = property;
    pair.propertyValue.value = com::rdk::hal::PropertyValue::Value::make<
        com::rdk::hal::PropertyValue::Value::intValue>(value);
    return pair;
}
}

class dCompositeInAIDLImpl::ControllerListener : public CompositeInput::BnCompositeInputControllerListener {
public:
    ControllerListener(dCompositeInAIDLImpl& parent, int32_t portId)
        : _parent(parent)
        , _portId(portId)
    {
    }

    android::binder::Status onConnectionChanged(bool connected) override
    {
        _parent.OnConnectionChanged(_portId, connected);
        return android::binder::Status::ok();
    }

    android::binder::Status onSignalStatusChanged(CompositeInput::SignalStatus signalStatus) override
    {
        _parent.OnSignalStatusChanged(_portId, signalStatus);
        return android::binder::Status::ok();
    }

    android::binder::Status onVideoModeChanged(const CompositeInput::VideoResolution& resolution) override
    {
        _parent.OnVideoModeChanged(_portId, resolution);
        return android::binder::Status::ok();
    }

private:
    dCompositeInAIDLImpl& _parent;
    int32_t _portId;
};

class dCompositeInAIDLImpl::EventListener : public CompositeInput::BnCompositeInputEventListener {
public:
    EventListener(dCompositeInAIDLImpl& parent, int32_t portId)
        : _parent(parent)
        , _portId(portId)
    {
    }

    android::binder::Status onStateChanged(CompositeInput::State, CompositeInput::State newState) override
    {
        _parent.OnStateChanged(_portId, newState);
        return android::binder::Status::ok();
    }

    android::binder::Status onPropertyChanged(CompositeInput::PortProperty, const com::rdk::hal::PropertyValue&) override
    {
        return android::binder::Status::ok();
    }

private:
    dCompositeInAIDLImpl& _parent;
    int32_t _portId;
};

dCompositeInAIDLImpl::dCompositeInAIDLImpl()
    : _activePortId(-1)
{
    InitialiseHAL();
}

dCompositeInAIDLImpl::~dCompositeInAIDLImpl()
{
    DeInitialiseHAL();
}

android::sp<dCompositeInAIDLImpl::Manager> dCompositeInAIDLImpl::GetManager()
{
    return GetService<Manager>();
}

android::sp<dCompositeInAIDLImpl::PlaneControl> dCompositeInAIDLImpl::GetPlaneControl()
{
    return GetService<PlaneControl>();
}

bool dCompositeInAIDLImpl::IsAvailable()
{
    return GetManager() != nullptr;
}

void dCompositeInAIDLImpl::InitialiseHAL()
{
    std::lock_guard<std::mutex> lock(_adminLock);
    if (_manager == nullptr) {
        _manager = GetManager();
        if (_manager != nullptr) {
            android::ProcessState::self()->startThreadPool();
            DSLOG_INFO("CompositeInput AIDL HAL initialized");
        } else {
            DSLOG_ERR("CompositeInput AIDL service is no longer available");
        }
    }
}

void dCompositeInAIDLImpl::DeInitialiseHAL()
{
    std::lock_guard<std::mutex> lock(_adminLock);
    StopActivePort();
    _manager.clear();
}

void dCompositeInAIDLImpl::setAllCallbacks(const CallbackBundle& bundle)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    _callbacks = bundle;
}

void dCompositeInAIDLImpl::getPersistenceValue()
{
}

uint32_t dCompositeInAIDLImpl::GetNrOfCompositeInputs(int32_t& nrCompositeInputs)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    const android::sp<Manager> manager = _manager != nullptr ? _manager : GetManager();
    std::vector<int32_t> portIds;
    if (manager == nullptr || !manager->getPortIds(&portIds).isOk()) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    nrCompositeInputs = static_cast<int32_t>(portIds.size());
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dCompositeInAIDLImpl::GetCompositeInStatus(CompositeInStatus& status)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    const android::sp<Manager> manager = _manager != nullptr ? _manager : GetManager();
    std::vector<int32_t> portIds;
    if (manager == nullptr || !manager->getPortIds(&portIds).isOk()) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    status.activePort = static_cast<CompositeInPort>(-1);
    status.isPresented = false;
    for (const int32_t portId : portIds) {
        android::sp<Port> port;
        CompositeInput::PortStatus portStatus;
        if (manager->getPort(portId, &port).isOk() && port != nullptr
            && port->getStatus(&portStatus).isOk() && portStatus.active) {
            status.activePort = static_cast<CompositeInPort>(portId);
            status.isPresented = true;
            break;
        }
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dCompositeInAIDLImpl::SelectCompositeInPort(const CompositeInPort port)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    const int32_t portId = static_cast<int32_t>(port);
    if (portId < 0) {
        StopActivePort();
        return WPEFramework::Core::ERROR_NONE;
    }

    const android::sp<Manager> manager = _manager != nullptr ? _manager : GetManager();
    android::sp<Port> selectedPort;
    if (manager == nullptr || !manager->getPort(portId, &selectedPort).isOk() || selectedPort == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    StopActivePort();
    android::sp<ControllerListener> controllerListener = new ControllerListener(*this, portId);
    android::sp<EventListener> eventListener = new EventListener(*this, portId);
    android::sp<Controller> controller;
    if (!selectedPort->registerEventListener(eventListener).isOk()
        || !selectedPort->open(controllerListener, &controller).isOk() || controller == nullptr) {
        selectedPort->unregisterEventListener(eventListener);
        return WPEFramework::Core::ERROR_GENERAL;
    }
    if (!controller->start().isOk()) {
        bool closed = false;
        selectedPort->close(controller, &closed);
        selectedPort->unregisterEventListener(eventListener);
        return WPEFramework::Core::ERROR_GENERAL;
    }

    _activePortId = portId;
    _activePort = selectedPort;
    _controller = controller;
    _controllerListener = controllerListener;
    _eventListener = eventListener;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dCompositeInAIDLImpl::ScaleCompositeInVideo(const CompositeInVideoRectangle videoRect)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    if (_activePortId < 0 || videoRect.width <= 0 || videoRect.height <= 0) {
        return WPEFramework::Core::ERROR_BAD_REQUEST;
    }

    const android::sp<PlaneControl> planeControl = GetPlaneControl();
    std::vector<Plane::SourcePlaneMapping> mappings;
    if (planeControl == nullptr || !planeControl->getVideoSourceDestinationPlaneMapping(&mappings).isOk()) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    int32_t planeIndex = -1;
    for (const auto& mapping : mappings) {
        if (mapping.sourceType == Plane::SourceType::COMPOSITE && mapping.sourceIndex == _activePortId) {
            planeIndex = mapping.destinationPlaneIndex;
            break;
        }
    }
    if (planeIndex < 0) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    const std::vector<Plane::PropertyKVPair> properties = {
        PlaneProperty(Plane::Property::X, videoRect.x),
        PlaneProperty(Plane::Property::Y, videoRect.y),
        PlaneProperty(Plane::Property::WIDTH, videoRect.width),
        PlaneProperty(Plane::Property::HEIGHT, videoRect.height)
    };
    bool applied = false;
    const android::binder::Status result = planeControl->setPropertyMultiAtomic(planeIndex, properties, &applied);
    return result.isOk() && applied ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_GENERAL;
}

void dCompositeInAIDLImpl::StopActivePort()
{
    if (_controller != nullptr) {
        _controller->stop();
    }
    if (_activePort != nullptr) {
        if (_eventListener != nullptr) {
            _activePort->unregisterEventListener(_eventListener);
        }
        if (_controller != nullptr) {
            bool closed = false;
            _activePort->close(_controller, &closed);
        }
    }
    _eventListener.clear();
    _controllerListener.clear();
    _controller.clear();
    _activePort.clear();
    _activePortId = -1;
}

void dCompositeInAIDLImpl::OnConnectionChanged(int32_t portId, bool connected)
{
    if (_callbacks.OnCompositeInHotPlug) {
        _callbacks.OnCompositeInHotPlug(
            static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort>(portId), connected);
    }
}

void dCompositeInAIDLImpl::OnSignalStatusChanged(int32_t portId, CompositeInput::SignalStatus signalStatus)
{
    if (_callbacks.OnCompositeInSignalStatus) {
        _callbacks.OnCompositeInSignalStatus(
            static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort>(portId),
            static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus>(signalStatus));
    }
}

void dCompositeInAIDLImpl::OnVideoModeChanged(int32_t portId, const CompositeInput::VideoResolution& resolution)
{
    if (_callbacks.OnCompositeInVideoModeUpdate) {
        _callbacks.OnCompositeInVideoModeUpdate(
            static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort>(portId),
            ConvertResolution(resolution));
    }
}

void dCompositeInAIDLImpl::OnStateChanged(int32_t portId, CompositeInput::State newState)
{
    if (_callbacks.OnCompositeInStatus
        && (newState == CompositeInput::State::STARTED || newState == CompositeInput::State::READY
            || newState == CompositeInput::State::CLOSED)) {
        _callbacks.OnCompositeInStatus(
            static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort>(portId),
            newState == CompositeInput::State::STARTED);
    }
}