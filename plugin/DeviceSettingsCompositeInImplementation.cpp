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

#include "DeviceSettingsCompositeInImplementation.h"

#include <syscall.h>
#include <vector>

using namespace std;

namespace WPEFramework {
namespace Plugin {

    DeviceSettingsCompositeInImpl::DeviceSettingsCompositeInImpl() : 
        _CompositeInNotifications(),
        _apiLock(),
        _callbackLock(),
        _compositeIn(CompositeIn::Create(*this))
    {
        DSLOG_INFO("Constructor - Instance Address: %p", this);
    }

    DeviceSettingsCompositeInImpl::~DeviceSettingsCompositeInImpl() {
        DSLOG_INFO("Destructor - Instance Address: %p", this);
    }

    template<typename Func, typename... Args>
    void DeviceSettingsCompositeInImpl::dispatchCompositeInEvent(Func notifyFunc, Args&&... args) {
        DSLOG_INFO(">>");
        std::vector<std::pair<string, Exchange::IDeviceSettingsCompositeIn::INotification*>> notifications;
        _callbackLock.Lock();
        for (auto& entry : _CompositeInNotifications) {
            entry.second->AddRef();
            notifications.push_back(entry);
        }
        _callbackLock.Unlock();

        DSLOG_INFO(">>> Dispatching CompositeIn event to %zu clients", notifications.size());

        for (auto& entry : notifications) {
            const string& clientName = entry.first;
            auto* notification = entry.second;
            auto start = std::chrono::steady_clock::now();
            (notification->*notifyFunc)(std::forward<Args>(args)...);
            auto elapsed = std::chrono::steady_clock::now() - start;
            DSLOG_INFO("client '%s' took %" PRId64 "ms to process ICompositeIn event", clientName.c_str(), std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            notification->Release();
        }
        DSLOG_INFO("<<");
    }

    template <typename T>
    Core::hresult DeviceSettingsCompositeInImpl::Register(std::list<std::pair<string, T*>>& list, const string& clientName, T* notification)
    {
        uint32_t status = Core::ERROR_GENERAL;
        ASSERT(nullptr != notification);

        _callbackLock.Lock();
        // Make sure we can't register the same notification callback multiple times
        auto it = std::find_if(list.begin(), list.end(), [notification](const std::pair<string, T*>& p){ return p.second == notification; });
        if (it == list.end()) {
            list.push_back({clientName, notification});
            notification->AddRef();
            status = Core::ERROR_NONE;
        } else {
            DSLOG_WARN("Notification %p already registered - skipping", notification);
        }
        _callbackLock.Unlock();

        return status;
    }

    template <typename T>
    Core::hresult DeviceSettingsCompositeInImpl::Unregister(std::list<std::pair<string, T*>>& list, const T* notification)
    {
        uint32_t status = Core::ERROR_GENERAL;
        ASSERT(nullptr != notification);
        _callbackLock.Lock();

        // Make sure we can't unregister the same notification callback multiple times
        auto itr = std::find_if(list.begin(), list.end(), [notification](const std::pair<string, T*>& p){ return p.second == notification; });
        if (itr != list.end()) {
            itr->second->Release();
            list.erase(itr);
            status = Core::ERROR_NONE;
        }

        _callbackLock.Unlock();
        return status;
    }

    Core::hresult DeviceSettingsCompositeInImpl::Register(const string& clientName, Exchange::IDeviceSettingsCompositeIn::INotification* notification)
    {
        Core::hresult errorCode = Register(_CompositeInNotifications, clientName, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("ICompositeIn %p [%s], errorCode: %u", notification, clientName.c_str(), errorCode);
        } else {
            DSLOG_INFO("ICompositeIn %p [%s] registered successfully", notification, clientName.c_str());
        }
        return errorCode;
    }

    Core::hresult DeviceSettingsCompositeInImpl::Unregister(Exchange::IDeviceSettingsCompositeIn::INotification* notification)
    {
        Core::hresult errorCode = Unregister(_CompositeInNotifications, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("ICompositeIn %p, errorcode: %u", notification, errorCode);
        } else {
            DSLOG_INFO("ICompositeIn %p unregistered successfully", notification);
        }
        return errorCode;
    }

    // CompositeIn::INotification interface implementations (called by DS HAL)
    void DeviceSettingsCompositeInImpl::OnCompositeInHotPlug(const Exchange::IDeviceSettingsCompositeIn::CompositeInPort port, const bool isConnected)
    {
        DSLOG_INFO("DS HAL OnCompositeInHotPlug event: port=%d, isConnected=%s", static_cast<int>(port), isConnected ? "true" : "false");
        
        // Port already converted to WPE type at HAL layer - direct dispatch
        dispatchCompositeInEvent(&Exchange::IDeviceSettingsCompositeIn::INotification::OnCompositeInHotPlug, port, isConnected);
    }

    void DeviceSettingsCompositeInImpl::OnCompositeInSignalStatus(const Exchange::IDeviceSettingsCompositeIn::CompositeInPort port, const Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus signalStatus)
    {
        DSLOG_INFO("DS HAL OnCompositeInSignalStatus event: port=%d, signalStatus=%d", static_cast<int>(port), static_cast<int>(signalStatus));
        
        // Types already converted to WPE types at HAL layer - direct dispatch
        dispatchCompositeInEvent(&Exchange::IDeviceSettingsCompositeIn::INotification::OnCompositeInSignalStatus, port, signalStatus);
    }

    void DeviceSettingsCompositeInImpl::OnCompositeInStatus(const Exchange::IDeviceSettingsCompositeIn::CompositeInPort activePort, const bool isPresented)
    {
        DSLOG_INFO("DS HAL OnCompositeInStatus event: activePort=%d, isPresented=%s", static_cast<int>(activePort), isPresented ? "true" : "false");
        
        // Port already converted to WPE type at HAL layer - direct dispatch
        dispatchCompositeInEvent(&Exchange::IDeviceSettingsCompositeIn::INotification::OnCompositeInStatus, activePort, isPresented);
    }

    void DeviceSettingsCompositeInImpl::OnCompositeInVideoModeUpdate(const Exchange::IDeviceSettingsCompositeIn::CompositeInPort activePort, const Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution videoResolution)
    {
        DSLOG_INFO("DS HAL OnCompositeInVideoModeUpdate event: activePort=%d", static_cast<int>(activePort));
        
        // Types already converted to WPE types at HAL layer - direct dispatch
        dispatchCompositeInEvent(&Exchange::IDeviceSettingsCompositeIn::INotification::OnCompositeInVideoModeUpdate, activePort, videoResolution);
    }

    // CompositeIn interface method implementations called by DeviceSettingsImp (delegate to _compositeIn)
    uint32_t DeviceSettingsCompositeInImpl::GetNrOfCompositeInputs(int32_t &nrCompositeInputs)
    {
        uint32_t result = _compositeIn.GetNrOfCompositeInputs(nrCompositeInputs);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: nrCompositeInputs=%d", nrCompositeInputs);
        } else {
            DSLOG_ERR("failed: error=%u", result);
        }
        return result;
    }

