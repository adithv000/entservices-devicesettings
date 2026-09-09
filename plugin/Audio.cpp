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

#include <functional>
#include <unistd.h>

#include <core/IAction.h>
#include <core/Time.h>
#include <core/WorkerPool.h>

#include "secure_wrapper.h"
#include "Audio.h"

Audio::Audio(INotification& parent, std::shared_ptr<IPlatform> platform)
    : _platform(std::move(platform))
    , _parent(parent)
{
    Platform_init();
}

void Audio::Platform_init()
{
    CallbackBundle bundle;
    bundle.OnAudioOutHotPlug = [this](AudioPortType portType, uint32_t portNumber, bool isConnected) {
        this->OnAudioOutHotPlug(portType, portNumber, isConnected);
    };
    bundle.OnAudioFormatUpdate = [this](AudioFormat audioFormat) {
        this->OnAudioFormatUpdate(audioFormat);
    };
    bundle.OnDolbyAtmosCapabilitiesChanged = [this](DolbyAtmosCapability atmosCaps, bool status) {
        this->OnDolbyAtmosCapabilitiesChanged(atmosCaps, status);
    };
    bundle.OnAssociatedAudioMixingChanged = [this](bool mixing) {
        this->OnAssociatedAudioMixingChanged(mixing);
    };
    bundle.OnAudioFaderControlChanged = [this](int32_t mixerBalance) {
        this->OnAudioFaderControlChanged(mixerBalance);
    };
    bundle.OnAudioPrimaryLanguageChanged = [this](const std::string& primaryLanguage) {
        this->OnAudioPrimaryLanguageChanged(primaryLanguage);
    };
    bundle.OnAudioSecondaryLanguageChanged = [this](const std::string& secondaryLanguage) {
        this->OnAudioSecondaryLanguageChanged(secondaryLanguage);
    };
    bundle.OnAudioPortStateChanged = [this](AudioPortState audioPortState) {
        this->OnAudioPortStateChanged(audioPortState);
    };
    bundle.OnAudioLevelChanged = [this](float audioLevel) {
        this->OnAudioLevelChanged(audioLevel);
    };
    bundle.OnAudioModeChanged = [this](AudioPortType portType, AudioStereoMode mode) {
        this->OnAudioModeChanged(portType, mode);
    };
    if (_platform) {
        this->platform().setAllCallbacks(bundle);
        this->platform().getPersistenceValue();
    }
}

void Audio::OnAudioOutHotPlug(AudioPortType portType, uint32_t portNumber, bool isConnected)
{
    DSLOG_INFO("portType=%d, portNumber=%u, connected=%s", static_cast<int>(portType), portNumber, isConnected ? "true" : "false");
    // Trigger notification to parent for callback dispatch
    _parent.OnAudioOutHotPlug(portType, portNumber, isConnected);
}

void Audio::OnAudioFormatUpdate(AudioFormat audioFormat)
{
    DSLOG_INFO("format=%d", static_cast<int>(audioFormat));
    // Trigger notification to parent for callback dispatch  
    _parent.OnAudioFormatUpdate(audioFormat);
}

void Audio::OnDolbyAtmosCapabilitiesChanged(DolbyAtmosCapability atmosCaps, bool status)
{
    DSLOG_INFO("caps=%d, status=%s", static_cast<int>(atmosCaps), status ? "true" : "false");
    // Trigger notification to parent for callback dispatch
    _parent.OnDolbyAtmosCapabilitiesChanged(atmosCaps, status);
}

void Audio::OnAudioModeChanged(AudioPortType portType, AudioStereoMode mode)
{
    DSLOG_INFO("portType=%d, mode=%d", static_cast<int>(portType), static_cast<int>(mode));
    // Trigger notification to parent for callback dispatch
    _parent.OnAudioModeEvent(portType, mode);
}

// Event handler methods for audio state changes
void Audio::OnAssociatedAudioMixingChanged(bool mixing)
{
    DSLOG_INFO("mixing=%s", mixing ? "enabled" : "disabled");
    // Trigger notification to parent for callback dispatch
    _parent.OnAssociatedAudioMixingChanged(mixing);
}

