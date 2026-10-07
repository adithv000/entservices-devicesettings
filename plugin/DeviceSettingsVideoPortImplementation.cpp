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

#include "DeviceSettingsVideoPortImplementation.h"

#include <cinttypes>
#include <syscall.h>
#include <set>
#include <vector>

using namespace std;

#include "DeviceSettingsHALConfig.h"

namespace WPEFramework {
namespace Plugin {



    DeviceSettingsVideoPortImpl::DeviceSettingsVideoPortImpl() :
        _VideoPortNotifications(),
        _apiLock(),
        _callbackLock(),
        _jobLock(),
        _pendingJobs(),
        _videoPort(VideoPort::Create(*this))
    {
        DSLOG_INFO("Constructor - Instance Address: %p", this);
    }

    DeviceSettingsVideoPortImpl::~DeviceSettingsVideoPortImpl() {
        DSLOG_INFO("Destructor - Instance Address: %p", this);

        // Revoke every still-queued/in-flight VideoPortNotificationJob first: each job stores a raw
        // `this` pointer and calls Dispatch() on it from a worker-pool thread, so letting one run (or
        // keep running) after this object starts tearing down its members would be a use-after-free.
        // Revoke() blocks (default: indefinitely) until an already-executing Dispatch() returns.
        std::vector<Core::ProxyType<Core::IDispatch>> pendingJobs;
        _jobLock.Lock();
        pendingJobs.swap(_pendingJobs);
        _jobLock.Unlock();
        for (auto& job : pendingJobs) {
            Core::IWorkerPool::Instance().Revoke(job);
        }

        std::list<std::pair<string, Exchange::IDeviceSettingsVideoPort::INotification*>> notifications;
        _callbackLock.Lock();
        notifications.swap(_VideoPortNotifications);
        _callbackLock.Unlock();
        for (auto& entry : notifications) {
            entry.second->Release();
        }
    }

    void DeviceSettingsVideoPortImpl::submitVideoPortEvent(Event ev, ParamsType params) {
        Core::ProxyType<Core::IDispatch> job(VideoPortNotificationJob::Create(this, ev, std::move(params)));
        _jobLock.Lock();
        _pendingJobs.push_back(job);
        _jobLock.Unlock();
        Core::IWorkerPool::Instance().Submit(job);
    }

