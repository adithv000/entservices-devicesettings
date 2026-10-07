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

#include "DeviceSettingsDisplayImplementation.h"

#include <syscall.h>
#include <vector>

using namespace std;

namespace WPEFramework {
namespace Plugin {

    DeviceSettingsDisplayImpl::DeviceSettingsDisplayImpl() : 
        _DisplayNotifications(),
        _DisplayHDMIHotPlugNotifications(),
        _apiLock(),
        _callbackLock(),
        _display(Display::Create(*this))
    {
        DSLOG_INFO("Constructor - Instance Address: %p", this);
    }

    DeviceSettingsDisplayImpl::~DeviceSettingsDisplayImpl() {
        DSLOG_INFO("Destructor - Instance Address: %p", this);

        std::list<std::pair<string, IDisplayNotification*>> notifications;
        std::list<std::pair<string, IDisplayHDMIHotPlugNotification*>> hotPlugNotifications;
        _callbackLock.Lock();
        notifications.swap(_DisplayNotifications);
        hotPlugNotifications.swap(_DisplayHDMIHotPlugNotifications);
        _callbackLock.Unlock();
        for (auto& entry : notifications) {
            entry.second->Release();
        }
        for (auto& entry : hotPlugNotifications) {
            entry.second->Release();
        }
    }

    template<typename Func, typename... Args>
    void DeviceSettingsDisplayImpl::dispatchDisplayEvent(Func notifyFunc, Args&&... args) {
        DSLOG_INFO(">>");
        std::vector<std::pair<string, IDisplayNotification*>> notifications;
        _callbackLock.Lock();
        for (auto& entry : _DisplayNotifications) {
            entry.second->AddRef();
            notifications.push_back(entry);
        }
        _callbackLock.Unlock();

        DSLOG_INFO(">>> Dispatching Display event to %zu clients", notifications.size());

        for (auto& entry : notifications) {
            const string& clientName = entry.first;
            auto* notification = entry.second;
            auto start = std::chrono::steady_clock::now();
            (notification->*notifyFunc)(std::forward<Args>(args)...);
            auto elapsed = std::chrono::steady_clock::now() - start;
            DSLOG_INFO("client '%s' took %" PRId64 "ms to process Display event", clientName.c_str(), std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            notification->Release();
        }
        DSLOG_INFO("<<");
    }

    template<typename Func, typename... Args>
    void DeviceSettingsDisplayImpl::dispatchDisplayHDMIHotPlugEvent(Func notifyFunc, Args&&... args) {
        DSLOG_INFO(">>");
        std::vector<std::pair<string, IDisplayHDMIHotPlugNotification*>> notifications;
        _callbackLock.Lock();
        for (auto& entry : _DisplayHDMIHotPlugNotifications) {
            entry.second->AddRef();
            notifications.push_back(entry);
        }
        _callbackLock.Unlock();

        DSLOG_INFO(">>> Dispatching DisplayHDMIHotPlug event to %zu clients", notifications.size());

        for (auto& entry : notifications) {
            const string& clientName = entry.first;
            auto* notification = entry.second;
            auto start = std::chrono::steady_clock::now();
            (notification->*notifyFunc)(std::forward<Args>(args)...);
            auto elapsed = std::chrono::steady_clock::now() - start;
            DSLOG_INFO("client '%s' took %" PRId64 "ms to process DisplayHDMIHotPlug event", clientName.c_str(), std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            notification->Release();
        }
        DSLOG_INFO("<<");
    }

    template <typename T>
    Core::hresult DeviceSettingsDisplayImpl::Register(std::list<std::pair<string, T*>>& list, const string& clientName, T* notification)
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
    Core::hresult DeviceSettingsDisplayImpl::Unregister(std::list<std::pair<string, T*>>& list, const T* notification)
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

    Core::hresult DeviceSettingsDisplayImpl::Register(const string& clientName, IDisplayNotification* notification)
    {
        Core::hresult errorCode = Register(_DisplayNotifications, clientName, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("IDisplay %p [%s], errorCode: %u", notification, clientName.c_str(), errorCode);
        } else {
            DSLOG_INFO("IDisplay %p [%s] registered successfully", notification, clientName.c_str());
        }
        return errorCode;
    }

    Core::hresult DeviceSettingsDisplayImpl::Unregister(IDisplayNotification* notification)
    {
        Core::hresult errorCode = Unregister(_DisplayNotifications, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("IDisplay %p, errorcode: %u", notification, errorCode);
        } else {
            DSLOG_INFO("IDisplay %p unregistered successfully", notification);
        }
        return errorCode;
    }

    Core::hresult DeviceSettingsDisplayImpl::Register(const string& clientName, IDisplayHDMIHotPlugNotification* notification)
    {
        Core::hresult errorCode = Register(_DisplayHDMIHotPlugNotifications, clientName, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("IDisplayHDMIHotPlug %p [%s], errorCode: %u", notification, clientName.c_str(), errorCode);
        } else {
            DSLOG_INFO("IDisplayHDMIHotPlug %p [%s] registered successfully", notification, clientName.c_str());
        }
        return errorCode;
    }

    Core::hresult DeviceSettingsDisplayImpl::Unregister(IDisplayHDMIHotPlugNotification* notification)
    {
        Core::hresult errorCode = Unregister(_DisplayHDMIHotPlugNotifications, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("IDisplayHDMIHotPlug %p, errorcode: %u", notification, errorCode);
        } else {
            DSLOG_INFO("IDisplayHDMIHotPlug %p unregistered successfully", notification);
        }
        return errorCode;
    }

    void DeviceSettingsDisplayImpl::OnDisplayRxSense(const DisplayEvent displayEvent)
    {
        DSLOG_INFO("DS HAL OnDisplayRxSense event: displayEvent=%d", static_cast<int>(displayEvent));
        dispatchDisplayEvent(&IDisplayNotification::OnDisplayRxSense, displayEvent);
    }

    void DeviceSettingsDisplayImpl::OnDisplayHDCPStatus(const int32_t hdcpStatus)
    {
        DSLOG_INFO("DS HAL OnDisplayHDCPStatus event: hdcpStatus=%d (HDCP protocol change from Display HAL)", hdcpStatus);
        dispatchDisplayEvent(&IDisplayNotification::OnDisplayHDCPStatus, hdcpStatus);
    }

    void DeviceSettingsDisplayImpl::OnDisplayHDMIHotPlug(const DisplayEvent displayEvent)
    {
        DSLOG_INFO("DS HAL OnDisplayHDMIHotPlug event: displayEvent=%d", static_cast<int>(displayEvent));
        dispatchDisplayHDMIHotPlugEvent(&IDisplayHDMIHotPlugNotification::OnDisplayHDMIHotPlug, displayEvent);
    }

    // Display interface method implementations called by DeviceSettingsImp 
    uint32_t DeviceSettingsDisplayImpl::GetDisplayEdid(const int32_t handle, DisplayEDID &edId, IDSVideoPortResolutionIterator*& supportedResolutionList)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _display.GetDisplayEdid(handle, edId, supportedResolutionList);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d", handle);
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsDisplayImpl::GetDisplayEdidBytes(const int32_t handle, uint8_t edIdBytes[], const uint16_t edidLength)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _display.GetDisplayEdidBytes(handle, edIdBytes, edidLength);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, edidLength=%d", handle, edidLength);
        } else {
            DSLOG_ERR("failed: handle=%d, edidLength=%d, error=%u", handle, edidLength, result);
        }
        return result;
    }

