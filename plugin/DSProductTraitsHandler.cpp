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

#include "DSProductTraitsHandler.h"
#include "DSPwrEventListener.h"
#include "DeviceSettingsTypes.h"
#include "DeviceSettingsImplementation.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <utility>
#include <unistd.h>

namespace WPEFramework {
namespace Plugin {
namespace DSProductTraits {

// LambdaJob helper for WPEFramework timer callbacks
class LambdaJob : public Core::IDispatch {
public:
    LambdaJob(std::function<void()> job) : _job(std::move(job)) {}
    void Dispatch() override { _job(); }
private:
    std::function<void()> _job;
};

const unsigned int REBOOT_REASON_RETRY_INTERVAL_SECONDS = 2;
UXController* UXController::_singleton = nullptr;

static reboot_type_t GetRebootType()
{
    const char* file_updated_flag = "/tmp/Update_rebootInfo_invoked";
    const char* reboot_info_file_name = "/opt/secure/reboot/previousreboot.info";
    const char* hard_reboot_match_string = R"("reason":"POWER_ON_RESET")";
    reboot_type_t ret = reboot_type_t::SOFT;

    if (0 != access(file_updated_flag, F_OK)) {
        DSLOG_INFO("Error! Reboot info file isn't updated yet");
        ret = reboot_type_t::UNAVAILABLE;
    } else {
        std::ifstream reboot_info_file(reboot_info_file_name);
        std::string line;
        if (true == reboot_info_file.is_open()) {
            while (std::getline(reboot_info_file, line)) {
                if (std::string::npos != line.find(hard_reboot_match_string)) {
                    DSLOG_INFO("Detected hard reboot");
                    ret = reboot_type_t::HARD;
                    break;
                }
            }
        } else {
            DSLOG_INFO("Failed to open reboot info file");
        }
    }
    return ret;
}

static void ScheduleRebootReasonCheck(UXController* controller, unsigned int retryCount = 0)
{
    constexpr unsigned int max_count = 120 / REBOOT_REASON_RETRY_INTERVAL_SECONDS;
    
    reboot_type_t reboot_type = GetRebootType();
    if (reboot_type_t::UNAVAILABLE == reboot_type) {
        if (retryCount < max_count) {
            // Schedule next retry using a separate thread (mimics g_timeout_add_seconds)
            std::thread retryThread([controller, retryCount]() {
                std::this_thread::sleep_for(std::chrono::seconds(REBOOT_REASON_RETRY_INTERVAL_SECONDS));
                ScheduleRebootReasonCheck(controller, retryCount + 1);
            });
            retryThread.detach();
        } else {
            DSLOG_INFO("Exceeded retry limit");
        }
    } else {
        DSLOG_INFO("Got reboot reason in async check. Applying display configuration");
        controller->SyncDisplayPortsWithRebootReason(reboot_type);
    }
}

static inline bool DoForceDisplayOnPostReboot()
{
    const char* flag_filename = "/opt/force_display_on_after_reboot";
    bool ret = false;
    if (0 == access(flag_filename, F_OK)) {
        ret = true;
    }
    DSLOG_INFO("ForceDisplay: %s", (true == ret ? "true" : "false"));
    return ret;
}

/********************************* UXController Base Class ********************************/

UXController::UXController(unsigned int id, const std::string& name, deviceType_t deviceType)
    : _id(id)
    , _name(name)
    , _deviceType(deviceType)
    , _invalidateAsyncBootloaderPattern(false)
    , _firstPowerTransitionComplete(false)
    , _deviceSettings(nullptr)
{
    DSLOG_INFO("initializing for profile id %d, name %s", id, name.c_str());
    
    // Get DeviceSettings implementation instance
    _deviceSettings = DeviceSettingsImp::instance();
    if (!_deviceSettings) {
        DSLOG_ERR("Failed to get DeviceSettings implementation instance");
    }
    
    InitializeSafeDefaults();
}

void UXController::InitializeSafeDefaults()
{
    _enableMultiColourLedSupport = false;
    _enableSilentRebootSupport = true;
    _preferedPowerModeOnReboot = POWER_MODE_LAST_KNOWN;
    _invalidateAsyncBootloaderPattern = false;
    _firstPowerTransitionComplete = false;
    _ledColorInOnState = 0;
    _ledColorInStandby = 0;

    if (DEVICE_TYPE_STB == _deviceType) {
        _ledEnabledInStandby = false;
        _ledEnabledInOnState = true;
    } else {
        _ledEnabledInStandby = true;
        _ledEnabledInOnState = false;
    }
}

bool UXController::SetBootloaderPatternInternal(mfrBlPattern_t pattern)
{
    _mutex.lock();
    _invalidateAsyncBootloaderPattern = true;
    _mutex.unlock();
    return SetBootloaderPattern(pattern);
}

bool UXController::SetBootloaderPattern(mfrBlPattern_t pattern) const
{
    bool ret = true;

    if (false == _enableSilentRebootSupport) {
        return true;
    }

    IARM_Bus_MFRLib_SetBLPattern_Param_t mfrparam;
    mfrparam.pattern = pattern;
    if (IARM_RESULT_SUCCESS != IARM_Bus_Call(IARM_BUS_MFRLIB_NAME, IARM_BUS_MFRLIB_API_SetBootLoaderPattern, 
                                             (void*)&mfrparam, sizeof(mfrparam))) {
        DSLOG_INFO("Warning! Call to SetBootLoaderPattern failed");
        ret = false;
    } else {
        DSLOG_INFO("Successfully set bootloader pattern %d", (int)pattern);
    }
    return ret;
}

void UXController::SetBootloaderPatternAsync(mfrBlPattern_t pattern) const
{
    bool ret = true;
    const unsigned int retry_interval_seconds = 5;
    unsigned int remaining_retries = 12;
    
    DSLOG_INFO("start for pattern 0x%x", pattern);
    do {
        std::this_thread::sleep_for(std::chrono::seconds(retry_interval_seconds));
        std::unique_lock<std::mutex> lock(_mutex);
        if (false == _invalidateAsyncBootloaderPattern) {
            ret = SetBootloaderPattern(pattern);
        } else {
            DSLOG_INFO("Bootloader pattern invalidated. Aborting");
            break;
        }
    } while ((false == ret) && (0 < --remaining_retries));

    DSLOG_INFO("returns");
}

bool UXController::SetBootloaderPatternFaultTolerant(mfrBlPattern_t pattern)
{
    bool ret = true;
    ret = SetBootloaderPattern(pattern);
    if (false == ret) {
        _mutex.lock();
        _invalidateAsyncBootloaderPattern = false;
        _mutex.unlock();
        std::thread retry_thread(&UXController::SetBootloaderPatternAsync, this, pattern);
        retry_thread.detach();
    }
    return ret;
}

void UXController::SyncPowerLedWithPowerState(PowerState power_state) const
{
    if (true == _enableMultiColourLedSupport) {
        DSLOG_INFO("Warning! Device supports multi-colour LEDs but it isn't handled");
    }

    bool led_state;
    if (WPEFramework::Exchange::IPowerManager::POWER_STATE_ON == power_state) {
        led_state = _ledEnabledInOnState;
    } else {
        led_state = _ledEnabledInStandby;
    }

    try {
        DSLOG_INFO("Setting power LED State to %s", (led_state ? "ON" : "OFF"));
        
        if (_deviceSettings) {
            FPDIndicator indicator = static_cast<FPDIndicator>(dsFPD_INDICATOR_POWER);
            FPDState fpdState = (led_state ? FPDState::DS_FPD_STATE_ON : FPDState::DS_FPD_STATE_OFF);
            
            uint32_t result = _deviceSettings->SetFPDState(indicator, fpdState);
            if (result != WPEFramework::Core::ERROR_NONE) {
                DSLOG_ERR("SetFPDState failed with error: %d", result);
            } else {
                DSLOG_INFO("Successfully set FPD power state to %s", (led_state ? "ON" : "OFF"));
            }
        } else {
            DSLOG_ERR("DeviceSettings implementation not available");
        }
    } catch (...) {
        DSLOG_ERR("Warning! exception caught when trying to change FP state");
    }
}

void UXController::SyncDisplayPortsWithPowerState(PowerState power_state) const
{
    DSLOG_INFO("PowerState: %d", static_cast<int>(power_state));

    DSPwrEventListener* listener = DSPwrEventListener::GetInstance();
    if (listener != nullptr) {
        listener->SetAVPortsPowerState(power_state);
    } else {
        DSLOG_ERR("DSPwrEventListener instance not available");
    }
}

bool UXController::Initialize(unsigned int profile_id)
{
    bool ret = true;
    
    switch (profile_id) {
        case DEFAULT_STB_PROFILE:
            _singleton = new UXControllerStb(profile_id, "default-stb");
            break;

        case DEFAULT_TV_PROFILE:
            _singleton = new UXControllerTv(profile_id, "default-tv");
            break;

        case DEFAULT_STB_PROFILE_EUROPE:
            _singleton = new UXControllerStbEu(profile_id, "default-stb-eu");
            break;

        case DEFAULT_TV_PROFILE_EUROPE:
            _singleton = new UXControllerTvEu(profile_id, "default-tv-eu");
            break;

        default:
            DSLOG_ERR("Error! Unsupported product profile id %d", profile_id);
            ret = false;
    }
    return ret;
}

UXController* UXController::GetInstance()
{
    return _singleton;
}

/********************************* UXControllerTvEu Class ********************************/

UXControllerTvEu::UXControllerTvEu(unsigned int id, const std::string& name)
    : UXController(id, name, DEVICE_TYPE_TV)
{
    _preferedPowerModeOnReboot = POWER_MODE_LIGHT_SLEEP;
}

bool UXControllerTvEu::ApplyPowerStateChangeConfig(PowerState newState, 
                                                   PowerState prevState)
{
    bool ret = true;
    SyncDisplayPortsWithPowerState(newState);
    ret = SetBootloaderPatternInternal((WPEFramework::Exchange::IPowerManager::POWER_STATE_ON == newState ? mfrBL_PATTERN_NORMAL : mfrBL_PATTERN_SILENT_LED_ON));
    return ret;
}

bool UXControllerTvEu::ApplyPreRebootConfig(PowerState currentState) const
{
    return true;
}

bool UXControllerTvEu::ApplyPreMaintenanceRebootConfig(PowerState currentState)
{
    bool ret = true;
    if (WPEFramework::Exchange::IPowerManager::POWER_STATE_ON != currentState) {
        ret = SetBootloaderPatternInternal(mfrBL_PATTERN_SILENT);
    }
    return ret;
}

bool UXControllerTvEu::ApplyPostRebootConfig(PowerState targetState, 
                                            PowerState lastKnownState)
{
    bool ret = true;
    
    if ((WPEFramework::Exchange::IPowerManager::POWER_STATE_ON == lastKnownState) && (WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY == targetState)) {
        SyncDisplayPortsWithPowerState(WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
    } else {
        SyncDisplayPortsWithPowerState(targetState);
    }

    mfrBlPattern_t pattern = mfrBL_PATTERN_NORMAL;
    switch (targetState) {
        case WPEFramework::Exchange::IPowerManager::POWER_STATE_ON:
            break;
        case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY:
        case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_LIGHT_SLEEP:
        case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP:
            pattern = mfrBL_PATTERN_SILENT_LED_ON;
            break;
        default:
            DSLOG_INFO("Warning! Unhandled power transition. New state: %d", targetState);
            break;
    }
    ret = SetBootloaderPatternFaultTolerant(pattern);
    return ret;
}

PowerState UXControllerTvEu::GetPreferredPostRebootPowerState(
    PowerState prevState) const
{
    return WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY;
}

/********************************* UXControllerStbEu Class ********************************/

UXControllerStbEu::UXControllerStbEu(unsigned int id, const std::string& name)
    : UXController(id, name, DEVICE_TYPE_STB)
{
    _preferedPowerModeOnReboot = POWER_MODE_LIGHT_SLEEP;
}

bool UXControllerStbEu::ApplyPowerStateChangeConfig(PowerState newState, 
                                                    PowerState prevState)
{
    SyncDisplayPortsWithPowerState(newState);
    SyncPowerLedWithPowerState(newState);
    return true;
}

bool UXControllerStbEu::ApplyPreRebootConfig(PowerState currentState) const
{
    return true;
}

bool UXControllerStbEu::ApplyPreMaintenanceRebootConfig(PowerState currentState)
{
    bool ret = true;
    if (WPEFramework::Exchange::IPowerManager::POWER_STATE_ON != currentState) {
        ret = SetBootloaderPatternInternal(mfrBL_PATTERN_SILENT);
    }
    return ret;
}

bool UXControllerStbEu::ApplyPostRebootConfig(PowerState targetState, 
                                             PowerState lastKnownState)
{
    bool ret = true;
    
    if ((WPEFramework::Exchange::IPowerManager::POWER_STATE_ON == lastKnownState) && (WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY == targetState)) {
/* Sync bootup LEDs is disabling the bootup LED pattern.
Now the LED pattern is set by IUI after bootup we modifying this behavior based on conditions */
#ifdef ENABLE_LED_SYNC_IN_BOOTUP
        SyncPowerLedWithPowerState(WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
#endif
        SyncDisplayPortsWithPowerState(WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
    } else {
#ifdef ENABLE_LED_SYNC_IN_BOOTUP
        SyncPowerLedWithPowerState(targetState);
#endif
        SyncDisplayPortsWithPowerState(targetState);
    }

    ret = SetBootloaderPatternFaultTolerant(mfrBL_PATTERN_NORMAL);
    return ret;
}

PowerState UXControllerStbEu::GetPreferredPostRebootPowerState(
    PowerState prevState) const
{
    return WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY;
}

/********************************* UXControllerTv Class ********************************/

UXControllerTv::UXControllerTv(unsigned int id, const std::string& name)
    : UXController(id, name, DEVICE_TYPE_TV)
{
    _preferedPowerModeOnReboot = POWER_MODE_LIGHT_SLEEP;
}

bool UXControllerTv::ApplyPowerStateChangeConfig(PowerState newState, 
                                                 PowerState prevState)
{
    _mutex.lock();
    if (false == _firstPowerTransitionComplete) {
        _firstPowerTransitionComplete = true;
    }
    _mutex.unlock();

    SyncDisplayPortsWithPowerState(newState);
    bool ret = SetBootloaderPatternInternal((WPEFramework::Exchange::IPowerManager::POWER_STATE_ON == newState ? mfrBL_PATTERN_NORMAL : mfrBL_PATTERN_SILENT_LED_ON));
    return ret;
}

bool UXControllerTv::ApplyPreRebootConfig(PowerState currentState) const
{
    return true;
}

bool UXControllerTv::ApplyPreMaintenanceRebootConfig(PowerState currentState)
{
    bool ret = true;
    if (WPEFramework::Exchange::IPowerManager::POWER_STATE_ON != currentState) {
        ret = SetBootloaderPatternInternal(mfrBL_PATTERN_SILENT_LED_ON);
    }
    return ret;
}

bool UXControllerTv::ApplyPostRebootConfig(PowerState targetState, 
                                          PowerState lastKnownState)
{
    bool ret = true;
#ifdef ENABLE_LED_SYNC_IN_BOOTUP
    SyncPowerLedWithPowerState(targetState);
#endif
    if ((WPEFramework::Exchange::IPowerManager::POWER_STATE_ON == lastKnownState) && (WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY == targetState)) {
        SyncDisplayPortsWithPowerState(WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
    } else {
        SyncDisplayPortsWithPowerState(targetState);
    }

    mfrBlPattern_t pattern = mfrBL_PATTERN_NORMAL;
    switch (targetState) {
        case WPEFramework::Exchange::IPowerManager::POWER_STATE_ON:
            break;
        case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY:
        case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_LIGHT_SLEEP:
        case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP:
            pattern = mfrBL_PATTERN_SILENT_LED_ON;
            break;
        default:
            DSLOG_INFO("Warning! Unhandled power transition. New state: %d", targetState);
            break;
    }
    ret = SetBootloaderPatternFaultTolerant(pattern);
    return ret;
}

PowerState UXControllerTv::GetPreferredPostRebootPowerState(
    PowerState prevState) const
{
    return WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY;
}

void UXControllerTv::SyncDisplayPortsWithRebootReason(reboot_type_t reboot_type)
{
    _mutex.lock();
    if (false == _firstPowerTransitionComplete) {
        _mutex.unlock();
        SyncDisplayPortsWithPowerState(reboot_type_t::HARD == reboot_type ? WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY : WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
    } else {
        _mutex.unlock();
    }
}

/********************************* UXControllerStb Class ********************************/

UXControllerStb::UXControllerStb(unsigned int id, const std::string& name)
    : UXController(id, name, DEVICE_TYPE_STB)
{
    _preferedPowerModeOnReboot = POWER_MODE_LAST_KNOWN;
    _enableSilentRebootSupport = false;
}

bool UXControllerStb::ApplyPowerStateChangeConfig(PowerState newState, 
                                                  PowerState prevState)
{
    SyncDisplayPortsWithPowerState(newState);
    SyncPowerLedWithPowerState(newState);
    return true;
}

bool UXControllerStb::ApplyPreRebootConfig(PowerState currentState) const
{
    return true;
}

bool UXControllerStb::ApplyPreMaintenanceRebootConfig(PowerState currentState)
{
    return true;
}

bool UXControllerStb::ApplyPostRebootConfig(PowerState targetState, 
                                           PowerState lastKnownState)
{
    bool ret = true;
    SyncPowerLedWithPowerState(targetState);
    SyncDisplayPortsWithPowerState(targetState);
    return ret;
}

PowerState UXControllerStb::GetPreferredPostRebootPowerState(
    PowerState prevState) const
{
    return prevState;
}

} // namespace DSProductTraits
} // namespace Plugin
} // namespace WPEFramework
