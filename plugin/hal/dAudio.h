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
#pragma once

#include "dsAudio.h"
#include "dsError.h"
#include "dsUtl.h"
#include "dsTypes.h"
#include <cstdint>

#include <core/Portability.h>
#include <interfaces/IDeviceSettingsAudio.h>
#include "Module.h"
#include "DeviceSettingsTypes.h"

using namespace WPEFramework::Exchange;

namespace hal {
namespace dAudio {

    class IPlatform {

    public:
        virtual ~IPlatform() = default;

        // Callback management
        virtual void setAllCallbacks(const CallbackBundle bundle) = 0;
        virtual void getPersistenceValue() = 0;
        
        // Static callback functions for HAL events
        static void audioOutPortConnectCallback(dsAudioPortType_t portType, unsigned int uiPortNo, bool isPortConnected);
        static void audioFormatUpdateCallback(dsAudioFormat_t audioFormat);
        static void audioAtmosCapsChangeCallback(dsATMOSCapability_t atmosCaps, bool status);
        
        // Event notification functions (static helpers)
        static void notifyAssociatedAudioMixingChanged(bool mixing);
        static void notifyAudioFaderControlChanged(int32_t mixerBalance);
        static void notifyAudioPrimaryLanguageChanged(const std::string& primaryLanguage);
        static void notifyAudioSecondaryLanguageChanged(const std::string& secondaryLanguage);
        static void notifyAudioPortStateChanged(AudioPortType portType, bool enabled);
        static void notifyAudioLevelChanged(int32_t audioLevel);
        static void notifyAudioModeChanged(AudioPortType portType, AudioStereoMode mode);

        // Audio Platform interface methods - all pure virtual
        virtual uint32_t GetAudioPort(const AudioPortType type, const int32_t index, int32_t &handle) = 0;
        // GetAudioPorts and GetSupportedAudioPorts methods removed - iterator type doesn't exist in interface
        virtual uint32_t GetAudioCapabilities(const int32_t handle, int32_t &capabilities) = 0;
        virtual uint32_t GetAudioMS12Capabilities(const int32_t handle, int32_t &capabilities) = 0;

        // Audio format and encoding
        virtual uint32_t GetAudioFormat(const int32_t handle, AudioFormat &audioFormat) = 0;
        virtual uint32_t GetAudioEncoding(const int32_t handle, AudioEncoding &encoding) = 0;
        virtual uint32_t GetSupportedCompressions(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions) = 0;
        virtual uint32_t GetAudioCompression(const int32_t handle, AudioCompression &compression) = 0;
        virtual uint32_t SetAudioCompression(const int32_t handle, const AudioCompression compression) = 0;

        // Audio level and volume control
        virtual uint32_t SetAudioLevel(const int32_t handle, const float audioLevel) = 0;
        virtual uint32_t GetAudioLevel(const int32_t handle, float &audioLevel) = 0;
        virtual uint32_t SetAudioGain(const int32_t handle, const float gainLevel) = 0;
        virtual uint32_t GetAudioGain(const int32_t handle, float &gainLevel) = 0;
        virtual uint32_t SetAudioMute(const int32_t handle, const bool mute) = 0;
        virtual uint32_t IsAudioMuted(const int32_t handle, bool &muted) = 0;

        // Audio ducking
        virtual uint32_t SetAudioDucking(const int32_t handle, const AudioDuckingType duckingType, const AudioDuckingAction duckingAction, const uint8_t level) = 0;

        // Stereo mode (needs to use AudioStereoMode to avoid HAL conflict)
        virtual uint32_t GetStereoMode(const int32_t handle, AudioStereoMode &mode, const bool persist) = 0;
        virtual uint32_t SetStereoMode(const int32_t handle, const AudioStereoMode mode, const bool persist) = 0;

        // Associated audio mixing
        virtual uint32_t SetAssociatedAudioMixing(const int32_t handle, const bool mixing) = 0;
        virtual uint32_t GetAssociatedAudioMixing(const int32_t handle, bool &mixing) = 0;

