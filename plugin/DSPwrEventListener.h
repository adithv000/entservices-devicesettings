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

#pragma once

#include <queue>
#include <atomic>
#include <vector>
#include <unistd.h>
#include <pthread.h>
#include <interfaces/IPowerManager.h>
#include <wpeframework/helpers/PowerManagerInterface.h>
#include <wpeframework/helpers/DeviceSettingsInterface.h>
#include "Module.h"

#include "DeviceSettingsImplementation.h"
#include "DeviceSettingsTypes.h"

// C headers with built-in C++ protection
#include "libIARM.h"
#include "libIBusDaemon.h"
#include "sysMgr.h"
#include "libIBus.h"

using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;

namespace WPEFramework {
namespace Plugin {

/* Retry every 300 msec */
#define DSMGR_PWR_CNTRL_CONNECT_WAIT_TIME_MS   (300*1000)
#define MAX_NUM_VIDEO_PORTS 5
// DSMGR_MAX_VIDEO_PORT_NAME_LENGTH already defined in dsRpc.h

typedef struct{
    char port[DSMGR_MAX_VIDEO_PORT_NAME_LENGTH];
    bool isEnabled;
} DSMgr_Standby_Video_State_t;

/* Power Controller State Data Structure to Pass to the Thread */
struct DSMgr_Power_Event_State_t {
    PowerState currentState;
    PowerState newState;
    DSMgr_Power_Event_State_t(PowerState currSt, PowerState newSt)
        : currentState(currSt), newState(newSt) {}
};

class DSPwrEventListener;

class PowerManagerNotification : public Exchange::IPowerManager::IModeChangedNotification {
private:
    PowerManagerNotification(const PowerManagerNotification&) = delete;
    PowerManagerNotification& operator=(const PowerManagerNotification&) = delete;

public:
    explicit PowerManagerNotification(DSPwrEventListener& parent)
        : _parent(parent)
    {
    }
    ~PowerManagerNotification() override = default;

public:
    void OnPowerModeChanged(const PowerState currentState, const PowerState newState) override;

    template <typename T>
    T* baseInterface()
    {
        static_assert(std::is_base_of<T, PowerManagerNotification>(), "base type mismatch");
        return static_cast<T*>(this);
    }

    BEGIN_INTERFACE_MAP(PowerManagerNotification)
    INTERFACE_ENTRY(Exchange::IPowerManager::IModeChangedNotification)
    END_INTERFACE_MAP

private:
    DSPwrEventListener& _parent;
};

class DSPwrEventListener {
public:
    DSPwrEventListener();
    ~DSPwrEventListener();

    void Init(PluginHost::IShell* service);
    void Deinit();
    void InitPwrControllerEvt();
    void DeinitPwrControllerEvt();
    void onPowerModeChanged(const PowerState currentState, const PowerState newState);
    void registerPowerEventHandler();
    static DSPwrEventListener* GetInstance();
    int SetAVPortsPowerState(PowerState powerState);

private:
    using VideoPortEntry = WPEFramework::Plugin::VideoPortEntry;
    using AudioPortEntry = WPEFramework::Plugin::AudioPortEntry;

    static void* PwrEventHandlingThreadFunc(void* arg);
    static void* PwrRetryEstablishConnThread(void* arg);
    
    void PwrCtrlEstablishConnection();
    void InitializePowerManager();
    void PwrControllerFetchNinitStateValues();
    void HandlePwrEventData(const PowerState currentState,
                           const PowerState newState);

    bool IsDeviceSettingsReady(bool refreshCacheIfEmpty = true);
    void RefreshPortConfigurationCache();
    bool BuildVideoPortEntries(std::vector<VideoPortEntry>& entries);
    bool BuildAudioPortEntries(std::vector<AudioPortEntry>& entries);
    bool ResolveVideoPortEntryByName(const std::string& requestedPort, VideoPortEntry& resolvedEntry);
    
    int SetLEDStatus(PowerState powerState);
    
    // DeviceSettings integration methods
    uint32_t ConfigureVideoPort(const std::string& portName, VideoPortType portType, int index, bool enabled);
    uint32_t ConfigureAudioPort(const std::string& portName, AudioPortType portType, int index, bool enabled, bool* isConfigurationSkippedPtr);
    
    bool GetVideoPortStandbySetting(const char* port);
    
    // IARM API handlers
    static IARM_Result_t SetStandbyVideoState(void* arg);
    static IARM_Result_t GetStandbyVideoState(void* arg);
    static IARM_Result_t SetAvPortState(void* arg);
    static IARM_Result_t SetLEDState(void* arg);
    static IARM_Result_t SetRebootConfig(void* arg);
    
    PowerState PwrMgrToPowerControllerPowerState(int pwrMgrState);
    
    //static PowerState PwrMgrToPowerControllerPowerState(int pwrMgrState);

private:
    static DSPwrEventListener* _instance;
    
    std::queue<DSMgr_Power_Event_State_t> _pwrEventQueue;
    pthread_t _pwrEventHandlerThreadID;
    pthread_t _pwrConnectThreadID;
    pthread_mutex_t _pwrEventMutexLock;
    pthread_cond_t _pwrEventMutexCond;
    pthread_mutex_t _pwrEventQueueMutexLock;
    std::atomic<bool> _stopThread;
    bool _initialized;
    bool _pwrConnectThreadStarted;
    bool _registeredPowerEventHandler;
    
    PowerState _curState;
    DSMgr_Standby_Video_State_t _standbyVideoPortSetting[MAX_NUM_VIDEO_PORTS];
    
    PowerManagerInterfaceRef _powerManagerPlugin;
    Core::Sink<PowerManagerNotification> _pwrMgrNotification;
    PluginHost::IShell* _service;
    DeviceSettingsImp* _deviceSettings;
    /** Cached video port entries — populated by RefreshPortConfigurationCache() from GetDeviceSettingConfigs(). */
    std::vector<VideoPortEntry> _videoPortEntries;
    /** Cached audio port entries — populated by RefreshPortConfigurationCache() from GetDeviceSettingConfigs(). */
    std::vector<AudioPortEntry> _audioPortEntries;
};

} // namespace Plugin
} // namespace WPEFramework
