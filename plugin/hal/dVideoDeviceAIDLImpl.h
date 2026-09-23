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

#include "dVideoDevice.h"

#include <com/rdk/hal/hdmioutput/BnHDMIOutputControllerListener.h>
#include <com/rdk/hal/hdmioutput/BnHDMIOutputEventListener.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutput.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputController.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputManager.h>

#include <atomic>
#include <mutex>

// Implements the VideoDevice HAL Platform interface (see dVideoDevice.h) on top of the
// com.rdk.hal.hdmioutput AIDL HAL. Per hdmiout_mapping.csv most dsVideoDevice.c APIs are
// actually backed by IPanelOutput/IVideoDecoderManager (not provided in this task), so only
// the operations that map onto the HDMI Output surface (HDR mode, VIC-encoded frame rate)
// are functionally implemented here; the rest report ERROR_UNAVAILABLE.
class dVideoDeviceAIDLImpl : public hal::dVideoDevice::IPlatform {
public:
    dVideoDeviceAIDLImpl();
    ~dVideoDeviceAIDLImpl() override;

    dVideoDeviceAIDLImpl(const dVideoDeviceAIDLImpl&) = delete;
    dVideoDeviceAIDLImpl& operator=(const dVideoDeviceAIDLImpl&) = delete;

    static bool IsAvailable();

    void InitialiseHAL() override;
    void DeInitialiseHAL() override;
    void setAllCallbacks(const CallbackBundle& bundle) override;
    void getPersistenceValue() override;

    uint32_t GetVideoDeviceHandle(const int32_t index, int32_t& handle) override;
    uint32_t SetVideoDeviceDFC(const int32_t handle, const VideoDeviceZoom zoomSetting) override;
    uint32_t GetVideoDeviceDFC(const int32_t handle, VideoDeviceZoom& zoomSetting) override;
    uint32_t GetHDRCapabilities(const int32_t handle, int32_t& capabilities) override;
    uint32_t GetSupportedVideoCodingFormats(const int32_t handle, int32_t& supportedFormats) override;
    uint32_t GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo) override;
    uint32_t DisableHDR(const int32_t handle, const bool disable) override;
    uint32_t SetFRFMode(const int32_t handle, const int32_t frfmode) override;
    uint32_t GetFRFMode(const int32_t handle, int32_t& frfmode) override;
    uint32_t GetCurrentDisplayFrameRate(const int32_t handle, string& framerate) override;
    uint32_t SetDisplayFrameRate(const int32_t handle, const string framerate) override;

private:
    class ControllerListener;
    class EventListener;

    using Manager = com::rdk::hal::hdmioutput::IHDMIOutputManager;
    using Output = com::rdk::hal::hdmioutput::IHDMIOutput;
    using Controller = com::rdk::hal::hdmioutput::IHDMIOutputController;

    static android::sp<Manager> GetManager();
    bool ResolveDefaultOutput();
    bool IsSupportedHandle(const int32_t handle) const { return _opened && handle == kDefaultHandle; }

    void OnFrameRateChanged();

    static constexpr int32_t kDefaultHandle = 0;

    std::mutex _adminLock;
    CallbackBundle _callbacks;
    android::sp<Manager> _manager;
    Output::Id _outputId;
    android::sp<Output> _output;
    android::sp<Controller> _controller;
    android::sp<ControllerListener> _controllerListener;
    android::sp<EventListener> _eventListener;
    bool _opened;
    VideoDeviceZoom _cachedZoom;
    bool _forceDisableHDR;
};