    uint32_t DeviceSettingsDisplayImpl::GetDisplay(const DisplayPortType portType, const int32_t index, int32_t &handle)
    {
        
        uint32_t result = Core::ERROR_GENERAL;
        result = _display.GetDisplay(portType, index, handle);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: portType=%d, index=%d, handle=%d", static_cast<int>(portType), index, handle);
        } else {
            DSLOG_ERR("failed: portType=%d, index=%d, error=%u", static_cast<int>(portType), index, result);
        }
        return result;
    }

    uint32_t DeviceSettingsDisplayImpl::GetDisplayAspectRatio(const int32_t handle, Exchange::IDeviceSettingsDisplay::DisplayVideoAspectRatio &aspectRatio)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _display.GetDisplayAspectRatio(handle, aspectRatio);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, aspectRatio=%d", handle, static_cast<int>(aspectRatio));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsDisplayImpl::SetAllmEnabled(const int32_t handle, const bool enabled)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _display.SetAllmEnabled(handle, enabled);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, enabled=%s", handle, enabled ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, enabled=%s, error=%u", handle, enabled ? "true" : "false", result);
        }
        return result;
    }

    uint32_t DeviceSettingsDisplayImpl::SetAVIContentType(const int32_t handle, const DisplayAVIContentType contentType)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _display.SetAVIContentType(handle, contentType);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, contentType=%d", handle, static_cast<int>(contentType));
        } else {
            DSLOG_ERR("failed: handle=%d, contentType=%d, error=%u", handle, static_cast<int>(contentType), result);
        }
        return result;
    }

    uint32_t DeviceSettingsDisplayImpl::SetAVIScanInformation(const int32_t handle, const DisplayAVIScanInformation scanInfo)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _display.SetAVIScanInformation(handle, scanInfo);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, scanInfo=%d", handle, static_cast<int>(scanInfo));
        } else {
            DSLOG_ERR("failed: handle=%d, scanInfo=%d, error=%u", handle, static_cast<int>(scanInfo), result);
        }
        return result;
    }

} // namespace Plugin
} // namespace WPEFramework