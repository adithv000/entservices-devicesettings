/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
    Core::hresult DeviceSettingsAudioImpl::EnableAudioSurroundDecoder(const int32_t handle, const bool enable) {
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

#include "DeviceSettingsAudioImplementation.h"

#include <syscall.h>
#include <vector>

using namespace std;

#include "DeviceSettingsHALConfig.h"

namespace WPEFramework {
namespace Plugin {

    DeviceSettingsAudioImpl::DeviceSettingsAudioImpl()
        : _configLock()
        , _callbackLock()
        , _audio(Audio::Create(*this))
    {
        DSLOG_INFO("Constructor - Instance Address: %p", this);
    }

    DeviceSettingsAudioImpl::~DeviceSettingsAudioImpl() {
        DSLOG_INFO("Destructor - Instance Address: %p", this);
    }

    template<typename Func, typename... Args>
    void DeviceSettingsAudioImpl::dispatchAudioEvent(Func notifyFunc, Args&&... args) {
        DSLOG_INFO(">>");
        std::vector<std::pair<string, DeviceSettingsAudio::INotification*>> notifications;
        _callbackLock.Lock();
        for (auto& entry : _AudioNotifications) {
            entry.second->AddRef();
            notifications.push_back(entry);
        }
        _callbackLock.Unlock();

        DSLOG_INFO(">>> Dispatching Audio event to %zu clients", notifications.size());

        for (auto& entry : notifications) {
            const string& clientName = entry.first;
            auto* notification = entry.second;
            auto start = std::chrono::steady_clock::now();
            (notification->*notifyFunc)(std::forward<Args>(args)...);
            auto elapsed = std::chrono::steady_clock::now() - start;
            DSLOG_INFO("client '%s' took %" PRId64 "ms to process IAudio event", clientName.c_str(), std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            notification->Release();
        }
        DSLOG_INFO("<<");
    }

    template <typename T>
    Core::hresult DeviceSettingsAudioImpl::Register(std::list<std::pair<string, T*>>& list, const string& clientName, T* notification)
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
    Core::hresult DeviceSettingsAudioImpl::Unregister(std::list<std::pair<string, T*>>& list, const T* notification)
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

    Core::hresult DeviceSettingsAudioImpl::Register(const string& clientName, DeviceSettingsAudio::INotification* notification)
    {
        Core::hresult errorCode = Register(_AudioNotifications, clientName, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("IAudio %p [%s], errorCode: %u", notification, clientName.c_str(), errorCode);
        } else {
            DSLOG_INFO("IAudio %p [%s] registered successfully", notification, clientName.c_str());
        }
        return errorCode;
    }

    Core::hresult DeviceSettingsAudioImpl::Unregister(DeviceSettingsAudio::INotification* notification)
    {
        Core::hresult errorCode = Unregister(_AudioNotifications, notification);
        if (errorCode != Core::ERROR_NONE) {
            DSLOG_ERR("IAudio %p, errorcode: %u", notification, errorCode);
        } else {
            DSLOG_INFO("IAudio %p unregistered successfully", notification);
        }
        return errorCode;
    }

    // Audio notification implementations - hardware callbacks
    void DeviceSettingsAudioImpl::OnAssociatedAudioMixingChanged(bool mixing)
    {
        DSLOG_INFO("event Received: mixing=%s", mixing ? "true" : "false");
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAssociatedAudioMixingChanged, mixing);
    }

    void DeviceSettingsAudioImpl::OnAudioFaderControlChanged(int32_t mixerBalance)
    {
        DSLOG_INFO("event Received: mixerBalance=%d", mixerBalance);
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAudioFaderControlChanged, mixerBalance);
    }

    void DeviceSettingsAudioImpl::OnAudioPrimaryLanguageChanged(const std::string& primaryLanguage)
    {
        DSLOG_INFO("event Received: primaryLanguage=%s", primaryLanguage.c_str());
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAudioPrimaryLanguageChanged, primaryLanguage);
    }

    void DeviceSettingsAudioImpl::OnAudioSecondaryLanguageChanged(const std::string& secondaryLanguage)
    {
        DSLOG_INFO("event Received: secondaryLanguage=%s", secondaryLanguage.c_str());
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAudioSecondaryLanguageChanged, secondaryLanguage);
    }

    void DeviceSettingsAudioImpl::OnAudioOutHotPlug(AudioPortType portType, uint32_t uiPortNumber, bool isPortConnected)
    {
        DSLOG_INFO("event Received: portType=%d, port=%u, connected=%s", portType, uiPortNumber, isPortConnected ? "true" : "false");
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAudioOutHotPlug, portType, uiPortNumber, isPortConnected);
    }

    void DeviceSettingsAudioImpl::OnAudioFormatUpdate(AudioFormat audioFormat)
    {
        DSLOG_INFO("event Received: audioFormat=%d", audioFormat);
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAudioFormatUpdate, audioFormat);
    }

    void DeviceSettingsAudioImpl::OnDolbyAtmosCapabilitiesChanged(DolbyAtmosCapability atmosCapability, bool status)
    {
        DSLOG_INFO("event Received: capability=%d, status=%s", atmosCapability, status ? "true" : "false");
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnDolbyAtmosCapabilitiesChanged, atmosCapability, status);
    }

    void DeviceSettingsAudioImpl::OnAudioPortStateChanged(AudioPortState audioPortState)
    {
        DSLOG_INFO("event Received: audioPortState=%d", audioPortState);
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAudioPortStateChanged, audioPortState);
    }

    void DeviceSettingsAudioImpl::OnAudioLevelChanged(int32_t audioLevel)
    {
        DSLOG_INFO("event Received: audioLevel=%d", audioLevel);
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAudioLevelChanged, audioLevel);
    }

    void DeviceSettingsAudioImpl::OnAudioModeEvent(AudioPortType audioPortType, AudioStereoMode audioMode)
    {
        DSLOG_INFO("event Received: portType=%d, mode=%d", audioPortType, audioMode);
        dispatchAudioEvent(&DeviceSettingsAudio::INotification::OnAudioModeEvent, audioPortType, audioMode);
    }

    // Audio port management
    Core::hresult DeviceSettingsAudioImpl::GetAudioPort(const AudioPortType type, const int32_t index, int32_t &handle) {
        DSLOG_INFO("type=%d, index=%d", type, index);
        uint32_t result = _audio.GetAudioPort(type, index, handle);
        return result;
    }

    // Audio capabilities
    Core::hresult DeviceSettingsAudioImpl::GetAudioCapabilities(const int32_t handle, int32_t &capabilities) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioCapabilities(handle, capabilities);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAudioMS12Capabilities(const int32_t handle, int32_t &capabilities) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioMS12Capabilities(handle, capabilities);
        return result;
    }

    // Audio format and encoding
    Core::hresult DeviceSettingsAudioImpl::GetAudioFormat(const int32_t handle, AudioFormat &audioFormat) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioFormat(handle, audioFormat);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAudioEncoding(const int32_t handle, AudioEncoding &encoding) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioEncoding(handle, encoding);
        return result;
    }

    // Audio level and volume control
    Core::hresult DeviceSettingsAudioImpl::SetAudioLevel(const int32_t handle, const float audioLevel) {
        DSLOG_INFO("handle=%d, audioLevel=%.2f", handle, audioLevel);
        uint32_t result = _audio.SetAudioLevel(handle, audioLevel);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAudioLevel(const int32_t handle, float &audioLevel) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioLevel(handle, audioLevel);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::SetAudioGain(const int32_t handle, const float gainLevel) {
        DSLOG_INFO("handle=%d, gainLevel=%.2f", handle, gainLevel);
        uint32_t result = _audio.SetAudioGain(handle, gainLevel);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAudioGain(const int32_t handle, float &gainLevel) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioGain(handle, gainLevel);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::SetAudioMute(const int32_t handle, const bool mute) {
        DSLOG_INFO("handle=%d, mute=%s", handle, mute ? "true" : "false");
        uint32_t result = _audio.SetAudioMute(handle, mute);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::IsAudioMuted(const int32_t handle, bool &muted) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.IsAudioMuted(handle, muted);
        return result;
    }

    // Audio ducking
    Core::hresult DeviceSettingsAudioImpl::SetAudioDucking(const int32_t handle, const AudioDuckingType duckingType, const AudioDuckingAction duckingAction, const uint8_t level) {
        DSLOG_INFO("handle=%d, duckingType=%d, duckingAction=%d, level=%d", handle, duckingType, duckingAction, level);
        uint32_t result = _audio.SetAudioDucking(handle, duckingType, duckingAction, level);
        return result;
    }

    // Stereo mode
    Core::hresult DeviceSettingsAudioImpl::GetStereoMode(const int32_t handle, AudioStereoMode &mode, const bool persist) {
        DSLOG_INFO("handle=%d, persist=%s", handle, persist ? "true" : "false");
        uint32_t result = _audio.GetStereoMode(handle, mode, persist);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::SetStereoMode(const int32_t handle, const AudioStereoMode mode, const bool persist) {
        DSLOG_INFO("handle=%d, mode=%d, persist=%s", handle, mode, persist ? "true" : "false");
        uint32_t result = _audio.SetStereoMode(handle, mode, persist);
        return result;
    }

    // Associated audio mixing
    Core::hresult DeviceSettingsAudioImpl::SetAssociatedAudioMixing(const int32_t handle, const bool mixing) {
        DSLOG_INFO("handle=%d, mixing=%s", handle, mixing ? "true" : "false");
        uint32_t result = _audio.SetAssociatedAudioMixing(handle, mixing);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAssociatedAudioMixing(const int32_t handle, bool &mixing) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAssociatedAudioMixing(handle, mixing);
        return result;
    }

    // Audio fader control
    Core::hresult DeviceSettingsAudioImpl::SetAudioFaderControl(const int32_t handle, const int32_t mixerBalance) {
        DSLOG_INFO("handle=%d, mixerBalance=%d", handle, mixerBalance);
        uint32_t result = _audio.SetAudioFaderControl(handle, mixerBalance);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAudioFaderControl(const int32_t handle, int32_t &mixerBalance) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioFaderControl(handle, mixerBalance);
        return result;
    }

    // Audio language settings
    Core::hresult DeviceSettingsAudioImpl::SetAudioPrimaryLanguage(const int32_t handle, const std::string& primaryAudioLanguage) {
        DSLOG_INFO("handle=%d, primaryAudioLanguage=%s", handle, primaryAudioLanguage.c_str());
        uint32_t result = _audio.SetAudioPrimaryLanguage(handle, primaryAudioLanguage);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAudioPrimaryLanguage(const int32_t handle, std::string &primaryAudioLanguage) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioPrimaryLanguage(handle, primaryAudioLanguage);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::SetAudioSecondaryLanguage(const int32_t handle, const std::string& secondaryAudioLanguage) {
        DSLOG_INFO("handle=%d, secondaryAudioLanguage=%s", handle, secondaryAudioLanguage.c_str());
        uint32_t result = _audio.SetAudioSecondaryLanguage(handle, secondaryAudioLanguage);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAudioSecondaryLanguage(const int32_t handle, std::string &secondaryAudioLanguage) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioSecondaryLanguage(handle, secondaryAudioLanguage);
        return result;
    }

    // Output connection status
    Core::hresult DeviceSettingsAudioImpl::IsAudioOutputConnected(const int32_t handle, bool &isConnected) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.IsAudioOutputConnected(handle, isConnected);
        return result;
    }

    // Dolby Atmos
    Core::hresult DeviceSettingsAudioImpl::GetAudioSinkDeviceAtmosCapability(const int32_t handle, DolbyAtmosCapability &atmosCapability) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioSinkDeviceAtmosCapability(handle, atmosCapability);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::SetAudioAtmosOutputMode(const int32_t handle, const bool enable) {
        DSLOG_INFO("handle=%d, enable=%s", handle, enable ? "true" : "false");
        uint32_t result = _audio.SetAudioAtmosOutputMode(handle, enable);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetSupportedCompressions(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetSupportedCompressions(handle, compressions);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetAudioCompression(const int32_t handle, AudioCompression &compression) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetAudioCompression(handle, compression);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::SetAudioCompression(const int32_t handle, const AudioCompression compression) {
        DSLOG_INFO("handle=%d, compression=%d", handle, compression);
        uint32_t result = _audio.SetAudioCompression(handle, compression);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetMS12Capabilities(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetMS12Capabilities(handle, compressions);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::GetStereoAuto(const int32_t handle, int32_t &mode) {
        DSLOG_INFO("handle=%d", handle);
        uint32_t result = _audio.GetStereoAuto(handle, mode);
        return result;
    }

    Core::hresult DeviceSettingsAudioImpl::SetStereoAuto(const int32_t handle, const int32_t mode, const bool persist) {
        DSLOG_INFO("handle=%d, mode=%d, persist=%s", handle, mode, persist ? "true" : "false");
        uint32_t result = _audio.SetStereoAuto(handle, mode, persist);
        return result;
    }

    // Missing Audio interface methods implementation
    
    Core::hresult DeviceSettingsAudioImpl::IsAudioPortEnabled(const int32_t handle, bool &enabled) {
        uint32_t result = _audio.IsAudioPortEnabled(handle, enabled);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::EnableAudioPort(const int32_t handle, const bool enable) {
        uint32_t result = _audio.EnableAudioPort(handle, enable);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetSupportedARCTypes(const int32_t handle, int32_t &types) {
        uint32_t result = _audio.GetSupportedARCTypes(handle, types);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetSAD(const int32_t handle, const uint8_t sadList[], const uint8_t count) {
        uint32_t result = _audio.SetSAD(handle, sadList, count);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::EnableARC(const int32_t handle, const AudioARCStatus arcStatus) {
        uint32_t result = _audio.EnableARC(handle, arcStatus);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioEnablePersist(const int32_t handle, bool &enabled, string &portName) {
        uint32_t result = _audio.GetAudioEnablePersist(handle, enabled, portName);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioEnablePersist(const int32_t handle, const bool enable, const string& portName) {
        uint32_t result = _audio.SetAudioEnablePersist(handle, enable, portName);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::IsAudioMSDecoded(const int32_t handle, bool &hasms11Decode) {
        uint32_t result = _audio.IsAudioMSDecoded(handle, hasms11Decode);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::IsAudioMS12Decoded(const int32_t handle, bool &hasms12Decode) {
        uint32_t result = _audio.IsAudioMS12Decoded(handle, hasms12Decode);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioLEConfig(const int32_t handle, bool &enabled) {
        uint32_t result = _audio.GetAudioLEConfig(handle, enabled);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::EnableAudioLEConfig(const int32_t handle, const bool enable) {
        uint32_t result = _audio.EnableAudioLEConfig(handle, enable);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioDelay(const int32_t handle, const uint32_t audioDelay) {
        uint32_t result = _audio.SetAudioDelay(handle, audioDelay);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioDelay(const int32_t handle, uint32_t &audioDelay) {
        uint32_t result = _audio.GetAudioDelay(handle, audioDelay);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioDelayOffset(const int32_t handle, const uint32_t delayOffset) {
        uint32_t result = _audio.SetAudioDelayOffset(handle, delayOffset);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioDelayOffset(const int32_t handle, uint32_t &delayOffset) {
        uint32_t result = _audio.GetAudioDelayOffset(handle, delayOffset);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioCompression(const int32_t handle, const int32_t compressionLevel) {
        uint32_t result = _audio.SetAudioCompression(handle, compressionLevel);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioCompression(const int32_t handle, int32_t &compressionLevel) {
        uint32_t result = _audio.GetAudioCompression(handle, compressionLevel);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioDialogEnhancement(const int32_t handle, const int32_t level) {
        uint32_t result = _audio.SetAudioDialogEnhancement(handle, level);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioDialogEnhancement(const int32_t handle, int32_t &level) {
        uint32_t result = _audio.GetAudioDialogEnhancement(handle, level);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioDolbyVolumeMode(const int32_t handle, const bool enable) {
        uint32_t result = _audio.SetAudioDolbyVolumeMode(handle, enable);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioDolbyVolumeMode(const int32_t handle, bool &enabled) {
        uint32_t result = _audio.GetAudioDolbyVolumeMode(handle, enabled);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioIntelligentEqualizerMode(const int32_t handle, const int32_t mode) {
        uint32_t result = _audio.SetAudioIntelligentEqualizerMode(handle, mode);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioIntelligentEqualizerMode(const int32_t handle, int32_t &mode) {
        uint32_t result = _audio.GetAudioIntelligentEqualizerMode(handle, mode);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioVolumeLeveller(const int32_t handle, const VolumeLeveller volumeLeveller) {
        uint32_t result = _audio.SetAudioVolumeLeveller(handle, volumeLeveller);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioVolumeLeveller(const int32_t handle, VolumeLeveller &volumeLeveller) {
        uint32_t result = _audio.GetAudioVolumeLeveller(handle, volumeLeveller);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioBassEnhancer(const int32_t handle, const int32_t boost) {
        uint32_t result = _audio.SetAudioBassEnhancer(handle, boost);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioBassEnhancer(const int32_t handle, int32_t &boost) {
        uint32_t result = _audio.GetAudioBassEnhancer(handle, boost);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::EnableAudioSurroundDecoder(const int32_t handle, const bool enable) {
        uint32_t result = _audio.EnableAudioSurroundDecoder(handle, enable);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::IsAudioSurroundDecoderEnabled(const int32_t handle, bool &enabled) {
        uint32_t result = _audio.IsAudioSurroundDecoderEnabled(handle, enabled);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioDRCMode(const int32_t handle, const int32_t drcMode) {
        uint32_t result = _audio.SetAudioDRCMode(handle, drcMode);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioDRCMode(const int32_t handle, int32_t &drcMode) {
        uint32_t result = _audio.GetAudioDRCMode(handle, drcMode);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioSurroundVirtualizer(const int32_t handle, const SurroundVirtualizer surroundVirtualizer) {
        uint32_t result = _audio.SetAudioSurroudVirtualizer(handle, surroundVirtualizer);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioSurroundVirtualizer(const int32_t handle, SurroundVirtualizer &surroundVirtualizer) {
        uint32_t result = _audio.GetAudioSurroudVirtualizer(handle, surroundVirtualizer);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioMISteering(const int32_t handle, const bool enable) {
        uint32_t result = _audio.SetAudioMISteering(handle, enable);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioMISteering(const int32_t handle, bool &enable) {
        uint32_t result = _audio.GetAudioMISteering(handle, enable);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioGraphicEqualizerMode(const int32_t handle, const int32_t mode) {
        uint32_t result = _audio.SetAudioGraphicEqualizerMode(handle, mode);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioGraphicEqualizerMode(const int32_t handle, int32_t &mode) {
        uint32_t result = _audio.GetAudioGraphicEqualizerMode(handle, mode);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioMS12ProfileList(const int32_t handle, IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) const {
        uint32_t result = _audio.GetAudioMS12ProfileList(handle, ms12ProfileList);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioMS12Profile(const int32_t handle, string &profile) {
        uint32_t result = _audio.GetAudioMS12Profile(handle, profile);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioMS12Profile(const int32_t handle, const string& profile) {
        uint32_t result = _audio.SetAudioMS12Profile(handle, profile);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioMixerLevels(const int32_t handle, const AudioInput audioInput, const int32_t volume) {
        uint32_t result = _audio.SetAudioMixerLevels(handle, audioInput, volume);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::SetAudioMS12SettingsOverride(const int32_t handle, const string& profileName, const string& profileSettingsName, const string& profileSettingValue, const AudioMS12ProfileState profileState) {
        /* Convert AudioMS12ProfileState enum to the string ("ADD"/"REMOVE") expected
         * by the Audio layer and dAudioImpl.h platform layer. */
        string stateStr = (profileState == AudioMS12ProfileState::AUDIO_MS12_PROFILE_STATE_ADD) ? "ADD" : "REMOVE";
        uint32_t result = _audio.SetAudioMS12SettingsOverride(handle, profileName, profileSettingsName, profileSettingValue, stateStr);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::ResetAudioDialogEnhancement(const int32_t handle) {
        uint32_t result = _audio.ResetAudioDialogEnhancement(handle);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::ResetAudioBassEnhancer(const int32_t handle) {
        uint32_t result = _audio.ResetAudioBassEnhancer(handle);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::ResetAudioSurroundVirtualizer(const int32_t handle) {
        uint32_t result = _audio.ResetAudioSurroundVirtualizer(handle);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::ResetAudioVolumeLeveller(const int32_t handle) {
        uint32_t result = _audio.ResetAudioVolumeLeveller(handle);
        return result;
    }
    
    Core::hresult DeviceSettingsAudioImpl::GetAudioHDMIARCPortId(const int32_t handle, int32_t &portId) {
        uint32_t result = _audio.GetAudioHDMIARCPortId(handle, portId);
        return result;
    }

    void DeviceSettingsAudioImpl::getCachedConfigs(
        std::vector<Exchange::IDeviceSettings::AudioTypeConfigInfo>& audioTypes,
        std::vector<Exchange::IDeviceSettings::AudioPortConfigInfo>& audioPorts) const
    {
        _configLock.Lock();

        // AudioTypeConfigInfo is identical in IDeviceSettings — direct assignment
        audioTypes.assign(_cachedAudioTypeConfigs.begin(), _cachedAudioTypeConfigs.end());

        // AudioPortConfigInfo still differs (AudioPortType enum → int32_t) — keep cast
        audioPorts.reserve(_cachedAudioPortConfigs.size());
        for (const auto& src : _cachedAudioPortConfigs) {
            audioPorts.push_back({static_cast<int32_t>(src.audioPortType), src.audioPortIndex,
                src.connectedVideoPortType, src.connectedVideoPortIndex});
        }

        _configLock.Unlock();
    }

} // namespace Plugin
} // namespace WPEFramework