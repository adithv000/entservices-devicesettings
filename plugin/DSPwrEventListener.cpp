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

#include "DSPwrEventListener.h"
#include "DSProductTraitsHandler.h"  
#include "DSController.h"
#include "DeviceSettingsTypes.h"
#include "DeviceSettingsImplementation.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>

extern "C" {
    extern void _setEASAudioMode();
}

#define PWRMGR_REBOOT_REASON_MAINTENANCE "MAINTENANCE_REBOOT"

// Static variable accessible to functions outside namespace
static WPEFramework::Plugin::DSProductTraits::UXController* ux = nullptr;

namespace WPEFramework {
namespace Plugin {

DSPwrEventListener* DSPwrEventListener::_instance = nullptr;

DSPwrEventListener::DSPwrEventListener()
    : _pwrEventHandlerThreadID(0)
    , _pwrConnectThreadID(0)
    , _stopThread(false)
    , _initialized(false)
    , _pwrEventHandlerThreadStarted(false)
    , _pwrConnectThreadStarted(false)
    , _registeredPowerEventHandler(false)
    , _curState(PowerState::POWER_STATE_STANDBY)
    , _pwrMgrNotification(*this)
    , _service(nullptr)
    , _deviceSettings(nullptr)
{
    DSLOG_INFO("Constructor");
    memset(_standbyVideoPortSetting, 0, sizeof(_standbyVideoPortSetting));
    DSPwrEventListener::_instance = this;
}

DSPwrEventListener* DSPwrEventListener::GetInstance()
{
    return _instance;
}

bool DSPwrEventListener::IsDeviceSettingsReady(bool refreshCacheIfEmpty)
{
    if (_deviceSettings == nullptr) {
        _deviceSettings = DeviceSettingsImp::instance();
        if (_deviceSettings == nullptr) {
            DSLOG_ERR("DeviceSettings implementation not available yet");
            return false;
        }

        DSLOG_INFO("DeviceSettings implementation recovered");
        RefreshPortConfigurationCache();
        return true;
    }

    if (refreshCacheIfEmpty && _videoPortEntries.empty() && _audioPortEntries.empty()) {
        RefreshPortConfigurationCache();
    }

    return true;
}

void DSPwrEventListener::RefreshPortConfigurationCache()
{
    // Use GetDeviceSettingConfigs() to load all configs in a single call and
    // build VideoPortEntry / AudioPortEntry vectors directly from DeviceSettingsInterface.h types —
    // no intermediate VideoPortConfigStore / AudioConfigStore mirroring needed.
    Exchange::IDeviceSettings::DeviceSettingConfigs rawCfg;
    const Core::hresult rc = _deviceSettings->GetDeviceSettingConfigs(rawCfg);
    if (rc != Core::ERROR_NONE) {
        DSLOG_ERR("GetDeviceSettingConfigs failed: %u",
               static_cast<uint32_t>(rc));
        return;
    }

    // Build VideoPortEntry vector directly from raw config
    std::vector<VideoPortEntry> newVpEntries;
    for (const auto& pc : rawCfg.videoPorts) {
        VideoPortEntry e;
        e.type  = static_cast<VideoPortType>(pc.videoPortType);
        e.index = pc.videoPortIndex;
        for (const auto& tc : rawCfg.videoPortTypes) {
            if (tc.typeId == pc.videoPortType) {
                e.typeName = tc.name;
                break;
            }
        }
        e.name = getVideoPortName(e.type, e.index);
        newVpEntries.push_back(std::move(e));
    }

    // Build AudioPortEntry vector directly from raw config
    std::vector<AudioPortEntry> newAudioEntries;
    for (const auto& pc : rawCfg.audioPorts) {
        AudioPortEntry e;
        e.type  = static_cast<AudioPortType>(pc.audioPortType);
        e.index = pc.audioPortIndex;
        e.name  = getAudioPortName(e.type, e.index);
        newAudioEntries.push_back(std::move(e));
    }

    _videoPortEntries = std::move(newVpEntries);
    _audioPortEntries = std::move(newAudioEntries);
    DSLOG_INFO("loaded %zu videoPorts, %zu audioPorts",
            _videoPortEntries.size(), _audioPortEntries.size());
}

bool DSPwrEventListener::BuildVideoPortEntries(std::vector<DSPwrEventListener::VideoPortEntry>& entries)
{
    if (IsDeviceSettingsReady(true) == false) {
        return false;
    }
    entries = _videoPortEntries;
    return !entries.empty();
}

bool DSPwrEventListener::BuildAudioPortEntries(std::vector<DSPwrEventListener::AudioPortEntry>& entries)
{
    if (IsDeviceSettingsReady(true) == false) {
        return false;
    }
    entries = _audioPortEntries;
    return !entries.empty();
}

bool DSPwrEventListener::ResolveVideoPortEntryByName(const std::string& requestedPort, DSPwrEventListener::VideoPortEntry& resolvedEntry)
{
    if (IsDeviceSettingsReady(true) == false) {
        return false;
    }
    // Resolve directly from cached _videoPortEntries — no config store lookup needed.
    for (const auto& e : _videoPortEntries) {
        if (e.name == requestedPort) {
            resolvedEntry = e;
            return true;
        }
    }
    return false;
}

DSPwrEventListener::~DSPwrEventListener()
{
    DSLOG_INFO("Destructor");
    Deinit();
    if (_instance == this) {
        _instance = nullptr;
    }
}

void DSPwrEventListener::Init(PluginHost::IShell* service)
{
    DSLOG_INFO("Entering");
    
    _service = service;
    _service->AddRef();

    if (IsDeviceSettingsReady(true) == false) {
        DSLOG_ERR("DeviceSettings implementation not ready, will retry lazily");
    }
    
    // profileType is already initialized in DeviceSettingsImplementation.cpp constructor
    // No need to call searchRdkProfile() again here

    if (profileType == TV) { // TV
        if (WPEFramework::Plugin::DSProductTraits::UXController::Initialize(WPEFramework::Plugin::DSProductTraits::DEFAULT_TV_PROFILE)) {
            ux = WPEFramework::Plugin::DSProductTraits::UXController::GetInstance();
        }
    } else { // STB
        if (WPEFramework::Plugin::DSProductTraits::UXController::Initialize(WPEFramework::Plugin::DSProductTraits::DEFAULT_STB_PROFILE_EUROPE)) {
            ux = WPEFramework::Plugin::DSProductTraits::UXController::GetInstance();
        }
    }
    
    if (nullptr == ux) {
        DSLOG_INFO("DSMgr product traits not supported");
    }

    // Note: device::Manager::load() is intentionally disabled to avoid linker dependency
    // on DS library which may not be available in all configurations.
    // The WPEFramework plugin architecture handles initialization independently.
    // Original code kept commented for reference:
    // try {
    //     device::Manager::load();
    //     DSLOG_INFO("device::Manager::load success");
    // } catch (...) {
    //     DSLOG_ERR("Exception Caught during device::Manager::load");
    // }

    // TODO: Re-enable these DSMGR IARM API registrations when a client starts consuming them.
    // Currently no client calls these APIs, so registration is intentionally disabled.
    //
    // IARM_Result_t rc;
    // rc = IARM_Bus_RegisterCall(IARM_BUS_DSMGR_API_SetStandbyVideoState, SetStandbyVideoState);
    // if (IARM_RESULT_SUCCESS != rc) {
    //     DSLOG_ERR("IARM_Bus_RegisterCall Failed for SetStandbyVideoState, Error: %d", rc);
    // }
    //
    // rc = IARM_Bus_RegisterCall(IARM_BUS_DSMGR_API_GetStandbyVideoState, GetStandbyVideoState);
    // if (IARM_RESULT_SUCCESS != rc) {
    //     DSLOG_ERR("IARM_Bus_RegisterCall Failed for GetStandbyVideoState, Error: %d", rc);
    // }
    //
    // rc = IARM_Bus_RegisterCall(IARM_BUS_DSMGR_API_SetAvPortState, SetAvPortState);
    // if (IARM_RESULT_SUCCESS != rc) {
    //     DSLOG_ERR("IARM_Bus_RegisterCall Failed for SetAvPortState, Error: %d", rc);
    // }
    //
    // rc = IARM_Bus_RegisterCall(IARM_BUS_DSMGR_API_SetLEDStatus, SetLEDState);
    // if (IARM_RESULT_SUCCESS != rc) {
    //     DSLOG_ERR("IARM_Bus_RegisterCall Failed for SetLEDStatus, Error: %d", rc);
    // }
    //
    // rc = IARM_Bus_RegisterCall(IARM_BUS_DSMGR_API_SetRebootConfig, SetRebootConfig);
    // if (IARM_RESULT_SUCCESS != rc) {
    //     DSLOG_ERR("IARM_Bus_RegisterCall Failed for SetRebootConfig, Error: %d", rc);
    // }

    // Initialize mutexes and condition variables
    pthread_mutex_init(&_pwrEventQueueMutexLock, NULL);
    pthread_mutex_init(&_pwrEventMutexLock, NULL);
    pthread_cond_init(&_pwrEventMutexCond, NULL);

    _stopThread = false;
    if (pthread_create(&_pwrEventHandlerThreadID, NULL, PwrEventHandlingThreadFunc, this) != 0) {
        DSLOG_ERR("DSMgr PwrEventHandlingThread creation failed");
        _pwrEventHandlerThreadStarted = false;
    } else {
        _pwrEventHandlerThreadStarted = true;
    }
    _initialized = true;

    // Initialize PowerManager connection using retry pattern (like original dsMgr)
    DSLOG_INFO("DSMgr PowerManager Connect setup in a Thread");
    PwrCtrlEstablishConnection();
}

void DSPwrEventListener::Deinit()
{
    DSLOG_INFO("Entering");

    if (!_initialized) {
        return;
    }

    _stopThread = true;

    pthread_mutex_lock(&_pwrEventMutexLock);
    pthread_cond_signal(&_pwrEventMutexCond);
    pthread_mutex_unlock(&_pwrEventMutexLock);

    DSLOG_INFO("Before joining thread");
    if (_pwrEventHandlerThreadStarted) {
        pthread_join(_pwrEventHandlerThreadID, NULL);
        _pwrEventHandlerThreadStarted = false;
    }
    DSLOG_INFO("Completed joining thread");

    if (_pwrConnectThreadStarted) {
        pthread_join(_pwrConnectThreadID, NULL);
        _pwrConnectThreadStarted = false;
    }

    if (_powerManagerPlugin) {
        _powerManagerPlugin->Unregister(_pwrMgrNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
        _powerManagerPlugin.Reset();
    }
    _registeredPowerEventHandler = false;

    pthread_mutex_lock(&_pwrEventQueueMutexLock);
    while (!_pwrEventQueue.empty()) {
        _pwrEventQueue.pop();
    }
    pthread_mutex_unlock(&_pwrEventQueueMutexLock);

    pthread_cond_destroy(&_pwrEventMutexCond);
    pthread_mutex_destroy(&_pwrEventQueueMutexLock);
    pthread_mutex_destroy(&_pwrEventMutexLock);

    if (_service) {
        _service->Release();
        _service = nullptr;
    }

    _initialized = false;
}

void DSPwrEventListener::InitializePowerManager()
{
    DSLOG_INFO("Connecting to PowerManager plugin");
    PowerState pwrStateCur = PowerState::POWER_STATE_UNKNOWN;
    PowerState pwrStatePrev = PowerState::POWER_STATE_UNKNOWN;
    Core::hresult retStatus = Core::ERROR_GENERAL;
    
    _powerManagerPlugin = PowerManagerInterfaceBuilder(_T("org.rdk.PowerManager"))
        .withIShell(_service)
        .withRetryIntervalMS(200)
        .withRetryCount(25)
        .createInterface();

    registerPowerEventHandler();

    if (_powerManagerPlugin) {
        retStatus = _powerManagerPlugin->GetPowerState(pwrStateCur, pwrStatePrev);
    }
    
    if (Core::ERROR_NONE == retStatus) {
        _curState = pwrStateCur;
        DSLOG_INFO("Current power state: %d", _curState);
    } else {
        DSLOG_ERR("Failed to get power state");
    }
}

void DSPwrEventListener::registerPowerEventHandler()
{
    if (!_registeredPowerEventHandler && _powerManagerPlugin) {
        DSLOG_INFO("Registering PowerManager event handler");
        _registeredPowerEventHandler = true;
        _powerManagerPlugin->Register(_pwrMgrNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
    } else {
        DSLOG_INFO("PowerManager event handler already registered or plugin not available");
    }
}

void PowerManagerNotification::OnPowerModeChanged(const PowerState currentState, const PowerState newState)
{
    _parent.onPowerModeChanged(currentState, newState);
}

void DSPwrEventListener::onPowerModeChanged(const PowerState currentState, const PowerState newState)
{
    DSLOG_INFO("currentState: %d, newState: %d", currentState, newState);
    
    // Queue the event for thread processing (same pattern as dsMgr original)
    pthread_mutex_lock(&_pwrEventQueueMutexLock);
    _pwrEventQueue.emplace(currentState, newState);
    pthread_mutex_unlock(&_pwrEventQueueMutexLock);
    
    DSLOG_INFO("Sending signal to thread for processing callback event");
    pthread_mutex_lock(&_pwrEventMutexLock);
    pthread_cond_signal(&_pwrEventMutexCond);
    pthread_mutex_unlock(&_pwrEventMutexLock);
}

void DSPwrEventListener::PwrCtrlEstablishConnection()
{
    DSLOG_INFO("Entering");

    // Joinable, and joined by Deinit() before the components it touches are torn down:
    // DeviceSettingsImp::~DeviceSettingsImp() destroys DSController (which Deinit()s/joins
    // this listener) before deleting the component implementations.
    if (pthread_create(&_pwrConnectThreadID, NULL, PwrRetryEstablishConnThread, this) == 0) {
        _pwrConnectThreadStarted = true;
    } else {
        DSLOG_ERR("DSPwrEventListener PwrCtrlEstablishConnection Thread Creation Failed");
    }
}

void DSPwrEventListener::PwrControllerFetchNinitStateValues()
{

    PowerState powerStateBeforeReboot = PowerState::POWER_STATE_STANDBY;
    if (_powerManagerPlugin) {
        Core::hresult retStatus = _powerManagerPlugin->GetPowerStateBeforeReboot(powerStateBeforeReboot);
        if (Core::ERROR_NONE != retStatus) {
            DSLOG_ERR("GetPowerStateBeforeReboot failed, defaulting to STANDBY");
        }
    }

    // Note: _curState is already set in InitializePowerManager from GetPowerState
    DSLOG_INFO("Current Power State: %d, Power State Before Reboot: %d", _curState, powerStateBeforeReboot);

    if (nullptr != ux) {
        ux->ApplyPostRebootConfig(_curState, powerStateBeforeReboot);
    }

    if (nullptr == ux) {
#ifdef ENABLE_LED_SYNC_IN_BOOTUP
        SetLEDStatus(_curState);
#endif
        SetAVPortsPowerState(_curState);
    }
}

void DSPwrEventListener::HandlePwrEventData(const PowerState currentState,
                                           const PowerState newState)
{
    DSLOG_INFO("currentState: %d, newState: %d", currentState, newState);
    
    if (nullptr != ux) {
        ux->ApplyPowerStateChangeConfig(newState, currentState);
    } else {
#ifdef ENABLE_LED_SYNC_IN_BOOTUP
        SetLEDStatus(newState);
#endif
        SetAVPortsPowerState(newState);
    }
}

int DSPwrEventListener::SetLEDStatus(PowerState powerState)
{
    DSLOG_INFO("powerState: %d", powerState);
    
    try {
        if (IsDeviceSettingsReady(true) == false) {
            DSLOG_ERR("DeviceSettings implementation not available");
            return -1;
        }

        if (_deviceSettings) {
            FPDIndicator indicator = static_cast<FPDIndicator>(dsFPD_INDICATOR_POWER);
            FPDState fpdState;
            
            if (PowerState::POWER_STATE_ON != powerState) {
                if (profileType == TV) {
                    fpdState = FPDState::DS_FPD_STATE_ON;
                    DSLOG_INFO("Settings Power LED State to ON");
                } else {
                    fpdState = FPDState::DS_FPD_STATE_OFF;
                    DSLOG_INFO("Settings Power LED State to OFF");
                }
            } else {
                fpdState = FPDState::DS_FPD_STATE_ON;
                DSLOG_INFO("Settings Power LED State to ON");
            }

            uint32_t result = _deviceSettings->SetFPDState(indicator, fpdState);
            if (result != WPEFramework::Core::ERROR_NONE) {
                DSLOG_ERR("SetFPDState failed with error: %d", result);
                return -1;
            }
        } else {
            DSLOG_ERR("DeviceSettings implementation not available");
            return -1;
        }
    } catch (...) {
        DSLOG_ERR("Exception Caught during SetLEDStatus");
        return -1;
    }
    
    return 0;
}

int DSPwrEventListener::SetAVPortsPowerState(PowerState powerState)
{
    DSLOG_INFO("powerState: %d", powerState);
    
    try {
        if (PowerState::POWER_STATE_ON != powerState) {
            // Non-ON power state (standby or off) - certain ports may stay on in standby modes
            try {
                std::vector<VideoPortEntry> videoPorts;
                if (!BuildVideoPortEntries(videoPorts)) {
                    DSLOG_ERR("Failed to enumerate video ports for powerState %d", static_cast<int>(powerState));
                }

                DSLOG_INFO("Number of Video Ports: %zu", videoPorts.size());

                for (size_t i = 0; i < videoPorts.size(); i++) {
                    try {
                        const VideoPortEntry& vPort = videoPorts.at(i);
                        bool doEnable = GetVideoPortStandbySetting(vPort.name.c_str());
                        DSLOG_INFO("Video port %s will be %s for PowerState %d",
                               vPort.name.c_str(), 
                               (doEnable ? "enabled" : "disabled"), 
                               static_cast<int>(powerState));
                               
                        if ((false == doEnable) || (PowerState::POWER_STATE_OFF == powerState)) {
                            uint32_t result = ConfigureVideoPort(vPort.name,
                                                               vPort.type,
                                                               vPort.index,
                                                               false);
                            if (result == WPEFramework::Core::ERROR_NONE) {
                                DSLOG_INFO("VideoPort %s disabled for powerState %d",
                                       vPort.name.c_str(), static_cast<int>(powerState));
                            }
                        } else {
                            DSLOG_INFO("VideoPort %s stays enabled for powerState %d",
                                   vPort.name.c_str(), static_cast<int>(powerState));
                        }
                    } catch (...) {
                        DSLOG_ERR("Exception caught in video port processing for port %zu", i);
                    }
                }
            } catch (...) {
                DSLOG_ERR("Exception caught during video port enumeration");
            }
            
            // Configure Audio Ports  
            try {
                std::vector<AudioPortEntry> audioPorts;
                if (!BuildAudioPortEntries(audioPorts)) {
                    DSLOG_ERR("Failed to enumerate audio ports for powerState %d", static_cast<int>(powerState));
                }
                DSLOG_INFO("Number of Audio Ports: %zu", audioPorts.size());
                
                for (size_t i = 0; i < audioPorts.size(); i++) {
                    try {
                        const AudioPortEntry& aPort = audioPorts.at(i);
                        bool isConfigSkipped = false;
                        
                        uint32_t result = ConfigureAudioPort(aPort.name,
                                                           aPort.type,
                                                           aPort.index,
                                                           false,
                                                           &isConfigSkipped);
                        if (result == WPEFramework::Core::ERROR_NONE) {
                            DSLOG_INFO("AudioPort %s disabled for powerState %d",
                                   aPort.name.c_str(), static_cast<int>(powerState));
                        }
                    } catch (...) {
                        DSLOG_ERR("Exception caught in audio port processing for port %zu", i);
                    }
                }
            } catch (...) {
                DSLOG_ERR("Exception caught during audio port enumeration");
            }
        } else {
            // POWER_STATE_ON - Enable all ports
            try {
                std::vector<VideoPortEntry> videoPorts;
                if (!BuildVideoPortEntries(videoPorts)) {
                    DSLOG_ERR("Failed to enumerate video ports for POWER_STATE_ON");
                }

                for (size_t i = 0; i < videoPorts.size(); i++) {
                    try {
                        const VideoPortEntry& vPort = videoPorts.at(i);

                        uint32_t result = ConfigureVideoPort(vPort.name,
                                                           vPort.type,
                                                           vPort.index,
                                                           true);
                        if (result == WPEFramework::Core::ERROR_NONE) {
                            DSLOG_INFO("VideoPort %s enabled for powerState %d",
                                   vPort.name.c_str(), static_cast<int>(powerState));
                        }
                    } catch (...) {
                        DSLOG_ERR("Exception caught in video port processing for port %zu", i);
                    }
                }
                
                std::vector<AudioPortEntry> audioPorts;
                if (!BuildAudioPortEntries(audioPorts)) {
                    DSLOG_ERR("Failed to enumerate audio ports for POWER_STATE_ON");
                }
                for (size_t i = 0; i < audioPorts.size(); i++) {
                    try {
                        const AudioPortEntry& aPort = audioPorts.at(i);
                        bool isConfigSkipped = false;
                        
                        uint32_t result = ConfigureAudioPort(aPort.name,
                                                           aPort.type,
                                                           aPort.index,
                                                           true,
                                                           &isConfigSkipped);
                        if (result == WPEFramework::Core::ERROR_NONE && !isConfigSkipped) {
                            DSLOG_INFO("AudioPort %s enabled for powerState %d",
                                   aPort.name.c_str(), static_cast<int>(powerState));
                        }
                    } catch (...) {
                        DSLOG_ERR("Exception caught in audio port processing for port %zu", i);
                    }
                }
                
                // Special EAS mode handling
                if (DSController::instance()->getEASMode() == IARM_BUS_SYS_MODE_EAS) {
                    DSLOG_INFO("Force Stereo in EAS mode");
                    // Set EAS audio mode using original dsMgr function
                    _setEASAudioMode();
                }
                
            } catch (...) {
                DSLOG_ERR("Exception caught during video port enumeration");
            }
        }
    } catch (...) {
        DSLOG_ERR("Exception Caught during SetAVPortsPowerState");
        return -1;
    }
    
    DSLOG_INFO("Exiting SetAVPortsPowerState");
    return 0;
}

bool DSPwrEventListener::GetVideoPortStandbySetting(const char* port)
{
    if (NULL == port) {
        DSLOG_ERR("Port name is NULL");
        return false;
    }
    
    for (int i = 0; i < MAX_NUM_VIDEO_PORTS; i++) {
        if (0 == strncasecmp(port, _standbyVideoPortSetting[i].port, DSMGR_MAX_VIDEO_PORT_NAME_LENGTH)) {
            return _standbyVideoPortSetting[i].isEnabled;
        }
    }
    return false; // Default: video port is disabled in standby mode
}



PowerState DSPwrEventListener::PwrMgrToPowerControllerPowerState(int pwrMgrState)
{
    PowerState powerState = PowerState::POWER_STATE_UNKNOWN;
    
    switch (pwrMgrState) {
        case 0: // PWRMGR_POWERSTATE_OFF
            powerState = PowerState::POWER_STATE_OFF;
            break;
        case 1: // PWRMGR_POWERSTATE_STANDBY
            powerState = PowerState::POWER_STATE_STANDBY;
            break;
        case 2: // PWRMGR_POWERSTATE_ON
            powerState = PowerState::POWER_STATE_ON;
            break;
        case 3: // PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP
            powerState = PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP;
            break;
        case 4: // PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP
            powerState = PowerState::POWER_STATE_STANDBY_DEEP_SLEEP;
            break;
        default:
            DSLOG_ERR("Invalid Power State: %d", pwrMgrState);
            break;
    }
    
    DSLOG_INFO("pwrMgrState=%d converted to powerState=%d", pwrMgrState, static_cast<int>(powerState));
    return powerState;
}

void DSPwrEventListener::InitPwrControllerEvt()
{
    DSLOG_INFO("Entering");

    // Initialize mutexes and condition variables (already done in constructor)
    // Thread is already created in Init() method
    
    // This method is kept for compatibility with original dsMgr pattern
    // The actual mutex/thread initialization happens in Init()
    DSLOG_INFO("Power Controller Event handling initialized");
}

void DSPwrEventListener::DeinitPwrControllerEvt()
{
    DSLOG_INFO("Entering");

    // Stop thread and cleanup
    pthread_mutex_lock(&_pwrEventMutexLock);
    _stopThread = true;
    pthread_cond_signal(&_pwrEventMutexCond);
    pthread_mutex_unlock(&_pwrEventMutexLock);

    DSLOG_INFO("Before joining thread");
    if (_pwrEventHandlerThreadStarted) {
        pthread_join(_pwrEventHandlerThreadID, NULL);
        _pwrEventHandlerThreadStarted = false;
    }
    DSLOG_INFO("Completed joining thread");

    // Clean the queue with guarding mutex
    pthread_mutex_lock(&_pwrEventQueueMutexLock);
    while (!_pwrEventQueue.empty()) {
        _pwrEventQueue.pop();
    }
    pthread_mutex_unlock(&_pwrEventQueueMutexLock);

    // Destroy condition variable and mutexes (handled in destructor)
    DSLOG_INFO("Power Controller Event handling deinitialized");
}

} // namespace Plugin  
} // namespace WPEFramework

// Static member functions defined outside namespace with full qualification
IARM_Result_t WPEFramework::Plugin::DSPwrEventListener::SetStandbyVideoState(void* arg)
{
    if (NULL == arg) {
        return IARM_RESULT_INVALID_PARAM;
    }
    
    if (!_instance) {
        return IARM_RESULT_INVALID_STATE;
    }
    
    dsMgrStandbyVideoStateParam_t* param = (dsMgrStandbyVideoStateParam_t*)arg;
    param->result = 0;

    int i = 0;
    for (i = 0; i < MAX_NUM_VIDEO_PORTS; i++) {
        if (0 == strncasecmp(param->port, _instance->_standbyVideoPortSetting[i].port, DSMGR_MAX_VIDEO_PORT_NAME_LENGTH)) {
            _instance->_standbyVideoPortSetting[i].isEnabled = ((0 == param->isEnabled) ? false : true);
            break;
        }
    }
    
    if (MAX_NUM_VIDEO_PORTS == i) {
        for (i = 0; i < MAX_NUM_VIDEO_PORTS; i++) {
            if ('\0' == _instance->_standbyVideoPortSetting[i].port[0]) {
                strncpy(_instance->_standbyVideoPortSetting[i].port, param->port, (DSMGR_MAX_VIDEO_PORT_NAME_LENGTH - 1));
                _instance->_standbyVideoPortSetting[i].isEnabled = ((0 == param->isEnabled) ? false : true);
                break;
            }
        }
    }
    
    if (MAX_NUM_VIDEO_PORTS == i) {
        DSLOG_ERR("Error! Out of room to write new video port setting for standby mode");
    }
    
    // Apply setting immediately if currently in standby state (like original dsMgr)
    try {
        if (PowerState::POWER_STATE_ON != _instance->_curState && PowerState::POWER_STATE_OFF != _instance->_curState) {
            // We're currently in one of the standby states. Apply this new setting right away.
            DSLOG_INFO("Setting standby %s port status to %s immediately",
                   param->port, (param->isEnabled ? "enabled" : "disabled"));

            VideoPortEntry resolvedPort;
                 if (_instance->ResolveVideoPortEntryByName(param->port, resolvedPort)) {
                const uint32_t result = _instance->ConfigureVideoPort(resolvedPort.name,
                                                                      resolvedPort.type,
                                                                      resolvedPort.index,
                                                                      (1 == param->isEnabled));
                if (result != WPEFramework::Core::ERROR_NONE) {
                    DSLOG_ERR("Failed to update standby video port state for %s", param->port);
                    param->result = -1;
                }
            } else {
                DSLOG_ERR("Failed to resolve standby video port %s", param->port);
                param->result = -1;
            }
        } else {
            DSLOG_INFO("Video port %s will be %s when going into standby mode",
                   param->port, (param->isEnabled ? "enabled" : "disabled"));
        }
    } catch (...) {
        DSLOG_ERR("Exception caught during immediate video port setting for %s. Possible bad video port", param->port);
        param->result = -1;
    }
    
    return IARM_RESULT_SUCCESS;
}

IARM_Result_t WPEFramework::Plugin::DSPwrEventListener::GetStandbyVideoState(void* arg)
{
    if (NULL == arg) {
        return IARM_RESULT_INVALID_PARAM;
    }
    
    if (!_instance) {
        return IARM_RESULT_INVALID_STATE;
    }
    
    dsMgrStandbyVideoStateParam_t* param = (dsMgrStandbyVideoStateParam_t*)arg;
    param->isEnabled = (_instance->GetVideoPortStandbySetting(param->port) ? 1 : 0);
    param->result = 0;
    
    return IARM_RESULT_SUCCESS;
}

void* WPEFramework::Plugin::DSPwrEventListener::PwrRetryEstablishConnThread(void* arg)
{
    DSLOG_INFO("Entry");
    DSPwrEventListener* listener = static_cast<DSPwrEventListener*>(arg);
    
    while (!listener->_stopThread) {
        // Check if PowerManager connection is successful
        if (listener->_powerManagerPlugin && listener->_registeredPowerEventHandler) {
            DSLOG_INFO("PowerManager connection is success");
            listener->PwrControllerFetchNinitStateValues();
            break;
        } else {
            // Retry PowerManager initialization after delay
            usleep(DSMGR_PWR_CNTRL_CONNECT_WAIT_TIME_MS);
            if (!listener->_stopThread) {
                listener->InitializePowerManager();
            }
        }
    }
    DSLOG_INFO("Completed Exit");
    return arg;
}

void* WPEFramework::Plugin::DSPwrEventListener::PwrEventHandlingThreadFunc(void* arg)
{
    DSLOG_INFO("Entry");
    DSPwrEventListener* listener = static_cast<DSPwrEventListener*>(arg);

    while (true) {
        pthread_mutex_lock(&listener->_pwrEventMutexLock);
        DSLOG_INFO("Wait for Events");
        
        pthread_mutex_lock(&listener->_pwrEventQueueMutexLock);
        bool queueEmpty = listener->_pwrEventQueue.empty();
        pthread_mutex_unlock(&listener->_pwrEventQueueMutexLock);
        
        while (!listener->_stopThread && queueEmpty) {
            pthread_cond_wait(&listener->_pwrEventMutexCond, &listener->_pwrEventMutexLock);
            pthread_mutex_lock(&listener->_pwrEventQueueMutexLock);
            queueEmpty = listener->_pwrEventQueue.empty();
            pthread_mutex_unlock(&listener->_pwrEventQueueMutexLock);
        }
        
        if (listener->_stopThread) {
            DSLOG_INFO("Exiting due to stop thread");
            pthread_mutex_unlock(&listener->_pwrEventMutexLock);
            break;
        }
        pthread_mutex_unlock(&listener->_pwrEventMutexLock);

        pthread_mutex_lock(&listener->_pwrEventQueueMutexLock);
        while (!listener->_pwrEventQueue.empty()) {
            DSMgr_Power_Event_State_t pwrEvent = listener->_pwrEventQueue.front();
            listener->_pwrEventQueue.pop();
            pthread_mutex_unlock(&listener->_pwrEventQueueMutexLock);
            
            listener->HandlePwrEventData(pwrEvent.currentState, pwrEvent.newState);
            
            pthread_mutex_lock(&listener->_pwrEventQueueMutexLock);
        }
        pthread_mutex_unlock(&listener->_pwrEventQueueMutexLock);
    }
    return arg;
}

IARM_Result_t WPEFramework::Plugin::DSPwrEventListener::SetAvPortState(void* arg) {
    
    if (nullptr == arg || nullptr == _instance) {
        return IARM_RESULT_INVALID_PARAM;
    }
    
    dsMgrAVPortStateParam_t* param = (dsMgrAVPortStateParam_t*)arg;
    PowerState powerState = _instance->PwrMgrToPowerControllerPowerState(param->avPortPowerState);
    
    if (PowerState::POWER_STATE_UNKNOWN != powerState) {
        _instance->SetAVPortsPowerState(powerState);
    }
    
    param->result = 0;
    return IARM_RESULT_SUCCESS;
}

IARM_Result_t WPEFramework::Plugin::DSPwrEventListener::SetLEDState(void* arg)
{
    if (NULL == arg || !_instance) {
        return IARM_RESULT_INVALID_PARAM;
    }
    
    dsMgrLEDStatusParam_t* param = (dsMgrLEDStatusParam_t*)arg;
    PowerState powerState = _instance->PwrMgrToPowerControllerPowerState(param->ledState);
    
    if (PowerState::POWER_STATE_UNKNOWN != powerState) {
        _instance->SetLEDStatus(powerState);
    }
    
    param->result = 0;
    return IARM_RESULT_SUCCESS;
}

IARM_Result_t WPEFramework::Plugin::DSPwrEventListener::SetRebootConfig(void* arg)
{
    if (NULL == arg) {
        return IARM_RESULT_INVALID_PARAM;
    }
    
    dsMgrRebootConfigParam_t* param = (dsMgrRebootConfigParam_t*)arg;
    param->reboot_reason_custom[sizeof(param->reboot_reason_custom) - 1] = '\0';
    
    if (nullptr != ux) {
        PowerState powerState = _instance->PwrMgrToPowerControllerPowerState(param->powerState);
        
        if (PowerState::POWER_STATE_UNKNOWN != powerState) {
            if (0 == strncmp(PWRMGR_REBOOT_REASON_MAINTENANCE, param->reboot_reason_custom, 
                            sizeof(param->reboot_reason_custom))) {
                ux->ApplyPreMaintenanceRebootConfig(powerState);
            } else {
                ux->ApplyPreRebootConfig(powerState);
            }
        }
    }
    
    param->result = 0;
    return IARM_RESULT_SUCCESS;
}

// DeviceSettings component methods (replacing legacy RPC calls)
uint32_t WPEFramework::Plugin::DSPwrEventListener::ConfigureVideoPort(const std::string& portName, VideoPortType portType, int index, bool requestEnable)
{
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;

    if (IsDeviceSettingsReady(true) == false) {
        DSLOG_ERR("DeviceSettings implementation not available");
        return result;
    }
    
    try {
        int32_t handle = 0;
        result = _deviceSettings->GetVideoPort(portType, index, handle);
        
        if (result == WPEFramework::Core::ERROR_NONE && handle != 0) {
            result = _deviceSettings->EnableVideoPort(handle, requestEnable);
            if (result == WPEFramework::Core::ERROR_NONE) {
                DSLOG_INFO("VideoPort %s successfully %s", portName.c_str(), (requestEnable ? "enabled" : "disabled"));
            } else {
                DSLOG_ERR("Failed to set video port %s state, Error: %d", portName.c_str(), result);
            }
        } else {
            DSLOG_ERR("Failed to get video port %s handle, Error: %d", portName.c_str(), result);
        }
    } catch (...) {
        DSLOG_ERR("Exception caught during ConfigureVideoPort for %s", portName.c_str());
        result = WPEFramework::Core::ERROR_GENERAL;
    }
    
    return result;
}

uint32_t WPEFramework::Plugin::DSPwrEventListener::ConfigureAudioPort(const std::string& portName, AudioPortType portType, int index, bool requestEnable, bool* isConfigurationSkippedPtr)
{
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    
    if (!isConfigurationSkippedPtr) {
        return WPEFramework::Core::ERROR_BAD_REQUEST;
    }
    
    *isConfigurationSkippedPtr = false;
    
    if (IsDeviceSettingsReady(true) == false) {
        DSLOG_ERR("DeviceSettings implementation not available");
        return result;
    }
    
    try {
        int32_t handle = 0;
        result = _deviceSettings->GetAudioPort(portType, index, handle);
        
        if (result == WPEFramework::Core::ERROR_NONE && handle != 0) {
            if (requestEnable) {
                // Check if port should be enabled based on persistent settings
                bool persistEnabled = true;
                result = _deviceSettings->IsAudioPortEnabled(handle, persistEnabled);
                if (result == WPEFramework::Core::ERROR_NONE) {
                    if (!persistEnabled) {
                        *isConfigurationSkippedPtr = true;
                        DSLOG_INFO("Enable AudioPort %s skipped - persistent state is disabled", portName.c_str());
                        return WPEFramework::Core::ERROR_NONE;
                    }
                }
            }
            
            result = _deviceSettings->EnableAudioPort(handle, requestEnable);
            if (result == WPEFramework::Core::ERROR_NONE) {
                DSLOG_INFO("AudioPort %s successfully %s", portName.c_str(), (requestEnable ? "enabled" : "disabled"));
            } else {
                DSLOG_ERR("Failed to set audio port %s state, Error: %d", portName.c_str(), result);
            }
        } else {
            DSLOG_ERR("Failed to get audio port %s handle, Error: %d", portName.c_str(), result);
        }
    } catch (...) {
        DSLOG_ERR("Exception caught during ConfigureAudioPort for %s", portName.c_str());
        result = WPEFramework::Core::ERROR_GENERAL;
    }
    
    return result;
}