void Audio::OnAudioFaderControlChanged(int32_t mixerBalance)
{
    DSLOG_INFO("mixerBalance=%d", mixerBalance);
    // Trigger notification to parent for callback dispatch
    _parent.OnAudioFaderControlChanged(mixerBalance);
}

void Audio::OnAudioPrimaryLanguageChanged(const std::string& primaryLanguage)
{
    DSLOG_INFO("primaryLanguage=%s", primaryLanguage.c_str());
    // Trigger notification to parent for callback dispatch
    _parent.OnAudioPrimaryLanguageChanged(primaryLanguage);
}

void Audio::OnAudioSecondaryLanguageChanged(const std::string& secondaryLanguage)
{
    DSLOG_INFO("secondaryLanguage=%s", secondaryLanguage.c_str());
    // Trigger notification to parent for callback dispatch
    _parent.OnAudioSecondaryLanguageChanged(secondaryLanguage);
}

void Audio::OnAudioPortStateChanged(AudioPortState audioPortState)
{
    DSLOG_INFO("audioPortState=%d", static_cast<int>(audioPortState));
    // Trigger notification to parent for callback dispatch
    _parent.OnAudioPortStateChanged(audioPortState);
}

void Audio::OnAudioLevelChanged(float audioLevel)
{
    DSLOG_INFO("audioLevel=%.2f", audioLevel);
    // Trigger notification to parent for callback dispatch
    _parent.OnAudioLevelChanged(static_cast<int32_t>(audioLevel));
}

