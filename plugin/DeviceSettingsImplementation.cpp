/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
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

#include "DeviceSettingsImplementation.h"
#include "DSController.h"
#include "DeviceSettingsFPDImplementation.h"
#include "DeviceSettingsHdmiInImplementation.h"
#include "DeviceSettingsAudioImplementation.h"
#include "DeviceSettingsHostImplementation.h"
#include "DeviceSettingsHALConfig.h"
#include "DeviceSettingsTelemetry.h"

#include <dlfcn.h>
#include <chrono>

// Definition of the shared global declared in DeviceSettingsTypes.h
profile_t profileType = NOT_FOUND;
#include <syscall.h>

using namespace std;

#define DELEGATE_TO_COMPONENT(component, method, ...) \
    ENTRY_LOG; \
    Core::hresult result = (component != nullptr) ? component->method(__VA_ARGS__) : Core::ERROR_UNAVAILABLE; \
    EXIT_LOG; \
    return result;

// Macro for methods that don't take parameters
#define DELEGATE_TO_COMPONENT_NO_PARAMS(component, method) \
    ENTRY_LOG; \
    Core::hresult result = (component != nullptr) ? component->method() : Core::ERROR_UNAVAILABLE; \
    EXIT_LOG; \
    return result;

namespace WPEFramework {
namespace Plugin {

    namespace DeviceSettingsHALLoader {
        void* gLibraryHandle = nullptr;
        std::mutex gLibraryLock;

        void* ResolveSymbol(const std::string& libName, const std::string& symbolName)
        {
            std::lock_guard<std::mutex> guard(gLibraryLock);

            if (gLibraryHandle == nullptr) {
                gLibraryHandle = dlopen(libName.c_str(), RTLD_LAZY);
                if (gLibraryHandle == nullptr) {
                    DSLOG_ERR("dlopen failed for %s: %s", libName.c_str(), dlerror());
                    return nullptr;
                }
            }

            void* symbol = dlsym(gLibraryHandle, symbolName.c_str());
            if (symbol == nullptr) {
                DSLOG_ERR("dlsym failed for %s: %s", symbolName.c_str(), dlerror());
            }

            return symbol;
        }

        void ReleaseAllLibraries()
        {
            std::lock_guard<std::mutex> guard(gLibraryLock);

            if (gLibraryHandle != nullptr) {
                dlclose(gLibraryHandle);
                gLibraryHandle = nullptr;
            }
        }
    }

    SERVICE_REGISTRATION(DeviceSettingsImp, 1, 0);

    DeviceSettingsImp* DeviceSettingsImp::_instance = nullptr;

    DeviceSettingsImp::DeviceSettingsImp()
        : _dsController(nullptr)
        , _fpdSettings(nullptr)
        , _hdmiInSettings(nullptr)
        , _audioSettings(nullptr)
        , _videoPortSettings(nullptr)
        , _videoDeviceSettings(nullptr)
        , _hostSettings(nullptr)
        , _displaySettings(nullptr)
        , _compositeInSettings(nullptr)
        , mConnectionId(0)
    {
        // Set the static instance for backward compatibility (if still needed)
        DeviceSettingsImp::_instance = this;

        // Matches iarmmgrs dsMgr.c DSMgr_Start(): TELEMETRY_INIT(IARM_BUS_DSMGR_NAME)
        TELEMETRY_INIT("DSMgr");

        // Initialize profile type only — Start() is deferred to Configure()
        // to avoid blocking the WPEFramework plugin activation thread.
        profileType = searchRdkProfile();
        DSLOG_INFO("Initialized profileType: %d (0=STB, 1=TV)", profileType);

        // ── Per-component creation timing ─────────────────────────────────────
        using Clock = std::chrono::steady_clock;
        using Ms    = std::chrono::milliseconds;
        auto tTotal = Clock::now();
        auto t0     = tTotal;

#define DS_TIME_COMPONENT(label, expr) \
        t0 = Clock::now(); \
        expr; \
        DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", label, \
                (long long)std::chrono::duration_cast<Ms>(Clock::now() - t0).count())

        DS_TIME_COMPONENT("DSController::Create", _dsController = DSController::Create(this));

        DSLOG_INFO("[DS-INIT-TIMING] Serial component creation - begin");
        DS_TIME_COMPONENT("Host::Create", _hostSettings = DeviceSettingsHostImpl::Create());
        DS_TIME_COMPONENT("Display::Create", _displaySettings = DeviceSettingsDisplayImpl::Create());
        DS_TIME_COMPONENT("Audio::Create", _audioSettings = DeviceSettingsAudioImpl::Create());
        DS_TIME_COMPONENT("VideoPort::Create", _videoPortSettings = DeviceSettingsVideoPortImpl::Create());
        DS_TIME_COMPONENT("VideoDevice::Create", _videoDeviceSettings = DeviceSettingsVideoDeviceImpl::Create());
        DS_TIME_COMPONENT("FPD::Create", _fpdSettings = DeviceSettingsFPDImpl::Create());
        DS_TIME_COMPONENT("HdmiIn::Create", _hdmiInSettings = DeviceSettingsHdmiInImp::Create());
        DS_TIME_COMPONENT("CompositeIn::Create", _compositeInSettings = DeviceSettingsCompositeInImpl::Create());

#undef DS_TIME_COMPONENT

        DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms",
                "DeviceSettingsImp ctor TOTAL",
                (long long)std::chrono::duration_cast<Ms>(Clock::now() - tTotal).count());
    }

    DeviceSettingsImp::~DeviceSettingsImp() {
        DSLOG_INFO("Destructor - Instance Address: %p", this);
        
        // Clean up created implementation instances
        if (_fpdSettings != nullptr) {
            delete _fpdSettings;
            _fpdSettings = nullptr;
        }
        
        if (_hdmiInSettings != nullptr) {
            delete _hdmiInSettings;
            _hdmiInSettings = nullptr;
        }
        
        if (_audioSettings != nullptr) {
            delete _audioSettings;
            _audioSettings = nullptr;
        }
        
        if (_videoPortSettings != nullptr) {
            delete _videoPortSettings;
            _videoPortSettings = nullptr;
        }
        if (_videoDeviceSettings != nullptr) {
            delete _videoDeviceSettings;
            _videoDeviceSettings = nullptr;
        }
        if (_hostSettings != nullptr) {
            delete _hostSettings;
            _hostSettings = nullptr;
        }
        if (_displaySettings != nullptr) {
            delete _displaySettings;
            _displaySettings = nullptr;
        }
        if (_compositeInSettings != nullptr) {
            delete _compositeInSettings;
            _compositeInSettings = nullptr;
        }
        
        // Clean up DSController last as it provides system infrastructure
        if (_dsController != nullptr) {
            delete _dsController;
            _dsController = nullptr;
        }

        DeviceSettingsHALLoader::ReleaseAllLibraries();
        DSLOG_INFO("Destructor - Released all HAL libraries");

        // Matches iarmmgrs dsMgr.c DSMgr_Stop(): TELEMETRY_UNINIT()
        TELEMETRY_UNINIT();
    }