        // Audio fader control
        virtual uint32_t SetAudioFaderControl(const int32_t handle, const int32_t mixerBalance) = 0;
        virtual uint32_t GetAudioFaderControl(const int32_t handle, int32_t &mixerBalance) = 0;

        // Audio language settings
        virtual uint32_t SetAudioPrimaryLanguage(const int32_t handle, const std::string& primaryAudioLanguage) = 0;
        virtual uint32_t GetAudioPrimaryLanguage(const int32_t handle, std::string &primaryAudioLanguage) = 0;
        virtual uint32_t SetAudioSecondaryLanguage(const int32_t handle, const std::string& secondaryAudioLanguage) = 0;
        virtual uint32_t GetAudioSecondaryLanguage(const int32_t handle, std::string &secondaryAudioLanguage) = 0;

        // Output connection status
        virtual uint32_t IsAudioOutputConnected(const int32_t handle, bool &isConnected) = 0;

        // Dolby Atmos
        virtual uint32_t GetAudioSinkDeviceAtmosCapability(const int32_t handle, DolbyAtmosCapability &atmosCapability) = 0;
        virtual uint32_t SetAudioAtmosOutputMode(const int32_t handle, const bool enable) = 0;

        // Audio port control
        virtual uint32_t IsAudioPortEnabled(const int32_t handle, bool &enabled) = 0;
        virtual uint32_t EnableAudioPort(const int32_t handle, const bool enable) = 0;
        virtual uint32_t GetSupportedARCTypes(const int32_t handle, int32_t &types) = 0;
        virtual uint32_t SetSAD(const int32_t handle, const uint8_t sadList[], const uint8_t count) = 0;
        virtual uint32_t EnableARC(const int32_t handle, const WPEFramework::Exchange::IDeviceSettingsAudio::AudioARCStatus arcStatus) = 0;

        // Persistence
        virtual uint32_t GetAudioEnablePersist(const int32_t handle, bool &enabled, std::string &portName) = 0;
        virtual uint32_t SetAudioEnablePersist(const int32_t handle, const bool enable, const std::string portName) = 0;

        // MS decode status
        virtual uint32_t IsAudioMSDecoded(const int32_t handle, bool &hasms11Decode) = 0;
        virtual uint32_t IsAudioMS12Decoded(const int32_t handle, bool &hasms12Decode) = 0;

        // LE config
        virtual uint32_t GetAudioLEConfig(const int32_t handle, bool &enabled) = 0;
        virtual uint32_t EnableAudioLEConfig(const int32_t handle, const bool enable) = 0;

        // Audio delay
        virtual uint32_t SetAudioDelay(const int32_t handle, const uint32_t audioDelay) = 0;
        virtual uint32_t GetAudioDelay(const int32_t handle, uint32_t &audioDelay) = 0;
        virtual uint32_t SetAudioDelayOffset(const int32_t handle, const uint32_t delayOffset) = 0;
        virtual uint32_t GetAudioDelayOffset(const int32_t handle, uint32_t &delayOffset) = 0;

        // Audio compression
        virtual uint32_t SetAudioCompression(const int32_t handle, const int32_t compressionLevel) = 0;
        virtual uint32_t GetAudioCompression(const int32_t handle, int32_t &compressionLevel) = 0;

        // Dialog enhancement
        virtual uint32_t SetAudioDialogEnhancement(const int32_t handle, const int32_t level) = 0;
        virtual uint32_t GetAudioDialogEnhancement(const int32_t handle, int32_t &level) = 0;

        // Dolby volume mode
        virtual uint32_t SetAudioDolbyVolumeMode(const int32_t handle, const bool enable) = 0;
        virtual uint32_t GetAudioDolbyVolumeMode(const int32_t handle, bool &enabled) = 0;

