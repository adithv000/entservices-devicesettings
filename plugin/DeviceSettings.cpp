/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
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
**/

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h> 
#include <iostream>
#include <fstream>

#include "DeviceSettings.h"
#include <interfaces/IConfiguration.h>
#include <chrono>

namespace WPEFramework {

namespace Plugin
{
    SERVICE_REGISTRATION(DeviceSettings, API_VERSION_MAJOR, API_VERSION_MINOR, API_VERSION_PATCH);

    namespace {
        static Metadata<DeviceSettings> metadata(
            // Version
            API_VERSION_MAJOR, API_VERSION_MINOR, API_VERSION_PATCH,
            // Preconditions
            {},
            // Terminations
            {},
            // Controls
            {}
        );
    }

    DeviceSettings::DeviceSettings()
        : mConstructionStart(std::chrono::steady_clock::now())
        , mConnectionId(0)
        , mService(nullptr)
        , _mDeviceSettings(nullptr)
        , _mDeviceSettingsCompositeIn(nullptr)
        , _mDeviceSettingsAudio(nullptr)
        , _mDeviceSettingsFPD(nullptr)
        , _mDeviceSettingsDisplay(nullptr)
        , _mDeviceSettingsHDMIIn(nullptr)
        , _mDeviceSettingsHost(nullptr)
        , _mDeviceSettingsVideoPort(nullptr)
        , _mDeviceSettingsVideoDevice(nullptr)
        , mNotificationSink(this)

    {
        #if (defined(RDK_LOGGER_ENABLED) || defined(DSMGR_LOGGER_ENABLED))

        const char* PdebugConfigFile = NULL;
        const char* DSMGR_DEBUG_ACTUAL_PATH    = "/etc/debug.ini";
        const char* DSMGR_DEBUG_OVERRIDE_PATH  = "/opt/debug.ini";

        /* Init the logger */
        if (access(DSMGR_DEBUG_OVERRIDE_PATH, F_OK) != -1 ) {
            PdebugConfigFile = DSMGR_DEBUG_OVERRIDE_PATH;
        }
        else {
            PdebugConfigFile = DSMGR_DEBUG_ACTUAL_PATH;
        }

        if (rdk_logger_init(PdebugConfigFile) == 0) {
            b_rdk_logger_enabled = 1;
        }

#endif

        DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "DeviceSettings constructor",
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - mConstructionStart).count());
    }

    
    DeviceSettings::~DeviceSettings()
    {
    }
    const string DeviceSettings::Initialize(PluginHost::IShell * service)
    {
        string message = "";
        using Clock = std::chrono::steady_clock;
        using Ms    = std::chrono::milliseconds;
        auto tInit  = Clock::now();
        DSLOG_INFO("[DS-INIT-TIMING] DeviceSettings::Initialize — begin");
        DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "Constructor to Initialize begin",
            (long long)std::chrono::duration_cast<Ms>(tInit - mConstructionStart).count());

        ASSERT(service != nullptr);
        ASSERT(mService == nullptr);
        ASSERT(mConnectionId == 0);
        ASSERT(_mDeviceSettings == nullptr);
        ASSERT(_mDeviceSettingsFPD == nullptr);
        ASSERT(_mDeviceSettingsHDMIIn == nullptr);
        ASSERT(_mDeviceSettingsVideoPort == nullptr);
        ASSERT(_mDeviceSettingsVideoDevice == nullptr);
        ASSERT(_mDeviceSettingsHost == nullptr);
        ASSERT(_mDeviceSettingsCompositeIn == nullptr);
        mService = service;
        mService->AddRef();

        mService->Register(mNotificationSink.baseInterface<RPC::IRemoteConnection::INotification>());
        mService->Register(mNotificationSink.baseInterface<PluginHost::IShell::ICOMLink::INotification>());

