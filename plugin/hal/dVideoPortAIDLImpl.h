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

#include "dVideoPort.h"

#include <com/rdk/hal/hdmioutput/BnHDMIOutputControllerListener.h>
#include <com/rdk/hal/hdmioutput/BnHDMIOutputEventListener.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutput.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputController.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputManager.h>

#include <atomic>
#include <mutex>

// Implements the VideoPort HAL Platform interface (see dVideoPort.h) on top of the
// com.rdk.hal.hdmioutput AIDL HAL. API mapping follows hdmiout_mapping.csv.
class dVideoPortAIDLImpl : public hal::dVideoPort::IPlatform {
public:
    dVideoPortAIDLImpl();
    ~dVideoPortAIDLImpl() override;

    dVideoPortAIDLImpl(const dVideoPortAIDLImpl&) = delete;
    dVideoPortAIDLImpl& operator=(const dVideoPortAIDLImpl&) = delete;

    static bool IsAvailable();

    void InitialiseHAL() override;
    void DeInitialiseHAL() override;
    void setAllCallbacks(const CallbackBundle& bundle) override;
    void getPersistenceValue() override;

    uint32_t GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t& handle) override;
    uint32_t IsVideoPortEnabled(const int32_t handle, bool& enabled) override;
    uint32_t EnableVideoPort(const int32_t handle, const bool enabled) override;
    uint32_t IsVideoPortDisplayConnected(const int32_t handle, bool& connected) override;
    uint32_t IsVideoPortActive(const int32_t handle, bool& active) override;
    uint32_t GetVideoPortResolution(const int32_t handle, VideoPortResolution& resolution) override;
    uint32_t SetVideoPortResolution(const int32_t handle, const VideoPortResolution resolution, const bool persist, const bool forceCompatibility) override;
    uint32_t getIgnoreEDIDStatus(const int32_t handle, bool& ignoreEDID) override;
    uint32_t GetColorDepth(const int32_t handle, uint32_t& colorDepth) override;
    uint32_t SetVideoPortColorDepth(const int32_t handle, const uint32_t colorDepth) override;
    uint32_t GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange& quantizationRange) override;
    uint32_t SetVideoPortQuantizationRange(const int32_t handle, const VideoPortQuantizationRange quantizationRange) override;
    uint32_t GetColorSpace(const int32_t handle, VideoPortColorSpace& colorSpace) override;
    uint32_t SetColorSpace(const int32_t handle, const VideoPortColorSpace colorSpace) override;
    uint32_t GetVideoPortFrameRate(const int32_t handle, uint32_t& frameRate) override;
    uint32_t SetVideoPortFrameRate(const int32_t handle, const uint32_t frameRate) override;
    uint32_t GetVideoPortHDCPStatus(const int32_t handle, VideoPortHdcpStatus& hdcpStatus) override;
    uint32_t GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion) override;
    uint32_t GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion) override;
    uint32_t GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion) override;
    uint32_t EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t* hdcpKey, const uint16_t hdcpKeySize) override;
    uint32_t IsHDCPEnabledOnVideoPort(const int32_t handle, bool& hdcpEnabled) override;
    uint32_t GetTVHDRCapabilities(const int32_t handle, int32_t& capabilities) override;
    uint32_t GetTVSupportedResolutions(const int32_t handle, int32_t& resolutions) override;
    uint32_t SetForceDisable4K(const int32_t handle, const bool disable) override;
    uint32_t GetForceDisable4K(const int32_t handle, bool& disabled) override;
    uint32_t IsVideoPortOutputHDR(const int32_t handle, bool& isHDR) override;
    uint32_t ResetVideoPortOutputToSDR() override;
    uint32_t GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion) override;
    uint32_t SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion) override;
    uint32_t GetVideoEOTF(const int32_t handle, HDRStandard& hdrStandard) override;
    uint32_t GetMatrixCoefficients(const int32_t handle, DisplayMatrixCoefficients& matrixCoefficients) override;
    uint32_t IsVideoPortDisplaySurround(const int32_t handle, bool& surround) override;
    uint32_t GetVideoPortDisplaySurroundMode(const int32_t handle, VideoPortSurroundMode& surroundMode) override;
    uint32_t GetCurrentOutputSettings(const int32_t handle, DSOutputSettings& outputSettings) override;
    uint32_t SetBackgroundColor(const int32_t handle, const VideoBackgroundColor backgroundColor) override;
    uint32_t SetForceHDRMode(const int32_t handle, const HDRStandard hdrMode) override;
    uint32_t GetColorDepthCapabilities(const int32_t handle, uint32_t& colorDepthCapabilities) override;
    uint32_t GetPreferredColorDepth(const int32_t handle, DisplayColorDepth& colorDepth, const bool persist) override;
    uint32_t SetPreferredColorDepth(const int32_t handle, const DisplayColorDepth colorDepth, const bool persist) override;

private:
    class ControllerListener;
    class EventListener;

    using Manager = com::rdk::hal::hdmioutput::IHDMIOutputManager;
    using Output = com::rdk::hal::hdmioutput::IHDMIOutput;
    using Controller = com::rdk::hal::hdmioutput::IHDMIOutputController;

    static android::sp<Manager> GetManager();
    bool ResolveDefaultOutput();
    // Only the single default HDMI output (handle 0) is exposed by the AIDL HAL.
    bool IsSupportedHandle(const int32_t handle) const { return _opened && handle == kDefaultHandle; }

    void OnHotPlugDetectStateChanged(bool connected);
    void OnFrameRateChanged();
    void OnHDCPStatusChanged(com::rdk::hal::hdmioutput::HDCPStatus status, com::rdk::hal::hdmioutput::HDCPProtocolVersion version);
    void OnStateChanged(com::rdk::hal::hdmioutput::State oldState, com::rdk::hal::hdmioutput::State newState);

    static bool ResolutionNameToVic(const std::string& name, com::rdk::hal::hdmioutput::VIC& vic);
    static bool VicToResolution(com::rdk::hal::hdmioutput::VIC vic, VideoPortResolution& resolution);
    static VideoPortHdcpStatus ConvertHdcpStatus(com::rdk::hal::hdmioutput::HDCPStatus status);
    static VideoPortHdcpProtocolVersion ConvertHdcpVersion(com::rdk::hal::hdmioutput::HDCPProtocolVersion version);
    static com::rdk::hal::hdmioutput::HDCPProtocolVersion ConvertHdcpVersion(const VideoPortHdcpProtocolVersion version);
    static HDRStandard ConvertHdrOutputMode(com::rdk::hal::hdmioutput::HDROutputMode mode);
    static com::rdk::hal::hdmioutput::HDROutputMode ConvertHdrStandard(const HDRStandard hdrStandard);

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
};