        // Intelligent equalizer
        virtual uint32_t SetAudioIntelligentEqualizerMode(const int32_t handle, const int32_t mode) = 0;
        virtual uint32_t GetAudioIntelligentEqualizerMode(const int32_t handle, int32_t &mode) = 0;

        // Volume leveller
        virtual uint32_t SetAudioVolumeLeveller(const int32_t handle, const WPEFramework::Exchange::IDeviceSettingsAudio::VolumeLeveller volumeLeveller) = 0;
        virtual uint32_t GetAudioVolumeLeveller(const int32_t handle, WPEFramework::Exchange::IDeviceSettingsAudio::VolumeLeveller &volumeLeveller) = 0;

        // Bass enhancer
        virtual uint32_t SetAudioBassEnhancer(const int32_t handle, const int32_t boost) = 0;
        virtual uint32_t GetAudioBassEnhancer(const int32_t handle, int32_t &boost) = 0;

        // Surround decoder
        virtual uint32_t EnableAudioSurroudDecoder(const int32_t handle, const bool enable) = 0;
        virtual uint32_t IsAudioSurroudDecoderEnabled(const int32_t handle, bool &enabled) = 0;

        // DRC mode
        virtual uint32_t SetAudioDRCMode(const int32_t handle, const int32_t drcMode) = 0;
        virtual uint32_t GetAudioDRCMode(const int32_t handle, int32_t &drcMode) = 0;

        // Surround virtualizer
        virtual uint32_t SetAudioSurroudVirtualizer(const int32_t handle, const WPEFramework::Exchange::IDeviceSettingsAudio::SurroundVirtualizer surroundVirtualizer) = 0;
        virtual uint32_t GetAudioSurroudVirtualizer(const int32_t handle, WPEFramework::Exchange::IDeviceSettingsAudio::SurroundVirtualizer &surroundVirtualizer) = 0;

        // MI Steering
        virtual uint32_t SetAudioMISteering(const int32_t handle, const bool enable) = 0;
        virtual uint32_t GetAudioMISteering(const int32_t handle, bool &enable) = 0;

        // Graphic equalizer
        virtual uint32_t SetAudioGraphicEqualizerMode(const int32_t handle, const int32_t mode) = 0;
        virtual uint32_t GetAudioGraphicEqualizerMode(const int32_t handle, int32_t &mode) = 0;

        // MS12 profile
        virtual uint32_t GetAudioMS12ProfileList(const int32_t handle, WPEFramework::Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) const = 0;
        virtual uint32_t GetAudioMS12Profile(const int32_t handle, std::string &profile) = 0;
        virtual uint32_t SetAudioMS12Profile(const int32_t handle, const std::string& profile) = 0;

        // Mixer levels
        virtual uint32_t SetAudioMixerLevels(const int32_t handle, const WPEFramework::Exchange::IDeviceSettingsAudio::AudioInput audioInput, const int32_t volume) = 0;

        // MS12 settings override
        virtual uint32_t SetAudioMS12SettingsOverride(const int32_t handle, const std::string profileName, const std::string profileSettingsName, const std::string profileSettingValue, const std::string profileState) = 0;

        // Reset methods
        virtual uint32_t ResetAudioDialogEnhancement(const int32_t handle) = 0;
        virtual uint32_t ResetAudioBassEnhancer(const int32_t handle) = 0;
        virtual uint32_t ResetAudioSurroundVirtualizer(const int32_t handle) = 0;
        virtual uint32_t ResetAudioVolumeLeveller(const int32_t handle) = 0;

        // HDMI ARC Port ID
        virtual uint32_t GetAudioHDMIARCPortId(const int32_t handle, int32_t &portId) = 0;

        // Stereo auto mode
        virtual uint32_t GetStereoAuto(const int32_t handle, int32_t &mode) = 0;
        virtual uint32_t SetStereoAuto(const int32_t handle, const int32_t mode, const bool persist) = 0;
    };

} // namespace dAudio
} // namespace hal