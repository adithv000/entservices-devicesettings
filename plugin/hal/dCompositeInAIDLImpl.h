#pragma once

#include "dCompositeIn.h"

#include <com/rdk/hal/compositeinput/BnCompositeInputControllerListener.h>
#include <com/rdk/hal/compositeinput/BnCompositeInputEventListener.h>
#include <com/rdk/hal/compositeinput/ICompositeInputManager.h>
#include <com/rdk/hal/planecontrol/IPlaneControl.h>

#include <mutex>

class dCompositeInAIDLImpl : public hal::dCompositeIn::IPlatform {
public:
    dCompositeInAIDLImpl();
    ~dCompositeInAIDLImpl() override;

    dCompositeInAIDLImpl(const dCompositeInAIDLImpl&) = delete;
    dCompositeInAIDLImpl& operator=(const dCompositeInAIDLImpl&) = delete;

    static bool IsAvailable();

    void InitialiseHAL() override;
    void DeInitialiseHAL() override;
    void setAllCallbacks(const CallbackBundle& bundle) override;
    void getPersistenceValue() override;

    uint32_t GetNrOfCompositeInputs(int32_t& nrCompositeInputs) override;
    uint32_t GetCompositeInStatus(CompositeInStatus& status) override;
    uint32_t SelectCompositeInPort(const CompositeInPort port) override;
    uint32_t ScaleCompositeInVideo(const CompositeInVideoRectangle videoRect) override;

private:
    class ControllerListener;
    class EventListener;

    using Manager = com::rdk::hal::compositeinput::ICompositeInputManager;
    using Port = com::rdk::hal::compositeinput::ICompositeInputPort;
    using Controller = com::rdk::hal::compositeinput::ICompositeInputController;
    using PlaneControl = com::rdk::hal::planecontrol::IPlaneControl;

    static android::sp<Manager> GetManager();
    static android::sp<PlaneControl> GetPlaneControl();
    void StopActivePort();
    void OnConnectionChanged(int32_t portId, bool connected);
    void OnSignalStatusChanged(int32_t portId, com::rdk::hal::compositeinput::SignalStatus signalStatus);
    void OnVideoModeChanged(int32_t portId, const com::rdk::hal::compositeinput::VideoResolution& resolution);
    void OnStateChanged(int32_t portId, com::rdk::hal::compositeinput::State newState);

    std::mutex _adminLock;
    CallbackBundle _callbacks;
    android::sp<Manager> _manager;
    android::sp<Port> _activePort;
    android::sp<Controller> _controller;
    android::sp<ControllerListener> _controllerListener;
    android::sp<EventListener> _eventListener;
    int32_t _activePortId;
};