#ifdef USE_LEGACY_INTERFACE
        // Get IDeviceSettingsFPD interface.
        // Get the unified interface that provides both FPD and HDMI functionality
        {
            auto t0 = Clock::now();
            _mDeviceSettings = service->Root<Exchange::IDeviceSettings>(mConnectionId, RPC::CommunicationTimeOut, _T("DeviceSettingsImp"));
            DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "service->Root (DeviceSettingsImp)",
                    (long long)std::chrono::duration_cast<Ms>(Clock::now() - t0).count());
        }

        if (_mDeviceSettings == nullptr) {
            DSLOG_ERR("Failed to initialise DeviceSettings plugin");
            message = _T("DeviceSettings plugin could not be initialised");
            DSLOG_ERR("Failed to get IDeviceSettings interface");
        } else {
            DSLOG_INFO("DeviceSettingsImp initialized successfully");
            
            // Call Configure method on DeviceSettingsImp with the service
            auto tCfg = Clock::now();
            Core::hresult result = _mDeviceSettings->Configure(service);
            DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "Configure(service) [legacy]",
                    (long long)std::chrono::duration_cast<Ms>(Clock::now() - tCfg).count());
            if (result != Core::ERROR_NONE) {
                DSLOG_ERR("Failed to configure DeviceSettings: %d", result);
                message = _T("DeviceSettings configuration failed");
            } else {
                // Initialize individual interface pointers for external COM-RPC access
                auto tQI = Clock::now();
                _mDeviceSettingsFPD = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsFPD>();
                if (_mDeviceSettingsFPD == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsFPD interface for external access");
                }
                
                _mDeviceSettingsHDMIIn = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsHDMIIn>();
                if (_mDeviceSettingsHDMIIn == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsHDMIIn interface for external access");
                }

                _mDeviceSettingsAudio = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsAudio>();
                if (_mDeviceSettingsAudio == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsAudio interface for external access");
                }

                _mDeviceSettingsVideoPort = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
                _mDeviceSettingsVideoDevice = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsVideoDevice>();
                _mDeviceSettingsHost = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsHost>();
                _mDeviceSettingsCompositeIn = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsCompositeIn>();
                _mDeviceSettingsDisplay = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsDisplay>();
                if (_mDeviceSettingsVideoPort == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsVideoPort interface for external access");
                }
                if (_mDeviceSettingsVideoDevice == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsVideoDevice interface for external access");
                }
                if (_mDeviceSettingsHost == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsHost interface for external access");
                }
                if (_mDeviceSettingsCompositeIn == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsCompositeIn interface for external access");
                }
                if (_mDeviceSettingsDisplay == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsDisplay interface for external access");
                }
                DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "QueryInterface x8 [legacy]",
                        (long long)std::chrono::duration_cast<Ms>(Clock::now() - tQI).count());

                DSLOG_INFO("Individual interfaces initialized for external access - FPD: %p, HDMIIn: %p, Audio: %p, VideoPort: %p, VideoDevice: %p, Host: %p, CompositeIn: %p, Display: %p",
                       _mDeviceSettingsFPD, _mDeviceSettingsHDMIIn, _mDeviceSettingsAudio, _mDeviceSettingsVideoPort, _mDeviceSettingsVideoDevice, _mDeviceSettingsHost, _mDeviceSettingsCompositeIn, _mDeviceSettingsDisplay);
                
                // Register for HDMIIn event notifications
                if (_mDeviceSettingsHDMIIn != nullptr) {
                    _mDeviceSettingsHDMIIn->Register("DeviceSettings", mNotificationSink.baseInterface<Exchange::IDeviceSettingsHDMIIn::INotification>());
                    DSLOG_INFO("Registered for HDMIIn event notifications");
                }
                
                // Register for VideoPort event notifications
                if (_mDeviceSettingsVideoPort != nullptr) {
                    _mDeviceSettingsVideoPort->Register("DeviceSettings", mNotificationSink.baseInterface<Exchange::IDeviceSettingsVideoPort::INotification>());
                    DSLOG_INFO("Registered for VideoPort event notifications");
                }
                
                // Register for VideoDevice event notifications
                if (_mDeviceSettingsVideoDevice != nullptr) {
                    _mDeviceSettingsVideoDevice->Register("DeviceSettings", mNotificationSink.baseInterface<Exchange::IDeviceSettingsVideoDevice::INotification>());
                    DSLOG_INFO("Registered for VideoDevice event notifications");
                }
                
                // Register for CompositeIn event notifications
                if (_mDeviceSettingsCompositeIn != nullptr) {
                    _mDeviceSettingsCompositeIn->Register("DeviceSettings", mNotificationSink.baseInterface<Exchange::IDeviceSettingsCompositeIn::INotification>());
                    DSLOG_INFO("Registered for CompositeIn event notifications");
                }
                
                // Register for Display event notifications
                if (_mDeviceSettingsDisplay != nullptr) {
                    _mDeviceSettingsDisplay->Register("DeviceSettings", mNotificationSink.baseInterface<IDisplayNotification>());
                    DSLOG_INFO("Registered for Display event notifications");
                }
            }
        }