uint32_t Audio::GetAudioPort(const AudioPortType type, const int32_t index, int32_t &handle) {
    DSLOG_INFO("type=%d, index=%d", type, index);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioPort(type, index, handle);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - type=%d, index=%d, handle=%d", type, index, handle);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioCapabilities(const int32_t handle, int32_t &capabilities) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioCapabilities(handle, capabilities);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, capabilities=%d", handle, capabilities);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioMS12Capabilities(const int32_t handle, int32_t &capabilities) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioMS12Capabilities(handle, capabilities);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, capabilities=%d", handle, capabilities);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioFormat(const int32_t handle, AudioFormat &audioFormat) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioFormat(handle, audioFormat);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, audioFormat=%d", handle, audioFormat);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioEncoding(const int32_t handle, AudioEncoding &encoding) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioEncoding(handle, encoding);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, encoding=%d", handle, encoding);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAudioLevel(const int32_t handle, const float audioLevel) {
    DSLOG_INFO("handle=%d, audioLevel=%.2f", handle, audioLevel);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAudioLevel(handle, audioLevel);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioLevel(const int32_t handle, float &audioLevel) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioLevel(handle, audioLevel);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, audioLevel=%.2f", handle, audioLevel);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAudioGain(const int32_t handle, const float gainLevel) {
    DSLOG_INFO("handle=%d, gainLevel=%.2f", handle, gainLevel);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAudioGain(handle, gainLevel);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioGain(const int32_t handle, float &gainLevel) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioGain(handle, gainLevel);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, gainLevel=%.2f", handle, gainLevel);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAudioMute(const int32_t handle, const bool mute) {
    DSLOG_INFO("handle=%d, mute=%s", handle, mute ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAudioMute(handle, mute);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::IsAudioMuted(const int32_t handle, bool &muted) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().IsAudioMuted(handle, muted);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, muted=%s", handle, muted ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAudioDucking(const int32_t handle, const AudioDuckingType duckingType, const AudioDuckingAction duckingAction, const uint8_t level) {
    DSLOG_INFO("handle=%d, duckingType=%d, duckingAction=%d, level=%d", handle, duckingType, duckingAction, level);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAudioDucking(handle, duckingType, duckingAction, level);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetStereoMode(const int32_t handle, AudioStereoMode &mode, const bool persist) {
    DSLOG_INFO("handle=%d, persist=%s", handle, persist ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetStereoMode(handle, mode, persist);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, mode=%d", handle, mode);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetStereoMode(const int32_t handle, const AudioStereoMode mode, const bool persist) {
    DSLOG_INFO("handle=%d, mode=%d, persist=%s", handle, mode, persist ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetStereoMode(handle, mode, persist);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAssociatedAudioMixing(const int32_t handle, const bool mixing) {
    DSLOG_INFO("handle=%d, mixing=%s", handle, mixing ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAssociatedAudioMixing(handle, mixing);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAssociatedAudioMixing(const int32_t handle, bool &mixing) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAssociatedAudioMixing(handle, mixing);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, mixing=%s", handle, mixing ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAudioFaderControl(const int32_t handle, const int32_t mixerBalance) {
    DSLOG_INFO("handle=%d, mixerBalance=%d", handle, mixerBalance);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAudioFaderControl(handle, mixerBalance);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioFaderControl(const int32_t handle, int32_t &mixerBalance) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioFaderControl(handle, mixerBalance);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, mixerBalance=%d", handle, mixerBalance);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAudioPrimaryLanguage(const int32_t handle, const std::string& primaryAudioLanguage) {
    DSLOG_INFO("handle=%d, primaryAudioLanguage=%s", handle, primaryAudioLanguage.c_str());
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAudioPrimaryLanguage(handle, primaryAudioLanguage);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioPrimaryLanguage(const int32_t handle, std::string &primaryAudioLanguage) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioPrimaryLanguage(handle, primaryAudioLanguage);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, primaryAudioLanguage=%s", handle, primaryAudioLanguage.c_str());
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAudioSecondaryLanguage(const int32_t handle, const std::string& secondaryAudioLanguage) {
    DSLOG_INFO("handle=%d, secondaryAudioLanguage=%s", handle, secondaryAudioLanguage.c_str());
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAudioSecondaryLanguage(handle, secondaryAudioLanguage);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioSecondaryLanguage(const int32_t handle, std::string &secondaryAudioLanguage) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioSecondaryLanguage(handle, secondaryAudioLanguage);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, secondaryAudioLanguage=%s", handle, secondaryAudioLanguage.c_str());
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

// Additional key methods - implementing the most commonly used ones
uint32_t Audio::IsAudioOutputConnected(const int32_t handle, bool &isConnected) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().IsAudioOutputConnected(handle, isConnected);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, isConnected=%s", handle, isConnected ? "true" : "false");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::GetAudioSinkDeviceAtmosCapability(const int32_t handle, DolbyAtmosCapability &atmosCapability) {
    DSLOG_INFO("handle=%d", handle);
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().GetAudioSinkDeviceAtmosCapability(handle, atmosCapability);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - handle=%d, atmosCapability=%d", handle, atmosCapability);
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}

uint32_t Audio::SetAudioAtmosOutputMode(const int32_t handle, const bool enable) {
    DSLOG_INFO("handle=%d, enable=%s", handle, enable ? "true" : "false");
    uint32_t result = WPEFramework::Core::ERROR_GENERAL;
    if (_platform) {
        result = this->platform().SetAudioAtmosOutputMode(handle, enable);
    }
    if (result == WPEFramework::Core::ERROR_NONE) {
        DSLOG_INFO("SUCCESS - platform call completed successfully");
    } else {
        DSLOG_ERR("FAILED - result=%u", result);
    }
    return result;
}


// NOTE: The remaining methods (like SetAudioDelay, GetAudioDelay, etc.) would follow
// the same pattern. For brevity, I'm implementing the key ones that are commonly used
// and that correspond to the notification handlers we saw in DeviceSettingsManager.h

// Missing Audio interface methods implementation

uint32_t Audio::GetSupportedCompressions(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions) {
    uint32_t result = (_platform != nullptr) ? _platform->GetSupportedCompressions(handle, compressions) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioCompression(const int32_t handle, AudioCompression &compression) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioCompression(handle, compression) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioCompression(const int32_t handle, const AudioCompression compression) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioCompression(handle, compression) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

// Missing Audio interface methods implementation

uint32_t Audio::IsAudioPortEnabled(const int32_t handle, bool &enabled) {
    uint32_t result = (_platform != nullptr) ? _platform->IsAudioPortEnabled(handle, enabled) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::EnableAudioPort(const int32_t handle, const bool enable) {
    uint32_t result = (_platform != nullptr) ? _platform->EnableAudioPort(handle, enable) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetSupportedARCTypes(const int32_t handle, int32_t &types) {
    uint32_t result = (_platform != nullptr) ? _platform->GetSupportedARCTypes(handle, types) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetSAD(const int32_t handle, const uint8_t sadList[], const uint8_t count) {
    uint32_t result = (_platform != nullptr) ? _platform->SetSAD(handle, sadList, count) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::EnableARC(const int32_t handle, const AudioARCStatus arcStatus) {
    uint32_t result = (_platform != nullptr) ? _platform->EnableARC(handle, arcStatus) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetStereoAuto(const int32_t handle, int32_t &mode) {
    uint32_t result = (_platform != nullptr) ? _platform->GetStereoAuto(handle, mode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetStereoAuto(const int32_t handle, const int32_t mode, const bool persist) {
    uint32_t result = (_platform != nullptr) ? _platform->SetStereoAuto(handle, mode, persist) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioEnablePersist(const int32_t handle, bool &enabled, string &portName) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioEnablePersist(handle, enabled, portName) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioEnablePersist(const int32_t handle, const bool enable, const string portName) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioEnablePersist(handle, enable, portName) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::IsAudioMSDecoded(const int32_t handle, bool &hasms11Decode) {
    uint32_t result = (_platform != nullptr) ? _platform->IsAudioMSDecoded(handle, hasms11Decode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::IsAudioMS12Decoded(const int32_t handle, bool &hasms12Decode) {
    uint32_t result = (_platform != nullptr) ? _platform->IsAudioMS12Decoded(handle, hasms12Decode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioLEConfig(const int32_t handle, bool &enabled) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioLEConfig(handle, enabled) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::EnableAudioLEConfig(const int32_t handle, const bool enable) {
    uint32_t result = (_platform != nullptr) ? _platform->EnableAudioLEConfig(handle, enable) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetApplicationAudioConfig(const int32_t handle, const string& audioConfig, const bool enable) {
    return (_platform != nullptr) ? _platform->SetApplicationAudioConfig(handle, audioConfig, enable) : WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t Audio::GetApplicationAudioConfig(const int32_t handle, const string& audioConfig, bool& enabled) {
    return (_platform != nullptr) ? _platform->GetApplicationAudioConfig(handle, audioConfig, enabled) : WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t Audio::GetApplicationAudioConfigList(const int32_t handle, IDeviceSettingsAudioApplicationConfigIterator*& configList) const {
    return (_platform != nullptr) ? _platform->GetApplicationAudioConfigList(handle, configList) : WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t Audio::SetAudioDelay(const int32_t handle, const uint32_t audioDelay) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioDelay(handle, audioDelay) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioDelay(const int32_t handle, uint32_t &audioDelay) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioDelay(handle, audioDelay) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioDelayOffset(const int32_t handle, const uint32_t delayOffset) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioDelayOffset(handle, delayOffset) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioDelayOffset(const int32_t handle, uint32_t &delayOffset) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioDelayOffset(handle, delayOffset) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioCompression(const int32_t handle, const int32_t compressionLevel) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioCompression(handle, compressionLevel) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioCompression(const int32_t handle, int32_t &compressionLevel) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioCompression(handle, compressionLevel) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioDialogEnhancement(const int32_t handle, const int32_t level) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioDialogEnhancement(handle, level) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioDialogEnhancement(const int32_t handle, int32_t &level) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioDialogEnhancement(handle, level) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioDolbyVolumeMode(const int32_t handle, const bool enable) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioDolbyVolumeMode(handle, enable) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioDolbyVolumeMode(const int32_t handle, bool &enabled) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioDolbyVolumeMode(handle, enabled) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioIntelligentEqualizerMode(const int32_t handle, const int32_t mode) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioIntelligentEqualizerMode(handle, mode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioIntelligentEqualizerMode(const int32_t handle, int32_t &mode) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioIntelligentEqualizerMode(handle, mode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioVolumeLeveller(const int32_t handle, const VolumeLeveller volumeLeveller) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioVolumeLeveller(handle, volumeLeveller) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioVolumeLeveller(const int32_t handle, VolumeLeveller &volumeLeveller) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioVolumeLeveller(handle, volumeLeveller) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioBassEnhancer(const int32_t handle, const int32_t boost) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioBassEnhancer(handle, boost) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioBassEnhancer(const int32_t handle, int32_t &boost) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioBassEnhancer(handle, boost) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::EnableAudioSurroundDecoder(const int32_t handle, const bool enable) {
    uint32_t result = (_platform != nullptr) ? _platform->EnableAudioSurroudDecoder(handle, enable) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::IsAudioSurroundDecoderEnabled(const int32_t handle, bool &enabled) {
    uint32_t result = (_platform != nullptr) ? _platform->IsAudioSurroudDecoderEnabled(handle, enabled) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioDRCMode(const int32_t handle, const int32_t drcMode) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioDRCMode(handle, drcMode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioDRCMode(const int32_t handle, int32_t &drcMode) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioDRCMode(handle, drcMode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioSurroudVirtualizer(const int32_t handle, const SurroundVirtualizer surroundVirtualizer) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioSurroudVirtualizer(handle, surroundVirtualizer) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioSurroudVirtualizer(const int32_t handle, SurroundVirtualizer &surroundVirtualizer) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioSurroudVirtualizer(handle, surroundVirtualizer) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioMISteering(const int32_t handle, const bool enable) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioMISteering(handle, enable) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioMISteering(const int32_t handle, bool &enable) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioMISteering(handle, enable) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioGraphicEqualizerMode(const int32_t handle, const int32_t mode) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioGraphicEqualizerMode(handle, mode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioGraphicEqualizerMode(const int32_t handle, int32_t &mode) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioGraphicEqualizerMode(handle, mode) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioMS12ProfileList(const int32_t handle, IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) const {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioMS12ProfileList(handle, ms12ProfileList) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioMS12Profile(const int32_t handle, string &profile) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioMS12Profile(handle, profile) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioMS12Profile(const int32_t handle, const string& profile) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioMS12Profile(handle, profile) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioMixerLevels(const int32_t handle, const AudioInput audioInput, const int32_t volume) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioMixerLevels(handle, audioInput, volume) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::SetAudioMS12SettingsOverride(const int32_t handle, const string& profileName, const string& profileSettingsName, const string& profileSettingValue, const string profileState) {
    uint32_t result = (_platform != nullptr) ? _platform->SetAudioMS12SettingsOverride(handle, profileName, profileSettingsName, profileSettingValue, profileState) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::ResetAudioDialogEnhancement(const int32_t handle) {
    uint32_t result = (_platform != nullptr) ? _platform->ResetAudioDialogEnhancement(handle) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::ResetAudioBassEnhancer(const int32_t handle) {
    uint32_t result = (_platform != nullptr) ? _platform->ResetAudioBassEnhancer(handle) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::ResetAudioSurroundVirtualizer(const int32_t handle) {
    uint32_t result = (_platform != nullptr) ? _platform->ResetAudioSurroundVirtualizer(handle) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::ResetAudioVolumeLeveller(const int32_t handle) {
    uint32_t result = (_platform != nullptr) ? _platform->ResetAudioVolumeLeveller(handle) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

uint32_t Audio::GetAudioHDMIARCPortId(const int32_t handle, int32_t &portId) {
    uint32_t result = (_platform != nullptr) ? _platform->GetAudioHDMIARCPortId(handle, portId) : WPEFramework::Core::ERROR_UNAVAILABLE;
    return result;
}

// ... Additional stub implementations would continue here following the same pattern
// For full implementation, each method would need proper platform delegation