    uint32_t DeviceSettingsCompositeInImpl::GetCompositeInStatus(CompositeInStatus &status)
    {
        uint32_t result = _compositeIn.GetCompositeInStatus(status);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: activePort=%d, isPresented=%s",
                    static_cast<int>(status.activePort), status.isPresented ? "true" : "false");
        } else {
            DSLOG_ERR("failed: error=%u", result);
        }
        return result;
    }

    uint32_t DeviceSettingsCompositeInImpl::SelectCompositeInPort(const CompositeInPort port)
    {
        uint32_t result = _compositeIn.SelectCompositeInPort(port);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: port=%d", static_cast<int>(port));
        } else {
            DSLOG_ERR("failed: port=%d, error=%u", static_cast<int>(port), result);
        }
        return result;
    }

    uint32_t DeviceSettingsCompositeInImpl::ScaleCompositeInVideo(const CompositeInVideoRectangle videoRect)
    {
        uint32_t result = _compositeIn.ScaleCompositeInVideo(videoRect);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: x=%d, y=%d, width=%d, height=%d",
                    videoRect.x, videoRect.y, videoRect.width, videoRect.height);
        } else {
            DSLOG_ERR("failed: error=%u", result);
        }
        return result;
    }



} // namespace Plugin
} // namespace WPEFramework