#else
        // Get the unified interface that provides both FPD and HDMI functionality
        {
            auto t0 = Clock::now();
            _mDeviceSettings = service->Root<Exchange::IDeviceSettings>(mConnectionId, RPC::CommunicationTimeOut, _T("DeviceSettingsImp"));
            DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "service->Root (DeviceSettingsImp)",
                    (long long)std::chrono::duration_cast<Ms>(Clock::now() - t0).count());
        }

        if (_mDeviceSettings == nullptr) {
            DSLOG_ERR("Failed to initialise DeviceSettings plugin");
            message = _T("DeviceSettings plugin could not be initialised");
            DSLOG_ERR("Failed to get IDeviceSettings interface");
        } else {
            DSLOG_INFO("DeviceSettingsImp initialized successfully");
            
            // Call Configure method on DeviceSettingsImp with the service
            auto tCfg = Clock::now();
            Core::hresult result = _mDeviceSettings->Configure(service);
            DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "Configure(service)",
                    (long long)std::chrono::duration_cast<Ms>(Clock::now() - tCfg).count());
            if (result != Core::ERROR_NONE) {
                DSLOG_ERR("Failed to configure DeviceSettings: %d", result);
                message = _T("DeviceSettings configuration failed");
            } else {
                // Initialize individual interface pointers for external COM-RPC access
                auto tQI = Clock::now();
                _mDeviceSettingsFPD = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsFPD>();
                if (_mDeviceSettingsFPD == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsFPD interface for external access");
                }
                
                _mDeviceSettingsHDMIIn = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsHDMIIn>();
                if (_mDeviceSettingsHDMIIn == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsHDMIIn interface for external access");
                }

                _mDeviceSettingsCompositeIn = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsCompositeIn>();
                _mDeviceSettingsAudio = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsAudio>();
                _mDeviceSettingsVideoPort = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
                _mDeviceSettingsVideoDevice = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsVideoDevice>();
                _mDeviceSettingsHost = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsHost>();
                _mDeviceSettingsDisplay = _mDeviceSettings->QueryInterface<Exchange::IDeviceSettingsDisplay>();
                if (_mDeviceSettingsCompositeIn == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsCompositeIn interface for external access");
                }
                if (_mDeviceSettingsAudio == nullptr) {
                    DSLOG_ERR("Failed to get DeviceSettingsAudio interface for external access");
                }
                if (_mDeviceSettingsVideoPort == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsVideoPort interface for external access");
                }
                if (_mDeviceSettingsVideoDevice == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsVideoDevice interface for external access");
                }
                if (_mDeviceSettingsHost == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsHost interface for external access");
                }
                if (_mDeviceSettingsDisplay == nullptr) {
                    DSLOG_ERR("Failed to get IDeviceSettingsDisplay interface for external access");
                }
                DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "QueryInterface x8",
                        (long long)std::chrono::duration_cast<Ms>(Clock::now() - tQI).count());

                DSLOG_INFO("Individual interfaces initialized for external access - FPD: %p, HDMIIn: %p, CompositeIn: %p, Audio: %p, VideoPort: %p, VideoDevice: %p, Host: %p, Display: %p",
                       _mDeviceSettingsFPD, _mDeviceSettingsHDMIIn, _mDeviceSettingsCompositeIn, _mDeviceSettingsAudio, _mDeviceSettingsVideoPort, _mDeviceSettingsVideoDevice, _mDeviceSettingsHost, _mDeviceSettingsDisplay);
                
                // Register for HDMIIn event notifications
                if (_mDeviceSettingsHDMIIn != nullptr) {
                    _mDeviceSettingsHDMIIn->Register("DeviceSettings", mNotificationSink.baseInterface<Exchange::IDeviceSettingsHDMIIn::INotification>());
                    DSLOG_INFO("Registered for HDMIIn event notifications");
                }
                
                // Register for VideoPort event notifications
                if (_mDeviceSettingsVideoPort != nullptr) {
                    _mDeviceSettingsVideoPort->Register("DeviceSettings", mNotificationSink.baseInterface<Exchange::IDeviceSettingsVideoPort::INotification>());
                    DSLOG_INFO("Registered for VideoPort event notifications");
                }
                
                // Register for VideoDevice event notifications
                if (_mDeviceSettingsVideoDevice != nullptr) {
                    _mDeviceSettingsVideoDevice->Register("DeviceSettings", mNotificationSink.baseInterface<Exchange::IDeviceSettingsVideoDevice::INotification>());
                    DSLOG_INFO("Registered for VideoDevice event notifications");
                }
                
                // Register for CompositeIn event notifications
                if (_mDeviceSettingsCompositeIn != nullptr) {
                    _mDeviceSettingsCompositeIn->Register("DeviceSettings", mNotificationSink.baseInterface<Exchange::IDeviceSettingsCompositeIn::INotification>());
                    DSLOG_INFO("Registered for CompositeIn event notifications");
                }
                
                // Register for Display event notifications
                if (_mDeviceSettingsDisplay != nullptr) {
                    _mDeviceSettingsDisplay->Register("DeviceSettings", mNotificationSink.baseInterface<IDisplayNotification>());
                    DSLOG_INFO("Registered for Display event notifications");
                }
            }
        }