    Core::hresult DeviceSettingsImp::Configure(PluginHost::IShell* service)
    {
        DSLOG_INFO("DeviceSettingsImp Configure called with service: %p", service);

        using Clock = std::chrono::steady_clock;
        using Ms    = std::chrono::milliseconds;
        auto tCfg   = Clock::now();

        if (service == nullptr) {
            DSLOG_ERR("Service parameter is null");
            return Core::ERROR_BAD_REQUEST;
        }

        if (_dsController != nullptr) {
            DSLOG_INFO("[DS-INIT-TIMING] DSController::Start — begin");
            auto t0 = Clock::now();
            _dsController->Start();
            DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "DSController::Start",
                    (long long)std::chrono::duration_cast<Ms>(Clock::now() - t0).count());
        } else {
            DSLOG_ERR("DSController is null - cannot start");
            return Core::ERROR_GENERAL;
        }

        // Initialize DSController power event listener with the service
        if (_dsController != nullptr) {
            DSLOG_INFO("[DS-INIT-TIMING] InitializePowerEventListener — begin");
            auto t0 = Clock::now();
            _dsController->InitializePowerEventListener(service);
            DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "InitializePowerEventListener",
                    (long long)std::chrono::duration_cast<Ms>(Clock::now() - t0).count());
        } else {
            DSLOG_ERR("DSController is null - cannot initialize power event listener");
        }

        // HAL initialisation is now done inside each HAL impl constructor as part of
        // the two-stage parallel component Create() calls in DeviceSettingsImp().
        // No separate InitialiseHAL() pass is needed here.

        DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "Configure TOTAL",
                (long long)std::chrono::duration_cast<Ms>(Clock::now() - tCfg).count());
        DSLOG_INFO("DeviceSettingsImp configured successfully");
        return Core::ERROR_NONE;
    }

    // ============================================================================
    // IDeviceSettingsFPD interface implementation - delegate to _fpdSettings interface
    // ============================================================================
    
    Core::hresult DeviceSettingsImp::Register(const string clientName, Exchange::IDeviceSettingsFPD::INotification* notification) {
        Core::hresult result;
        if (_fpdSettings != nullptr) {
            result = _fpdSettings->Register(clientName, notification);
            DSLOG_INFO("FPD Register: SUCCESS - forwarded to implementation");
        } else {
            DSLOG_ERR("FPD Register: FAILED - _fpdSettings is null");
            result = Core::ERROR_UNAVAILABLE;
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::Unregister(Exchange::IDeviceSettingsFPD::INotification* notification) {
        DELEGATE_TO_COMPONENT(_fpdSettings, Unregister, notification)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDTime(const FPDTimeFormat timeFormat, const uint32_t minutes, const uint32_t seconds) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDTime, timeFormat, minutes, seconds)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDScroll(const uint32_t scrollHoldDuration, const uint32_t nHorizontalScrollIterations, const uint32_t nVerticalScrollIterations) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDScroll, scrollHoldDuration, nHorizontalScrollIterations, nVerticalScrollIterations)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDBlink(const FPDIndicator indicator, const uint32_t blinkDuration, const uint32_t blinkIterations) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDBlink, indicator, blinkDuration, blinkIterations)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDBrightness(const FPDIndicator indicator, const uint32_t brightNess, const bool persist) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDBrightness, indicator, brightNess, persist)
    }
    
    Core::hresult DeviceSettingsImp::GetFPDBrightness(const FPDIndicator indicator, uint32_t &brightNess, const bool persist) {
        DELEGATE_TO_COMPONENT(_fpdSettings, GetFPDBrightness, indicator, brightNess, persist)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDState(const FPDIndicator indicator, const FPDState state) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDState, indicator, state)
    }
    
    Core::hresult DeviceSettingsImp::GetFPDState(const FPDIndicator indicator, FPDState &state) {
        DELEGATE_TO_COMPONENT(_fpdSettings, GetFPDState, indicator, state)
    }
    
    Core::hresult DeviceSettingsImp::GetFPDColor(const FPDIndicator indicator, uint32_t &color) {
        DELEGATE_TO_COMPONENT(_fpdSettings, GetFPDColor, indicator, color)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDColor(const FPDIndicator indicator, const uint32_t color) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDColor, indicator, color)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDTextBrightness(const FPDTextDisplay textDisplay, const uint32_t brightNess) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDTextBrightness, textDisplay, brightNess)
    }
    
    Core::hresult DeviceSettingsImp::GetFPDTextBrightness(const FPDTextDisplay textDisplay, uint32_t &brightNess) {
        DELEGATE_TO_COMPONENT(_fpdSettings, GetFPDTextBrightness, textDisplay, brightNess)
    }
    
    Core::hresult DeviceSettingsImp::EnableFPDClockDisplay(const bool enable) {
        DELEGATE_TO_COMPONENT(_fpdSettings, EnableFPDClockDisplay, enable)
    }
    
    Core::hresult DeviceSettingsImp::GetFPDTimeFormat(FPDTimeFormat &fpdTimeFormat) {
        DELEGATE_TO_COMPONENT(_fpdSettings, GetFPDTimeFormat, fpdTimeFormat)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDTimeFormat(const FPDTimeFormat fpdTimeFormat) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDTimeFormat, fpdTimeFormat)
    }
    
    Core::hresult DeviceSettingsImp::SetFPDMode(const FPDMode fpdMode) {
        DELEGATE_TO_COMPONENT(_fpdSettings, SetFPDMode, fpdMode)
    }

    // ============================================================================
    // IDeviceSettingsHDMIIn interface implementation - delegate to _hdmiInSettings interface
    // ============================================================================
    
    Core::hresult DeviceSettingsImp::Register(const string clientName, Exchange::IDeviceSettingsHDMIIn::INotification* notification) {
        Core::hresult result;
        if (_hdmiInSettings != nullptr) {
            result = _hdmiInSettings->Register(clientName, notification);
            DSLOG_INFO("HDMIIn Register: SUCCESS - forwarded to implementation");
        } else {
            DSLOG_ERR("HDMIIn Register: FAILED - _hdmiInSettings is null");
            result = Core::ERROR_UNAVAILABLE;
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::Unregister(Exchange::IDeviceSettingsHDMIIn::INotification* notification) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, Unregister, notification)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIInNumberOfInputs(int32_t &count) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMIInNumberOfInputs, count)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIInStatus(HDMIInStatus &hdmiStatus, IHDMIInPortConnectionStatusIterator*& portConnectionStatus) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMIInStatus, hdmiStatus, portConnectionStatus)
    }
    
    Core::hresult DeviceSettingsImp::SelectHDMIInPort(const HDMIInPort port, const bool requestAudioMix, const bool topMostPlane, const HDMIVideoPlaneType videoPlaneType) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, SelectHDMIInPort, port, requestAudioMix, topMostPlane, videoPlaneType)
    }
    
    Core::hresult DeviceSettingsImp::ScaleHDMIInVideo(const HDMIInVideoRectangle videoPosition) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, ScaleHDMIInVideo, videoPosition)
    }
    
    Core::hresult DeviceSettingsImp::SelectHDMIZoomMode(const HDMIInVideoZoom zoomMode) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, SelectHDMIZoomMode, zoomMode)
    }
    
    Core::hresult DeviceSettingsImp::GetSupportedGameFeaturesList(IHDMIInGameFeatureListIterator *& gameFeatureList) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetSupportedGameFeaturesList, gameFeatureList)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIInAVLatency(uint32_t &videoLatency, uint32_t &audioLatency) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMIInAVLatency, videoLatency, audioLatency)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIInAllmStatus(const HDMIInPort port, bool &allmStatus) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMIInAllmStatus, port, allmStatus)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIInEdid2AllmSupport(const HDMIInPort port, bool &allmSupport) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMIInEdid2AllmSupport, port, allmSupport)
    }
    
    Core::hresult DeviceSettingsImp::SetHDMIInEdid2AllmSupport(const HDMIInPort port, bool allmSupport) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, SetHDMIInEdid2AllmSupport, port, allmSupport)
    }
    
    Core::hresult DeviceSettingsImp::GetEdidBytes(const HDMIInPort port, const uint16_t edidBytesLength, uint8_t edidBytes[]) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetEdidBytes, port, edidBytesLength, edidBytes)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMISPDInformation(const HDMIInPort port, const uint16_t spdBytesLength, uint8_t spdBytes[]) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMISPDInformation, port, spdBytesLength, spdBytes)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIEdidVersion(const HDMIInPort port, HDMIInEdidVersion &edidVersion) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMIEdidVersion, port, edidVersion)
    }
    
    Core::hresult DeviceSettingsImp::SetHDMIEdidVersion(const HDMIInPort port, const HDMIInEdidVersion edidVersion) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, SetHDMIEdidVersion, port, edidVersion)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIVideoMode(HDMIVideoPortResolution &videoPortResolution) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMIVideoMode, videoPortResolution)
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIVersion(const HDMIInPort port, HDMIInCapabilityVersion &capabilityVersion) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetHDMIVersion, port, capabilityVersion)
    }
    
    Core::hresult DeviceSettingsImp::SetVRRSupport(const HDMIInPort port, const bool vrrSupport) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, SetVRRSupport, port, vrrSupport)
    }
    
    Core::hresult DeviceSettingsImp::GetVRRSupport(const HDMIInPort port, bool &vrrSupport) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetVRRSupport, port, vrrSupport)
    }
    
    Core::hresult DeviceSettingsImp::GetVRRStatus(const HDMIInPort port, HDMIInVRRStatus &vrrStatus) {
        DELEGATE_TO_COMPONENT(_hdmiInSettings, GetVRRStatus, port, vrrStatus)
    }

    // ============================================================================
    // IDeviceSettingsAudio interface implementation - delegate to _audioSettings interface
    // ============================================================================
    
    Core::hresult DeviceSettingsImp::Register(const string clientName, Exchange::IDeviceSettingsAudio::INotification* notification) {
        DELEGATE_TO_COMPONENT(_audioSettings, Register, clientName, notification)
    }
    
    Core::hresult DeviceSettingsImp::Unregister(Exchange::IDeviceSettingsAudio::INotification* notification) {
        DELEGATE_TO_COMPONENT(_audioSettings, Unregister, notification)
    }

    Core::hresult DeviceSettingsImp::GetAudioPort(const AudioPortType type, const int32_t index, int32_t &handle) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioPort, type, index, handle)
    }

    Core::hresult DeviceSettingsImp::GetAudioCapabilities(const int32_t handle, int32_t &capabilities) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioCapabilities, handle, capabilities)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioMS12Capabilities(const int32_t handle, int32_t &capabilities) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioMS12Capabilities, handle, capabilities)
    }

    Core::hresult DeviceSettingsImp::GetMS12Capabilities(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetMS12Capabilities, handle, compressions)
    }

    Core::hresult DeviceSettingsImp::GetAudioFormat(const int32_t handle, AudioFormat &audioFormat) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioFormat, handle, audioFormat)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioEncoding(const int32_t handle, AudioEncoding &encoding) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioEncoding, handle, encoding)
    }
    
    Core::hresult DeviceSettingsImp::GetSupportedCompressions(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetSupportedCompressions, handle, compressions)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioCompression(const int32_t handle, AudioCompression &compression) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioCompression, handle, compression)
    }

    Core::hresult DeviceSettingsImp::SetAudioCompression(const int32_t handle, const AudioCompression compression) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioCompression, handle, compression)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioLevel(const int32_t handle, const float audioLevel) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioLevel, handle, audioLevel)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioLevel(const int32_t handle, float &audioLevel) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioLevel, handle, audioLevel)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioGain(const int32_t handle, const float gainLevel) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioGain, handle, gainLevel)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioGain(const int32_t handle, float &gainLevel) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioGain, handle, gainLevel)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioMute(const int32_t handle, const bool mute) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioMute, handle, mute)
    }
    
    Core::hresult DeviceSettingsImp::IsAudioMuted(const int32_t handle, bool &muted) {
        DELEGATE_TO_COMPONENT(_audioSettings, IsAudioMuted, handle, muted)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioDucking(const int32_t handle, const AudioDuckingType duckingType, const AudioDuckingAction duckingAction, const uint8_t level) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioDucking, handle, duckingType, duckingAction, level)
    }
    
    Core::hresult DeviceSettingsImp::GetStereoMode(const int32_t handle, AudioStereoMode &mode, const bool persist) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetStereoMode, handle, mode, persist)
    }
    
    Core::hresult DeviceSettingsImp::SetStereoMode(const int32_t handle, const AudioStereoMode mode, const bool persist) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetStereoMode, handle, mode, persist)
    }
    
    Core::hresult DeviceSettingsImp::SetAssociatedAudioMixing(const int32_t handle, const bool mixing) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAssociatedAudioMixing, handle, mixing)
    }
    
    Core::hresult DeviceSettingsImp::GetAssociatedAudioMixing(const int32_t handle, bool &mixing) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAssociatedAudioMixing, handle, mixing)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioFaderControl(const int32_t handle, const int32_t mixerBalance) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioFaderControl, handle, mixerBalance)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioFaderControl(const int32_t handle, int32_t &mixerBalance) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioFaderControl, handle, mixerBalance)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioPrimaryLanguage(const int32_t handle, const string& primaryAudioLanguage) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioPrimaryLanguage, handle, primaryAudioLanguage)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioPrimaryLanguage(const int32_t handle, string &primaryAudioLanguage) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioPrimaryLanguage, handle, primaryAudioLanguage)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioSecondaryLanguage(const int32_t handle, const string& secondaryAudioLanguage) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioSecondaryLanguage, handle, secondaryAudioLanguage)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioSecondaryLanguage(const int32_t handle, string &secondaryAudioLanguage) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioSecondaryLanguage, handle, secondaryAudioLanguage)
    }
    
    Core::hresult DeviceSettingsImp::IsAudioOutputConnected(const int32_t handle, bool &isConnected) {
        DELEGATE_TO_COMPONENT(_audioSettings, IsAudioOutputConnected, handle, isConnected)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioSinkDeviceAtmosCapability(const int32_t handle, DolbyAtmosCapability &atmosCapability) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioSinkDeviceAtmosCapability, handle, atmosCapability)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioAtmosOutputMode(const int32_t handle, const bool enable) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioAtmosOutputMode, handle, enable)
    }

    // Missing Audio interface delegation methods
    Core::hresult DeviceSettingsImp::IsAudioPortEnabled(const int32_t handle, bool &enabled) {
        DELEGATE_TO_COMPONENT(_audioSettings, IsAudioPortEnabled, handle, enabled)
    }

    Core::hresult DeviceSettingsImp::EnableAudioPort(const int32_t handle, const bool enable) {
        DELEGATE_TO_COMPONENT(_audioSettings, EnableAudioPort, handle, enable)
    }

    Core::hresult DeviceSettingsImp::GetSupportedARCTypes(const int32_t handle, int32_t &types) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetSupportedARCTypes, handle, types)
    }

    Core::hresult DeviceSettingsImp::SetSAD(const int32_t handle, const uint8_t sadList[], const uint8_t count) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetSAD, handle, sadList, count)
    }
    
    Core::hresult DeviceSettingsImp::EnableARC(const int32_t handle, const AudioARCStatus arcStatus) {
        DELEGATE_TO_COMPONENT(_audioSettings, EnableARC, handle, arcStatus)
    }
    
    Core::hresult DeviceSettingsImp::GetStereoAuto(const int32_t handle, int32_t &mode) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetStereoAuto, handle, mode)
    }
    
    Core::hresult DeviceSettingsImp::SetStereoAuto(const int32_t handle, const int32_t mode, const bool persist) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetStereoAuto, handle, mode, persist)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioEnablePersist(const int32_t handle, bool &enabled, string &portName) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioEnablePersist, handle, enabled, portName)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioEnablePersist(const int32_t handle, const bool enable, const string& portName) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioEnablePersist, handle, enable, portName)
    }
    
    Core::hresult DeviceSettingsImp::IsAudioMSDecoded(const int32_t handle, bool &hasms11Decode) {
        DELEGATE_TO_COMPONENT(_audioSettings, IsAudioMSDecoded, handle, hasms11Decode)
    }
    
    Core::hresult DeviceSettingsImp::IsAudioMS12Decoded(const int32_t handle, bool &hasms12Decode) {
        DELEGATE_TO_COMPONENT(_audioSettings, IsAudioMS12Decoded, handle, hasms12Decode)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioLEConfig(const int32_t handle, bool &enabled) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioLEConfig, handle, enabled)
    }
    
    Core::hresult DeviceSettingsImp::EnableAudioLEConfig(const int32_t handle, const bool enable) {
        DELEGATE_TO_COMPONENT(_audioSettings, EnableAudioLEConfig, handle, enable)
    }

    Core::hresult DeviceSettingsImp::SetApplicationAudioConfig(const int32_t handle, const string& audioConfig, const bool enable) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetApplicationAudioConfig, handle, audioConfig, enable)
    }

    Core::hresult DeviceSettingsImp::GetApplicationAudioConfig(const int32_t handle, const string& audioConfig, bool& enabled) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetApplicationAudioConfig, handle, audioConfig, enabled)
    }

    Core::hresult DeviceSettingsImp::GetApplicationAudioConfigList(const int32_t handle, IDeviceSettingsAudioApplicationConfigIterator*& configList) const {
        DELEGATE_TO_COMPONENT(_audioSettings, GetApplicationAudioConfigList, handle, configList)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioDelay(const int32_t handle, const uint32_t audioDelay) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioDelay, handle, audioDelay)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioDelay(const int32_t handle, uint32_t &audioDelay) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioDelay, handle, audioDelay)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioDelayOffset(const int32_t handle, const uint32_t delayOffset) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioDelayOffset, handle, delayOffset)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioDelayOffset(const int32_t handle, uint32_t &delayOffset) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioDelayOffset, handle, delayOffset)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioCompression(const int32_t handle, const int32_t compressionLevel) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioCompression, handle, compressionLevel)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioCompression(const int32_t handle, int32_t &compressionLevel) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioCompression, handle, compressionLevel)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioDialogEnhancement(const int32_t handle, const int32_t level) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioDialogEnhancement, handle, level)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioDialogEnhancement(const int32_t handle, int32_t &level) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioDialogEnhancement, handle, level)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioDolbyVolumeMode(const int32_t handle, const bool enable) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioDolbyVolumeMode, handle, enable)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioDolbyVolumeMode(const int32_t handle, bool &enabled) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioDolbyVolumeMode, handle, enabled)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioIntelligentEqualizerMode(const int32_t handle, const int32_t mode) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioIntelligentEqualizerMode, handle, mode)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioIntelligentEqualizerMode(const int32_t handle, int32_t &mode) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioIntelligentEqualizerMode, handle, mode)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioVolumeLeveller(const int32_t handle, const VolumeLeveller volumeLeveller) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioVolumeLeveller, handle, volumeLeveller)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioVolumeLeveller(const int32_t handle, VolumeLeveller &volumeLeveller) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioVolumeLeveller, handle, volumeLeveller)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioBassEnhancer(const int32_t handle, const int32_t boost) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioBassEnhancer, handle, boost)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioBassEnhancer(const int32_t handle, int32_t &boost) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioBassEnhancer, handle, boost)
    }
    
    Core::hresult DeviceSettingsImp::EnableAudioSurroundDecoder(const int32_t handle, const bool enable) {
        DELEGATE_TO_COMPONENT(_audioSettings, EnableAudioSurroundDecoder, handle, enable)
    }
    
    Core::hresult DeviceSettingsImp::IsAudioSurroundDecoderEnabled(const int32_t handle, bool &enabled) {
        DELEGATE_TO_COMPONENT(_audioSettings, IsAudioSurroundDecoderEnabled, handle, enabled)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioDRCMode(const int32_t handle, const int32_t drcMode) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioDRCMode, handle, drcMode)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioDRCMode(const int32_t handle, int32_t &drcMode) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioDRCMode, handle, drcMode)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioSurroundVirtualizer(const int32_t handle, const SurroundVirtualizer surroundVirtualizer) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioSurroundVirtualizer, handle, surroundVirtualizer)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioSurroundVirtualizer(const int32_t handle, SurroundVirtualizer &surroundVirtualizer) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioSurroundVirtualizer, handle, surroundVirtualizer)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioMISteering(const int32_t handle, const bool enable) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioMISteering, handle, enable)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioMISteering(const int32_t handle, bool &enable) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioMISteering, handle, enable)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioGraphicEqualizerMode(const int32_t handle, const int32_t mode) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioGraphicEqualizerMode, handle, mode)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioGraphicEqualizerMode(const int32_t handle, int32_t &mode) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioGraphicEqualizerMode, handle, mode)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioMS12ProfileList(const int32_t handle, IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) const {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioMS12ProfileList, handle, ms12ProfileList)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioMS12Profile(const int32_t handle, string &profile) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioMS12Profile, handle, profile)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioMS12Profile(const int32_t handle, const string& profile) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioMS12Profile, handle, profile)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioMixerLevels(const int32_t handle, const AudioInput audioInput, const int32_t volume) {
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioMixerLevels, handle, audioInput, volume)
    }
    
    Core::hresult DeviceSettingsImp::SetAudioMS12SettingsOverride(const int32_t handle, const string& profileName, const string& profileSettingsName, const string& profileSettingValue, const AudioMS12ProfileState profileState) {
        // Pass the enum directly; DeviceSettingsAudioImpl converts it to "ADD"/"REMOVE" internally.
        DELEGATE_TO_COMPONENT(_audioSettings, SetAudioMS12SettingsOverride, handle, profileName, profileSettingsName, profileSettingValue, profileState)
    }
    
    Core::hresult DeviceSettingsImp::ResetAudioDialogEnhancement(const int32_t handle) {
        DELEGATE_TO_COMPONENT(_audioSettings, ResetAudioDialogEnhancement, handle)
    }
    
    Core::hresult DeviceSettingsImp::ResetAudioBassEnhancer(const int32_t handle) {
        DELEGATE_TO_COMPONENT(_audioSettings, ResetAudioBassEnhancer, handle)
    }
    
    Core::hresult DeviceSettingsImp::ResetAudioSurroundVirtualizer(const int32_t handle) {
        DELEGATE_TO_COMPONENT(_audioSettings, ResetAudioSurroundVirtualizer, handle)
    }
    
    Core::hresult DeviceSettingsImp::ResetAudioVolumeLeveller(const int32_t handle) {
        DELEGATE_TO_COMPONENT(_audioSettings, ResetAudioVolumeLeveller, handle)
    }
    
    Core::hresult DeviceSettingsImp::GetAudioHDMIARCPortId(const int32_t handle, int32_t &portId) {
        DELEGATE_TO_COMPONENT(_audioSettings, GetAudioHDMIARCPortId, handle, portId)
    }

    // ============================================================================
    // IDeviceSettingsVideoPort interface implementation - delegate to _videoPortSettings interface
    // ============================================================================
    
    Core::hresult DeviceSettingsImp::Register(const string clientName, Exchange::IDeviceSettingsVideoPort::INotification* notification) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, Register, clientName, notification)
    }
    
    Core::hresult DeviceSettingsImp::Unregister(Exchange::IDeviceSettingsVideoPort::INotification* notification) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, Unregister, notification)
    }
    
    Core::hresult DeviceSettingsImp::GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t &handle) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetVideoPort, videoPort, index, handle)
    }

    Core::hresult DeviceSettingsImp::IsVideoPortEnabled(const int32_t handle, bool &enabled) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, IsVideoPortEnabled, handle, enabled)
    }

    Core::hresult DeviceSettingsImp::GetVideoPortResolutionConfig(VideoPortType videoPortType,
                                                                  IVideoPortResolutionIterator*& videoPortResolutions) const {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetVideoPortResolutionConfig, videoPortType, videoPortResolutions)
    }

    Core::hresult DeviceSettingsImp::EnableVideoPort(const int32_t handle, const bool enabled) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, EnableVideoPort, handle, enabled)
    }
    
    Core::hresult DeviceSettingsImp::IsVideoPortDisplayConnected(const int32_t handle, bool &connected) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, IsVideoPortDisplayConnected, handle, connected)
    }
    
    Core::hresult DeviceSettingsImp::IsVideoPortActive(const int32_t handle, bool &active) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, IsVideoPortActive, handle, active)
    }
    
    Core::hresult DeviceSettingsImp::GetVideoPortResolution(const int32_t handle, VideoPortResolution &resolution) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetVideoPortResolution, handle, resolution)
    }
    
    Core::hresult DeviceSettingsImp::getIgnoreEDIDStatus(const int32_t handle, bool &ignoreEDID) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, getIgnoreEDIDStatus, handle, ignoreEDID)
    }

    Core::hresult DeviceSettingsImp::GetColorDepth(const int32_t handle, uint32_t &colorDepth) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetColorDepth, handle, colorDepth)
    }
    
    Core::hresult DeviceSettingsImp::GetColorSpace(const int32_t handle, VideoPortColorSpace &colorSpace) {
        VideoPortColorSpace internalColorSpace;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetColorSpace(handle, internalColorSpace) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            colorSpace = static_cast<VideoPortColorSpace>(internalColorSpace);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange &quantizationRange) {
        VideoPortQuantizationRange internalRange;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetQuantizationRange(handle, internalRange) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            quantizationRange = static_cast<VideoPortQuantizationRange>(internalRange);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::GetHDCPStatusOnVideoPort(const int32_t handle, Exchange::IDeviceSettingsVideoPort::HDCPStatus &hdcpStatus) {
        VideoPortHdcpStatus internalStatus;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetVideoPortHDCPStatus(handle, internalStatus) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            hdcpStatus = static_cast<Exchange::IDeviceSettingsVideoPort::HDCPStatus>(internalStatus);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) {
        VideoPortHdcpProtocolVersion internalVersion;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetHDCPProtocolVersionOnVideoPort(handle, internalVersion) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            hdcpVersion = static_cast<VideoPortHdcpProtocolVersion>(internalVersion);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) {
        VideoPortHdcpProtocolVersion internalVersion;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetHDCPReceiverProtocolVersionOnVideoPort(handle, internalVersion) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            hdcpVersion = static_cast<VideoPortHdcpProtocolVersion>(internalVersion);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) {
        VideoPortHdcpProtocolVersion internalVersion;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetHDCPCurrentProtocolVersionOnVideoPort(handle, internalVersion) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            hdcpVersion = static_cast<VideoPortHdcpProtocolVersion>(internalVersion);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::IsVideoPortDisplaySurround(const int32_t handle, bool &surround) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, IsVideoPortDisplaySurround, handle, surround)
    }
    
    Core::hresult DeviceSettingsImp::GetVideoPortDisplaySurroundMode(const int32_t handle, Exchange::IDeviceSettingsVideoPort::VideoPortSurroundMode &surroundMode) {
        VideoPortSurroundMode internalSurroundMode;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetVideoPortDisplaySurroundMode(handle, internalSurroundMode) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            surroundMode = static_cast<Exchange::IDeviceSettingsVideoPort::VideoPortSurroundMode>(internalSurroundMode);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::SetVideoPortResolution(const int32_t handle, const VideoPortResolution& videoPortResolution, const bool persist, const bool forceCompatibility) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, SetVideoPortResolution, handle, videoPortResolution, persist, forceCompatibility)
    }
    
    Core::hresult DeviceSettingsImp::EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t hdcpKey[], const uint16_t hdcpKeySize) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, EnableHDCPOnVideoPort, handle, hdcpEnable, hdcpKey, hdcpKeySize)
    }
    
    Core::hresult DeviceSettingsImp::IsHDCPEnabledOnVideoPort(const int32_t handle, bool &hdcpEnabled) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, IsHDCPEnabledOnVideoPort, handle, hdcpEnabled)
    }
    
    Core::hresult DeviceSettingsImp::GetTVHDRCapabilities(const int32_t handle, int32_t &capabilities) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetTVHDRCapabilities, handle, capabilities)
    }
    
    Core::hresult DeviceSettingsImp::GetTVSupportedResolutions(const int32_t handle, int32_t &resolutions) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetTVSupportedResolutions, handle, resolutions)
    }
    
    Core::hresult DeviceSettingsImp::SetForceDisable4K(const int32_t handle, const bool disable) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, SetForceDisable4K, handle, disable)
    }
    
    Core::hresult DeviceSettingsImp::GetForceDisable4K(const int32_t handle, bool &disabled) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetForceDisable4K, handle, disabled)
    }
    
    Core::hresult DeviceSettingsImp::IsVideoPortOutputHDR(const int32_t handle, bool &isHDR) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, IsVideoPortOutputHDR, handle, isHDR)
    }
    
    Core::hresult DeviceSettingsImp::ResetVideoPortOutputToSDR() {
        return _videoPortSettings ? _videoPortSettings->ResetVideoPortOutputToSDR() : Core::ERROR_GENERAL;
    }
    
    Core::hresult DeviceSettingsImp::GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) {
        VideoPortHdcpProtocolVersion internalVersion;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetHDMIPreference(handle, internalVersion) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            hdcpVersion = static_cast<VideoPortHdcpProtocolVersion>(internalVersion);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion) {
        return _videoPortSettings ? _videoPortSettings->SetHDMIPreference(handle, static_cast<VideoPortHdcpProtocolVersion>(hdcpVersion)) : Core::ERROR_GENERAL;
    }
    
    Core::hresult DeviceSettingsImp::GetVideoEOTF(const int32_t handle, HDRStandard &hdrStandard) {
        HDRStandard internalHdrStandard;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetVideoEOTF(handle, internalHdrStandard) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            hdrStandard = static_cast<Exchange::IDeviceSettingsVideoPort::HDRStandard>(internalHdrStandard);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::GetMatrixCoefficients(const int32_t handle, Exchange::IDeviceSettingsVideoPort::DisplayMatrixCoefficients &matrixCoefficients) {
        DisplayMatrixCoefficients internalMatrixCoefficients;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetMatrixCoefficients(handle, internalMatrixCoefficients) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            matrixCoefficients = static_cast<Exchange::IDeviceSettingsVideoPort::DisplayMatrixCoefficients>(internalMatrixCoefficients);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::GetCurrentOutputSettings(const int32_t handle, Exchange::IDeviceSettingsVideoPort::DSOutputSettings &outputSettings) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetCurrentOutputSettings, handle, outputSettings)
    }
    
    Core::hresult DeviceSettingsImp::SetBackgroundColor(const int32_t handle, const Exchange::IDeviceSettingsVideoPort::VideoBackgroundColor backgroundColor) {
        return _videoPortSettings ? _videoPortSettings->SetBackgroundColor(handle, static_cast<VideoBackgroundColor>(backgroundColor)) : Core::ERROR_GENERAL;
    }
    
    Core::hresult DeviceSettingsImp::SetForceHDRMode(const int32_t handle, const Exchange::IDeviceSettingsVideoPort::HDRStandard hdrMode) {
        return _videoPortSettings ? _videoPortSettings->SetForceHDRMode(handle, static_cast<HDRStandard>(hdrMode)) : Core::ERROR_GENERAL;
    }
    
    Core::hresult DeviceSettingsImp::GetColorDepthCapabilities(const int32_t handle, uint32_t &colorDepthCapabilities) {
        DELEGATE_TO_COMPONENT(_videoPortSettings, GetColorDepthCapabilities, handle, colorDepthCapabilities)
    }
    
    Core::hresult DeviceSettingsImp::GetPreferredColorDepth(const int32_t handle, Exchange::IDeviceSettingsVideoPort::DisplayColorDepth &colorDepth, const bool persist) {
        DisplayColorDepth internalColorDepth;
        Core::hresult result = _videoPortSettings ? _videoPortSettings->GetPreferredColorDepth(handle, internalColorDepth, persist) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            colorDepth = static_cast<Exchange::IDeviceSettingsVideoPort::DisplayColorDepth>(internalColorDepth);
        }
        return result;
    }
    
    Core::hresult DeviceSettingsImp::SetPreferredColorDepth(const int32_t handle, const Exchange::IDeviceSettingsVideoPort::DisplayColorDepth colorDepth, const bool persist) {
        return _videoPortSettings ? _videoPortSettings->SetPreferredColorDepth(handle, static_cast<DisplayColorDepth>(colorDepth), persist) : Core::ERROR_GENERAL;
    }

    // IDeviceSettingsVideoDevice interface implementation - delegate to _videoDeviceSettings interface

    Core::hresult DeviceSettingsImp::Register(const string clientName, Exchange::IDeviceSettingsVideoDevice::INotification* notification) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, Register, clientName, notification)
    }

    Core::hresult DeviceSettingsImp::Unregister(Exchange::IDeviceSettingsVideoDevice::INotification* notification) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, Unregister, notification)
    }

    Core::hresult DeviceSettingsImp::GetVideoDeviceHandle(const int32_t index, int32_t &handle) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, GetVideoDeviceHandle, index, handle)
    }

    Core::hresult DeviceSettingsImp::SetVideoDeviceDFC(const int32_t handle, const Exchange::IDeviceSettingsVideoDevice::VideoZoom zoom) {
        return _videoDeviceSettings ? _videoDeviceSettings->SetVideoDeviceDFC(handle, static_cast<VideoZoom>(zoom)) : Core::ERROR_GENERAL;
    }

    Core::hresult DeviceSettingsImp::GetVideoDeviceDFC(const int32_t handle, Exchange::IDeviceSettingsVideoDevice::VideoZoom &zoom) {
        VideoZoom internalZoom;
        Core::hresult result = _videoDeviceSettings ? _videoDeviceSettings->GetVideoDeviceDFC(handle, internalZoom) : Core::ERROR_GENERAL;
        if (result == Core::ERROR_NONE) {
            zoom = static_cast<Exchange::IDeviceSettingsVideoDevice::VideoZoom>(internalZoom);
        }
        return result;
    }

    Core::hresult DeviceSettingsImp::GetHDRCapabilities(const int32_t handle, int32_t &capabilities) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, GetHDRCapabilities, handle, capabilities)
    }

    Core::hresult DeviceSettingsImp::GetSupportedVideoCodingFormats(const int32_t handle, int32_t &supportedFormats) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, GetSupportedVideoCodingFormats, handle, supportedFormats)
    }

    Core::hresult DeviceSettingsImp::SetDisplayFrameRate(const int32_t handle, const string& framerate) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, SetDisplayFrameRate, handle, framerate)
    }

    Core::hresult DeviceSettingsImp::GetCodecInfo(const int32_t handle, const Exchange::IDeviceSettingsVideoDevice::VideoCodec videoCodec, Exchange::IDeviceSettingsVideoDevice::IDeviceSettingsVideoCodecProfileSupportIterator *&codecInfo) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, GetCodecInfo, handle, static_cast<VideoCodec>(videoCodec), codecInfo)
    }

    Core::hresult DeviceSettingsImp::DisableHDR(const int32_t handle, const bool disable) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, DisableHDR, handle, disable)
    }

    Core::hresult DeviceSettingsImp::SetFRFMode(const int32_t handle, const int32_t frfmode) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, SetFRFMode, handle, frfmode)
    }

    Core::hresult DeviceSettingsImp::GetFRFMode(const int32_t handle, int32_t &frfmode) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, GetFRFMode, handle, frfmode)
    }

    Core::hresult DeviceSettingsImp::GetCurrentDisplayFrameRate(const int32_t handle, string &framerate) {
        DELEGATE_TO_COMPONENT(_videoDeviceSettings, GetCurrentDisplayFrameRate, handle, framerate)
    }

    // ============================================================================
    // IDeviceSettingsHost interface implementation - delegate to _hostSettings interface
    // ============================================================================

    Core::hresult DeviceSettingsImp::GetEDID(uint8_t edId[], const uint16_t edIdLength) {
        DELEGATE_TO_COMPONENT(_hostSettings, GetEDID, edId, edIdLength)
    }

    Core::hresult DeviceSettingsImp::GetMS12ConfigType(string &ms12Config) {
        DELEGATE_TO_COMPONENT(_hostSettings, GetMS12ConfigType, ms12Config)
    }

    // ============================================================================
    // IDeviceSettingsDisplay interface implementation - delegate to _displaySettings interface
    // ============================================================================

    Core::hresult DeviceSettingsImp::Register(const string clientName, IDisplayNotification* notification) {
        DELEGATE_TO_COMPONENT(_displaySettings, Register, clientName, notification)
    }

    Core::hresult DeviceSettingsImp::Unregister(IDisplayNotification* notification) {
        DELEGATE_TO_COMPONENT(_displaySettings, Unregister, notification)
    }

    Core::hresult DeviceSettingsImp::GetDisplayEdid(const int32_t handle, DisplayEDID &edId, IDSVideoPortResolutionIterator*& supportedResolutionList) {
        DELEGATE_TO_COMPONENT(_displaySettings, GetDisplayEdid, handle, edId, supportedResolutionList)
    }

    Core::hresult DeviceSettingsImp::GetDisplayEdidBytes(const int32_t handle, uint8_t edIdBytes[], const uint16_t edidLength) {
        DELEGATE_TO_COMPONENT(_displaySettings, GetDisplayEdidBytes, handle, edIdBytes, edidLength)
    }

    Core::hresult DeviceSettingsImp::GetDisplay(const DisplayPortType portType, const int32_t index, int32_t &handle) {
        DELEGATE_TO_COMPONENT(_displaySettings, GetDisplay, portType, index, handle)
    }

    Core::hresult DeviceSettingsImp::Register(const string clientName, IDisplayHDMIHotPlugNotification* notification) {
        DELEGATE_TO_COMPONENT(_displaySettings, Register, clientName, notification)
    }

    Core::hresult DeviceSettingsImp::Unregister(IDisplayHDMIHotPlugNotification* notification) {
        DELEGATE_TO_COMPONENT(_displaySettings, Unregister, notification)
    }

    Core::hresult DeviceSettingsImp::GetDisplayAspectRatio(const int32_t handle, Exchange::IDeviceSettingsDisplay::DisplayVideoAspectRatio &aspectRatio) {
        DELEGATE_TO_COMPONENT(_displaySettings, GetDisplayAspectRatio, handle, aspectRatio)
    }

    Core::hresult DeviceSettingsImp::SetAllmEnabled(const int32_t handle, const bool enabled) {
        DELEGATE_TO_COMPONENT(_displaySettings, SetAllmEnabled, handle, enabled)
    }

    Core::hresult DeviceSettingsImp::SetAVIContentType(const int32_t handle, const DisplayAVIContentType contentType) {
        DELEGATE_TO_COMPONENT(_displaySettings, SetAVIContentType, handle, contentType)
    }

    Core::hresult DeviceSettingsImp::SetAVIScanInformation(const int32_t handle, const DisplayAVIScanInformation scanInfo) {
        DELEGATE_TO_COMPONENT(_displaySettings, SetAVIScanInformation, handle, scanInfo)
    }

    // ============================================================================
    // IDeviceSettingsCompositeIn interface implementation - delegate to _compositeInSettings interface
    // ============================================================================

    Core::hresult DeviceSettingsImp::Register(const string clientName, Exchange::IDeviceSettingsCompositeIn::INotification* notification) {
        DELEGATE_TO_COMPONENT(_compositeInSettings, Register, clientName, notification)
    }

    Core::hresult DeviceSettingsImp::Unregister(Exchange::IDeviceSettingsCompositeIn::INotification* notification) {
        DELEGATE_TO_COMPONENT(_compositeInSettings, Unregister, notification)
    }

    Core::hresult DeviceSettingsImp::GetNrOfCompositeInputs(int32_t &nrCompositeInputs) {
        DELEGATE_TO_COMPONENT(_compositeInSettings, GetNrOfCompositeInputs, nrCompositeInputs)
    }

    Core::hresult DeviceSettingsImp::GetCompositeInStatus(CompositeInStatus &status) {
        DELEGATE_TO_COMPONENT(_compositeInSettings, GetCompositeInStatus, status)
    }

    Core::hresult DeviceSettingsImp::SelectCompositeInPort(const CompositeInPort port) {
        DELEGATE_TO_COMPONENT(_compositeInSettings, SelectCompositeInPort, port)
    }

    Core::hresult DeviceSettingsImp::ScaleCompositeInVideo(const CompositeInVideoRectangle videoRect) {
        DELEGATE_TO_COMPONENT(_compositeInSettings, ScaleCompositeInVideo, videoRect)
    }

    // Static instance method implementation
    DeviceSettingsImp* DeviceSettingsImp::instance(DeviceSettingsImp* DeviceSettingsImpl)
    {
        if (DeviceSettingsImpl != nullptr) {
            _instance = DeviceSettingsImpl;
        }
        return _instance;
    }

    // ============================================================================
    // IDeviceSettings::GetDeviceSettingConfigs — single consolidated config call
    // ============================================================================

    Core::hresult DeviceSettingsImp::GetDeviceSettingConfigs(Exchange::IDeviceSettings::DeviceSettingConfigs& configs)
    {
        // Serve from cache on all calls after the first.
        if (_configLoaded.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(_configMutex);
            configs = _cachedConfigs;
            return Core::ERROR_NONE;
        }

        // First call: load from HAL, cache result, then return.
        // Config population is intentionally deferred here (not in constructors)
        // so plugin activation is not delayed by HAL config loading.

        // ── FPD config — IDeviceSettings types identical, direct population ──
        DeviceSettingsHAL::PopulateFPDConfig(
            configs.colors, configs.indicators, configs.textDisplays, configs.colorBindings);

        // ── Audio config ─────────────────────────────────────────────────────
        {
            using AudioTypeCfg = Exchange::IDeviceSettings::AudioTypeConfigInfo;
            using AudioPortCfg = Exchange::IDeviceSettingsAudio::AudioPortConfigInfo;
            std::vector<AudioTypeCfg> audioTypes;
            std::vector<AudioPortCfg> audioPorts;
            DeviceSettingsHAL::PopulateAudioConfig(audioTypes, audioPorts);

            // AudioTypeConfigInfo is identical in IDeviceSettings — direct copy
            configs.audioTypes.assign(audioTypes.begin(), audioTypes.end());

            // AudioPortConfigInfo still differs (AudioPortType enum → int32_t)
            configs.audioPorts.reserve(audioPorts.size());
            for (const auto& src : audioPorts) {
                configs.audioPorts.push_back({
                    static_cast<int32_t>(src.audioPortType),
                    src.audioPortIndex,
                    src.connectedVideoPortType,
                    src.connectedVideoPortIndex});
            }
        }

        // ── Video device config ───────────────────────────────────────────────
        {
            using VDevCfg = Exchange::IDeviceSettingsVideoDevice::VideoDeviceConfigInfo;
            std::vector<VDevCfg> videoDeviceConfigs;
            DeviceSettingsHAL::PopulateVideoDeviceConfig(videoDeviceConfigs);
            configs.videoConfigs.reserve(videoDeviceConfigs.size());
            for (const auto& src : videoDeviceConfigs) {
                configs.videoConfigs.push_back({
                    src.numSupportedDFCs,
                    src.supportedDFCsMask,
                    static_cast<int32_t>(src.defaultDFC)});
            }
        }

        // ── Video port config ─────────────────────────────────────────────────
        {
            using VPortTypeCfg = Exchange::IDeviceSettingsVideoPort::VideoPortTypeConfig;
            using VPortPortCfg = Exchange::IDeviceSettingsVideoPort::VideoPortPortConfig;
            using VPortRes     = Exchange::IDeviceSettingsVideoPort::VideoPortResolution;
            std::vector<VPortTypeCfg> videoPortTypes;
            std::vector<VPortPortCfg> videoPorts;
            DeviceSettingsHAL::PopulateVideoPortConfig(videoPortTypes, videoPorts);

            configs.videoPortTypes.reserve(videoPortTypes.size());
            for (const auto& src : videoPortTypes) {
                configs.videoPortTypes.push_back({
                    static_cast<int32_t>(src.typeId),
                    src.name,
                    src.dtcpSupported,
                    src.hdcpSupported,
                    src.restrictedResolution,
                    src.supportedResolutionNames});
            }

            configs.videoPorts.reserve(videoPorts.size());
            for (const auto& src : videoPorts) {
                configs.videoPorts.push_back({
                    static_cast<int32_t>(src.videoPortType),
                    src.videoPortIndex,
                    src.connectedAudioPortType,
                    src.connectedAudioPortIndex,
                    src.defaultResolution});
            }

            // Resolution config for the 0th video port type
            if (!videoPortTypes.empty()) {
                std::vector<VPortRes> resolutions;
                DeviceSettingsHAL::PopulateVideoPortResolutionConfig(
                    videoPortTypes[0].typeId, resolutions);
                configs.videoPortResolutions.reserve(resolutions.size());
                for (const auto& src : resolutions) {
                    configs.videoPortResolutions.push_back({
                        src.name,
                        static_cast<int32_t>(src.pixelResolution),
                        static_cast<int32_t>(src.aspectRatio),
                        static_cast<int32_t>(src.stereoScopicMode),
                        static_cast<int32_t>(src.frameRate),
                        src.interlaced});
                }
            }
        }

        DSLOG_INFO("audioTypes=%zu audioPorts=%zu "
                "textDisplays=%zu indicators=%zu colors=%zu colorBindings=%zu "
                "videoConfigs=%zu videoPortTypes=%zu videoPorts=%zu videoPortResolutions=%zu",
            configs.audioTypes.size(), configs.audioPorts.size(),
            configs.textDisplays.size(), configs.indicators.size(),
            configs.colors.size(), configs.colorBindings.size(),
            configs.videoConfigs.size(), configs.videoPortTypes.size(), configs.videoPorts.size(),
            configs.videoPortResolutions.size());

        // Store in cache for subsequent calls
        {
            std::lock_guard<std::mutex> lock(_configMutex);
            _cachedConfigs = configs;
        }
        _configLoaded.store(true, std::memory_order_release);

        return Core::ERROR_NONE;
    }

} // namespace Plugin
} // namespace WPEFramework