    void DeviceSettingsVideoPortImpl::Dispatch(Event ev, const ParamsType& params) {
        DSLOG_INFO(">>");
        std::vector<std::pair<string, Exchange::IDeviceSettingsVideoPort::INotification*>> notifications;
        _callbackLock.Lock();
        for (auto& entry : _VideoPortNotifications) {
            entry.second->AddRef();
            notifications.push_back(entry);
        }
        _callbackLock.Unlock();

        DSLOG_INFO(">>> Dispatching VideoPort event to %zu clients (async)", notifications.size());

        for (auto& entry : notifications) {
            switch (ev) {
                case EV_RESOLUTION_POST_CHANGE: {
                    const string& clientName = entry.first;
                    auto* notification = entry.second;
                    ResolutionChange res;
                    res.width = std::get<0>(params);
                    res.height = std::get<1>(params);
                    auto start = std::chrono::steady_clock::now();
                    notification->OnResolutionPostChange(res);
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    DSLOG_INFO("client '%s' took %" PRId64 "ms to process OnResolutionPostChange event", clientName.c_str(), std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
                    notification->Release();
                    break;
                }
                default:
                    DSLOG_WARN("Unknown event %d", ev);
                    break;
            }
        }
        DSLOG_INFO("<<");
    }

    template<typename Func, typename... Args>
    void DeviceSettingsVideoPortImpl::dispatchVideoPortEvent(Func notifyFunc, Args&&... args) {
        DSLOG_INFO(">>");
        std::vector<std::pair<string, Exchange::IDeviceSettingsVideoPort::INotification*>> notifications;
        _callbackLock.Lock();
        for (auto& entry : _VideoPortNotifications) {
            entry.second->AddRef();
            notifications.push_back(entry);
        }
        _callbackLock.Unlock();

        DSLOG_INFO(">>> Dispatching VideoPort event to %zu clients (sync)", notifications.size());

        for (auto& entry : notifications) {
            const string& clientName = entry.first;
            auto* notification = entry.second;
            auto start = std::chrono::steady_clock::now();
            (notification->*notifyFunc)(std::forward<Args>(args)...);
            auto elapsed = std::chrono::steady_clock::now() - start;
            DSLOG_INFO("client '%s' took %" PRId64 "ms to process IVideoPort event",clientName.c_str(), std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            notification->Release();
        }
        DSLOG_INFO("<<");
    }

    template <typename T>
    Core::hresult DeviceSettingsVideoPortImpl::Register(std::list<std::pair<string, T*>>& list, const string& clientName, T* notification)
    {
        uint32_t status = Core::ERROR_GENERAL;
        ASSERT(nullptr != notification);
        _callbackLock.Lock();
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
    Core::hresult DeviceSettingsVideoPortImpl::Unregister(std::list<std::pair<string, T*>>& list, const T* notification)
    {
        uint32_t status = Core::ERROR_GENERAL;
        ASSERT(nullptr != notification);
        _callbackLock.Lock();
        auto itr = std::find_if(list.begin(), list.end(), [notification](const std::pair<string, T*>& p){ return p.second == notification; });
        if (itr != list.end()) {
            itr->second->Release();
            list.erase(itr);
            status = Core::ERROR_NONE;
        }
        _callbackLock.Unlock();
        return status;
    }

    Core::hresult DeviceSettingsVideoPortImpl::Register(const string& clientName, Exchange::IDeviceSettingsVideoPort::INotification* notification)
    {
        Core::hresult errorCode = Register(_VideoPortNotifications, clientName, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("IVideoPort %p [%s], errorCode: %u", notification, clientName.c_str(), errorCode);
        } else {
            DSLOG_INFO("IVideoPort %p [%s] registered successfully", notification, clientName.c_str());
        }
        return errorCode;
    }

    Core::hresult DeviceSettingsVideoPortImpl::Unregister(Exchange::IDeviceSettingsVideoPort::INotification* notification)
    {
        Core::hresult errorCode = Unregister(_VideoPortNotifications, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("IVideoPort %p, errorcode: %u", notification, errorCode);
        } else {
            DSLOG_INFO("IVideoPort %p unregistered successfully", notification);
        }
        return errorCode;
    }

    // Intermediate notification methods removed - DS HAL callbacks now directly call dispatchVideoPortEvent

    // VideoPort::INotification interface implementations (called by DS HAL)
    void DeviceSettingsVideoPortImpl::OnResolutionPreChange(const ResolutionChange resolution)
    {
        DSLOG_INFO("DS HAL OnResolutionPreChange event: width=%u, height=%u", resolution.width, resolution.height);
        dispatchVideoPortEvent(&Exchange::IDeviceSettingsVideoPort::INotification::OnResolutionPreChange, resolution);
    }

    void DeviceSettingsVideoPortImpl::OnResolutionPostChange(const ResolutionChange resolution)
    {
        DSLOG_INFO("DS HAL OnResolutionPostChange event: width=%u, height=%u", resolution.width, resolution.height);
        submitVideoPortEvent(EV_RESOLUTION_POST_CHANGE, std::make_tuple(resolution.width, resolution.height));
    }

    void DeviceSettingsVideoPortImpl::OnHDCPStatusChange(const VideoPortHdcpStatus hdcpStatus)
    {
        DSLOG_INFO("DS HAL OnHDCPStatusChange event: hdcpStatus=%d", static_cast<int>(hdcpStatus));
        dispatchVideoPortEvent(&Exchange::IDeviceSettingsVideoPort::INotification::OnHDCPStatusChange, hdcpStatus);
    }

    void DeviceSettingsVideoPortImpl::OnVideoFormatUpdate(const HDRStandard videoFormatHDR)
    {
        DSLOG_INFO("DS HAL OnVideoFormatUpdate event: videoFormatHDR=0x%x", static_cast<uint16_t>(videoFormatHDR));
        dispatchVideoPortEvent(&Exchange::IDeviceSettingsVideoPort::INotification::OnVideoFormatUpdate, videoFormatHDR);
    }

    // VideoPort interface method implementations called by DeviceSettingsImp 
    uint32_t DeviceSettingsVideoPortImpl::GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t &handle)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetVideoPort(videoPort, index, handle);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: videoPort=%d, index=%d, handle=%d", static_cast<int>(videoPort), index, handle);
        } else {
            DSLOG_ERR("failed: videoPort=%d, index=%d, error=%u", static_cast<int>(videoPort), index, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::IsVideoPortEnabled(const int32_t handle, bool &enabled)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.IsVideoPortEnabled(handle, enabled);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d, enabled: %s", handle, enabled ? "true" : "false");
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetVideoPortResolutionConfig(VideoPortType videoPortType,
                                                                        IVideoPortResolutionIterator*& resolutions) const
    {
        std::vector<VideoPortResolution> resolutionConfigs;

        DeviceSettingsHAL::PopulateVideoPortResolutionConfig(videoPortType, resolutionConfigs);

        using ResolutionIterator = RPC::IteratorType<IVideoPortResolutionIterator>;
        resolutions = Core::Service<ResolutionIterator>::Create<IVideoPortResolutionIterator>(resolutionConfigs);

        DSLOG_INFO("videoPortType=%d resolutions=%zu",
            static_cast<int>(videoPortType), resolutionConfigs.size());
        return Core::ERROR_NONE;
    }

    uint32_t DeviceSettingsVideoPortImpl::EnableVideoPort(const int32_t handle, const bool enabled)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.EnableVideoPort(handle, enabled);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d, enabled: %s", handle, enabled ? "true" : "false");
        } else {
            DSLOG_ERR("failed for handle: %d, enabled: %s, error: %u", handle, enabled ? "true" : "false", result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::IsVideoPortDisplayConnected(const int32_t handle, bool &connected)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.IsVideoPortDisplayConnected(handle, connected);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d, connected: %s", handle, connected ? "true" : "false");
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::IsVideoPortActive(const int32_t handle, bool &active)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.IsVideoPortActive(handle, active);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d, active: %s", handle, active ? "true" : "false");
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetVideoPortResolution(const int32_t handle, VideoPortResolution &resolution)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetVideoPortResolution(handle, resolution);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d", handle);
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::SetVideoPortResolution(const int32_t handle, const VideoPortResolution resolution, const bool persist, const bool forceCompatibility)
    {
        uint32_t result = Core::ERROR_GENERAL;

        // Callers (e.g. DisplaySettings) may supply only the name with other fields uninitialised.
        // Look up the full params from the HAL-populated cache before passing to the HAL layer.
        VideoPortResolution resolvedResolution = resolution;
        if (!resolution.name.empty()) {
            _apiLock.Lock();
            // Lazy one-time population to avoid cost at plugin activation.
            if (_cachedVideoPortResolutions.empty()) {
                std::set<std::string> seen;
                for (int t = static_cast<int>(VideoPortType::DS_VIDEO_PORT_TYPE_RF);
                     t < static_cast<int>(VideoPortType::DS_VIDEO_PORT_TYPE_MAX); ++t) {
                    std::vector<VideoPortResolution> tmp;
                    DeviceSettingsHAL::PopulateVideoPortResolutionConfig(
                        static_cast<VideoPortType>(t), tmp);
                    for (const auto& r : tmp) {
                        if (seen.insert(r.name).second)
                            _cachedVideoPortResolutions.push_back(r);
                    }
                }
                DSLOG_INFO("lazily cached %zu resolutions from HAL",
                        _cachedVideoPortResolutions.size());
            }
            for (const auto& cached : _cachedVideoPortResolutions) {
                if (cached.name == resolution.name) {
                    resolvedResolution = cached;
                    break;
                }
            }
            _apiLock.Unlock();
        }

        result = _videoPort.SetVideoPortResolution(handle, resolvedResolution, persist, forceCompatibility);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d, persist: %s, forceCompatibility: %s", handle, persist ? "true" : "false", forceCompatibility ? "true" : "false");
        } else {
            DSLOG_ERR("failed for handle: %d, persist: %s, forceCompatibility: %s, error: %u", handle, persist ? "true" : "false", forceCompatibility ? "true" : "false", result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetColorSpace(const int32_t handle, VideoPortColorSpace &colorSpace)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetColorSpace(handle, colorSpace);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d", handle);
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::SetColorSpace(const int32_t handle, const VideoPortColorSpace colorSpace, const bool persist)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.SetColorSpace(handle, colorSpace);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d, persist: %s", handle, persist ? "true" : "false");
        } else {
            DSLOG_ERR("failed for handle: %d, persist: %s, error: %u", handle, persist ? "true" : "false", result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange &quantizationRange)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetQuantizationRange(handle, quantizationRange);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d", handle);
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::SetQuantizationRange(const int32_t handle, const VideoPortQuantizationRange quantizationRange, const bool persist)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.SetVideoPortQuantizationRange(handle, quantizationRange);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d, persist: %s", handle, persist ? "true" : "false");
        } else {
            DSLOG_ERR("failed for handle: %d, persist: %s, error: %u", handle, persist ? "true" : "false", result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetVideoPortHDCPStatus(const int32_t handle, VideoPortHdcpStatus &hdcpStatus)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetVideoPortHDCPStatus(handle, hdcpStatus);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d", handle);
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetHDCPProtocolVersionOnVideoPort(handle, hdcpVersion);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d", handle);
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetVideoPortHDCPCurrentProtocol(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetHDCPCurrentProtocolVersionOnVideoPort(handle, hdcpVersion);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d", handle);
        } else {
            DSLOG_ERR("failed for handle: %d, error: %u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::SetVideoPortHDCPProfile(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion, const bool persist)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.SetHDMIPreference(handle, hdcpVersion);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded for handle: %d, persist: %s", handle, persist ? "true" : "false");
        } else {
            DSLOG_ERR("failed for handle: %d, persist: %s, error: %u", handle, persist ? "true" : "false", result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetMatrixCoefficients(const int32_t handle, DisplayMatrixCoefficients &matrixCoefficients)
    {
        uint32_t result = Core::ERROR_GENERAL;
        DisplayMatrixCoefficients displayMatrixCoefficients;
        result = _videoPort.GetMatrixCoefficients(handle, displayMatrixCoefficients);
        if (result == Core::ERROR_NONE) {
            matrixCoefficients = static_cast<DisplayMatrixCoefficients>(displayMatrixCoefficients);
            DSLOG_INFO("succeeded: handle=%d, matrixCoefficients=%d", handle, static_cast<int>(matrixCoefficients));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetCurrentOutputSettings(const int32_t handle, DSOutputSettings &outputSettings)
    {
        uint32_t result = Core::ERROR_GENERAL;
        DSOutputSettings dsOutputSettings;
        result = _videoPort.GetCurrentOutputSettings(handle, dsOutputSettings);
        if (result == Core::ERROR_NONE) {
            // Convert DSOutputSettings to DSOutputSettings
            outputSettings.videoEotf = static_cast<HDRStandard>(dsOutputSettings.videoEotf);
            outputSettings.matrixCoefficients = static_cast<DisplayMatrixCoefficients>(dsOutputSettings.matrixCoefficients);
            outputSettings.colorDepth = dsOutputSettings.colorDepth;
            outputSettings.colorSpace = static_cast<VideoPortColorSpace>(dsOutputSettings.colorSpace);
            outputSettings.quantizationRange = static_cast<VideoPortQuantizationRange>(dsOutputSettings.quantizationRange);
            DSLOG_INFO("succeeded: handle=%d", handle);
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::SetBackgroundColor(const int32_t handle, const VideoBackgroundColor backgroundColor)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.SetBackgroundColor(handle, backgroundColor);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, backgroundColor=%d", handle, static_cast<int>(backgroundColor));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::SetForceHDRMode(const int32_t handle, const HDRStandard hdrMode)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.SetForceHDRMode(handle, hdrMode);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, hdrMode=%d", handle, static_cast<int>(hdrMode));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetColorDepthCapabilities(const int32_t handle, uint32_t &colorDepthCapabilities)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetColorDepthCapabilities(handle, colorDepthCapabilities);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, colorDepthCapabilities=0x%x", handle, colorDepthCapabilities);
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetPreferredColorDepth(const int32_t handle, DisplayColorDepth &colorDepth, const bool persist)
    {
        uint32_t result = Core::ERROR_GENERAL;
        DisplayColorDepth displayColorDepth;
        result = _videoPort.GetPreferredColorDepth(handle, displayColorDepth, persist);
        if (result == Core::ERROR_NONE) {
            colorDepth = static_cast<DisplayColorDepth>(displayColorDepth);
            DSLOG_INFO("succeeded: handle=%d, colorDepth=%d, persist=%s", handle, static_cast<int>(colorDepth), persist ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::SetPreferredColorDepth(const int32_t handle, const DisplayColorDepth colorDepth, const bool persist)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.SetPreferredColorDepth(handle, static_cast<DisplayColorDepth>(colorDepth), persist);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, colorDepth=%d, persist=%s", handle, static_cast<int>(colorDepth), persist ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    // Additional methods required by DeviceSettingsImplementation.cpp and IDeviceSettingsVideoPort.h interface
    
    uint32_t DeviceSettingsVideoPortImpl::getIgnoreEDIDStatus(const int32_t handle, bool &ignoreEDID)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.getIgnoreEDIDStatus(handle, ignoreEDID);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, ignoreEDID=%d", handle, static_cast<int>(ignoreEDID));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetColorDepth(const int32_t handle, uint32_t &colorDepth)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetColorDepth(handle, colorDepth);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, colorDepth=%u", handle, colorDepth);
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t hdcpKey[], const uint16_t hdcpKeySize)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.EnableHDCPOnVideoPort(handle, hdcpEnable, hdcpKey, hdcpKeySize);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, hdcpEnable=%s", handle, hdcpEnable ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::IsHDCPEnabledOnVideoPort(const int32_t handle, bool &hdcpEnabled)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.IsHDCPEnabledOnVideoPort(handle, hdcpEnabled);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, hdcpEnabled=%s", handle, hdcpEnabled ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::GetTVHDRCapabilities(const int32_t handle, int32_t &capabilities)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetTVHDRCapabilities(handle, capabilities);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, capabilities=0x%x", handle, capabilities);
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::GetTVSupportedResolutions(const int32_t handle, int32_t &resolutions)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetTVSupportedResolutions(handle, resolutions);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, resolutions=0x%x", handle, resolutions);
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::SetForceDisable4K(const int32_t handle, const bool disable)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.SetForceDisable4K(handle, disable);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, disable=%s", handle, disable ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::GetForceDisable4K(const int32_t handle, bool &disabled)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetForceDisable4K(handle, disabled);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, disabled=%s", handle, disabled ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::IsVideoPortOutputHDR(const int32_t handle, bool &isHDR)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.IsVideoPortOutputHDR(handle, isHDR);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, isHDR=%s", handle, isHDR ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::ResetVideoPortOutputToSDR()
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.ResetVideoPortOutputToSDR();
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded");
        } else {
            DSLOG_ERR("failed: error=%u", result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetHDMIPreference(handle, hdcpVersion);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, hdcpVersion=%d", handle, static_cast<int>(hdcpVersion));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.SetHDMIPreference(handle, hdcpVersion);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, hdcpVersion=%d", handle, static_cast<int>(hdcpVersion));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::GetVideoEOTF(const int32_t handle, HDRStandard &hdrStandard)
    {
        uint32_t result = Core::ERROR_GENERAL;
        HDRStandard interfaceHdrStandard;
        result = _videoPort.GetVideoEOTF(handle, interfaceHdrStandard);
        if (result == Core::ERROR_NONE) {
            hdrStandard = static_cast<HDRStandard>(interfaceHdrStandard);
            DSLOG_INFO("succeeded: handle=%d, hdrStandard=%d", handle, static_cast<int>(hdrStandard));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::IsVideoPortDisplaySurround(const int32_t handle, bool &surround)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.IsVideoPortDisplaySurround(handle, surround);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, surround=%s", handle, surround ? "true" : "false");
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }
    
    uint32_t DeviceSettingsVideoPortImpl::GetVideoPortDisplaySurroundMode(const int32_t handle, VideoPortSurroundMode &surroundMode)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetVideoPortDisplaySurroundMode(handle, surroundMode);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, surroundMode=%d", handle, static_cast<int>(surroundMode));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetHDCPReceiverProtocolVersionOnVideoPort(handle, hdcpVersion);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, hdcpVersion=%d", handle, static_cast<int>(hdcpVersion));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    uint32_t DeviceSettingsVideoPortImpl::GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion)
    {
        uint32_t result = Core::ERROR_GENERAL;
        result = _videoPort.GetHDCPCurrentProtocolVersionOnVideoPort(handle, hdcpVersion);
        if (result == Core::ERROR_NONE) {
            DSLOG_INFO("succeeded: handle=%d, hdcpVersion=%d", handle, static_cast<int>(hdcpVersion));
        } else {
            DSLOG_ERR("failed: handle=%d, error=%u", handle, result);
        }
        return result;
    }

    void DeviceSettingsVideoPortImpl::getCachedConfigs(
        std::vector<Exchange::IDeviceSettings::VideoPortTypeConfig>& videoPortTypes,
        std::vector<Exchange::IDeviceSettings::VideoPortPortConfig>& videoPorts,
        std::vector<Exchange::IDeviceSettings::VideoPortResolutionConfig>& videoPortResolutions) const
    {
        _apiLock.Lock();

        videoPortTypes.reserve(_cachedVideoPortTypes.size());
        for (const auto& src : _cachedVideoPortTypes) {
            videoPortTypes.push_back({static_cast<int32_t>(src.typeId), src.name,
                src.dtcpSupported, src.hdcpSupported,
                src.restrictedResolution, src.supportedResolutionNames});
        }

        videoPorts.reserve(_cachedVideoPorts.size());
        for (const auto& src : _cachedVideoPorts) {
            videoPorts.push_back({static_cast<int32_t>(src.videoPortType), src.videoPortIndex,
                src.connectedAudioPortType, src.connectedAudioPortIndex, src.defaultResolution});
        }

        // Resolution config is cached from the 0th video port type during init.
        // Copy it whenever the cache is non-empty (i.e. at least one type exists).
        if (!_cachedVideoPortResolutions.empty()) {
            videoPortResolutions.reserve(_cachedVideoPortResolutions.size());
            for (const auto& src : _cachedVideoPortResolutions) {
                videoPortResolutions.push_back({
                    src.name,
                    static_cast<int32_t>(src.pixelResolution),
                    static_cast<int32_t>(src.aspectRatio),
                    static_cast<int32_t>(src.stereoScopicMode),
                    static_cast<int32_t>(src.frameRate),
                    src.interlaced});
            }
        }

        _apiLock.Unlock();
    }

} // namespace Plugin
} // namespace WPEFramework