#endif
        if (0 != message.length()) {
            Deinitialize(service);
        }

        DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "DeviceSettings::Initialize TOTAL",
                (long long)std::chrono::duration_cast<Ms>(Clock::now() - tInit).count());
        DSLOG_INFO("[DS-INIT-TIMING] %-28s : %6lld ms", "Constructor to Initialize end",
            (long long)std::chrono::duration_cast<Ms>(Clock::now() - mConstructionStart).count());
        // On success return empty, to indicate there is no error text.
        return (message);
    }

    void DeviceSettings::Deinitialize(PluginHost::IShell* service VARIABLE_IS_NOT_USED)
    {
        if (mService != nullptr) {
            ASSERT(mService == service);
            mService->Unregister(mNotificationSink.baseInterface<RPC::IRemoteConnection::INotification>());
            mService->Unregister(mNotificationSink.baseInterface<PluginHost::IShell::ICOMLink::INotification>());
            
            // Unregister from event notifications before releasing interfaces
            if (_mDeviceSettingsHDMIIn != nullptr) {
                _mDeviceSettingsHDMIIn->Unregister(mNotificationSink.baseInterface<Exchange::IDeviceSettingsHDMIIn::INotification>());
                DSLOG_INFO("Unregistered from HDMIIn event notifications");
            }
            
            if (_mDeviceSettingsVideoPort != nullptr) {
                _mDeviceSettingsVideoPort->Unregister(mNotificationSink.baseInterface<Exchange::IDeviceSettingsVideoPort::INotification>());
                DSLOG_INFO("Unregistered from VideoPort event notifications");
            }
            
            if (_mDeviceSettingsVideoDevice != nullptr) {
                _mDeviceSettingsVideoDevice->Unregister(mNotificationSink.baseInterface<Exchange::IDeviceSettingsVideoDevice::INotification>());
                DSLOG_INFO("Unregistered from VideoDevice event notifications");
            }
            
            if (_mDeviceSettingsCompositeIn != nullptr) {
                _mDeviceSettingsCompositeIn->Unregister(mNotificationSink.baseInterface<Exchange::IDeviceSettingsCompositeIn::INotification>());
                DSLOG_INFO("Unregistered from CompositeIn event notifications");
            }
            
            if (_mDeviceSettingsDisplay != nullptr) {
                _mDeviceSettingsDisplay->Unregister(mNotificationSink.baseInterface<IDisplayNotification>());
                DSLOG_INFO("Unregistered from Display event notifications");
            }
            
            // Release individual interface pointers
            if (_mDeviceSettingsFPD != nullptr) {
                _mDeviceSettingsFPD->Release();
                _mDeviceSettingsFPD = nullptr;
            }
            
            if (_mDeviceSettingsHDMIIn != nullptr) {
                _mDeviceSettingsHDMIIn->Release();
                _mDeviceSettingsHDMIIn = nullptr;
            }
            
            if (_mDeviceSettingsCompositeIn != nullptr) {
                _mDeviceSettingsCompositeIn->Release();
                _mDeviceSettingsCompositeIn = nullptr;
            }
            
            if (_mDeviceSettingsAudio != nullptr) {
                _mDeviceSettingsAudio->Release();
                _mDeviceSettingsAudio = nullptr;
            }
            
            if (_mDeviceSettingsVideoPort != nullptr) {
                _mDeviceSettingsVideoPort->Release();
                _mDeviceSettingsVideoPort = nullptr;
            }
            if (_mDeviceSettingsVideoDevice != nullptr) {
                _mDeviceSettingsVideoDevice->Release();
                _mDeviceSettingsVideoDevice = nullptr;
            }
            
            if (_mDeviceSettingsHost != nullptr) {
                _mDeviceSettingsHost->Release();
                _mDeviceSettingsHost = nullptr;
            }
            
            if (_mDeviceSettingsDisplay != nullptr) {
                _mDeviceSettingsDisplay->Release();
                _mDeviceSettingsDisplay = nullptr;
            }
            
            // Release the main device settings interface
            if (_mDeviceSettings != nullptr) {
                _mDeviceSettings->Release();
                _mDeviceSettings = nullptr;
            }
            mService->Release();
            mService = nullptr;
            mConnectionId = 0;
            DSLOG_INFO("DeviceSettings de-initialised");
        }
    }

    string DeviceSettings::Information() const
    {
        // No additional info to report.
        return (string());
    }

    void DeviceSettings::Deactivated(RPC::IRemoteConnection* connection)
    {
        // This can potentially be called on a socket thread, so the deactivation (which in turn kills this object) must be done
        // on a separate thread. Also make sure this call-stack can be unwound before we are totally destructed.
        if (mConnectionId == connection->Id()) {
            ASSERT(mService != nullptr);
            Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(mService, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
        }
    }

    void DeviceSettings::CallbackRevoked(const Core::IUnknown* remote, const uint32_t interfaceId)
    {
        // Add your handling code here, or leave empty if not needed
        DSLOG_INFO("called for interfaceId %u", interfaceId);
    }

} // namespace Plugin
} // namespace WPEFramework
