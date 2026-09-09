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

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <core/Portability.h>
#include <core/Proxy.h>
#include <core/Trace.h>

#include <com/com.h>
#include <core/core.h>
#include <plugins/plugins.h>

#include <interfaces/IDeviceSettingsAudio.h>

#include "dsUtl.h"
#include "dsError.h"
#include "dsAudio.h"

#include "hal/dAudio.h"
#include "hal/dAudioImpl.h"
#include "DeviceSettingsTypes.h"

using namespace WPEFramework::Exchange;

class Audio {
public:
    class INotification {

        public:
            virtual ~INotification() = default;
            virtual void OnAssociatedAudioMixingChanged(bool mixing) = 0;
            virtual void OnAudioFaderControlChanged(int32_t mixerBalance) = 0;
            virtual void OnAudioPrimaryLanguageChanged(const std::string& primaryLanguage) = 0;
            virtual void OnAudioSecondaryLanguageChanged(const std::string& secondaryLanguage) = 0;
            virtual void OnAudioOutHotPlug(AudioPortType portType, uint32_t uiPortNumber, bool isPortConnected) = 0;
            virtual void OnAudioFormatUpdate(AudioFormat audioFormat) = 0;
            virtual void OnDolbyAtmosCapabilitiesChanged(DolbyAtmosCapability atmosCapability, bool status) = 0;
            virtual void OnAudioPortStateChanged(AudioPortState audioPortState) = 0;
            virtual void OnAudioLevelChanged(int32_t audioLevel) = 0;
            virtual void OnAudioModeEvent(AudioPortType audioPortType, AudioStereoMode audioMode) = 0;
    };

private:
    using IPlatform = hal::dAudio::IPlatform;
    using DefaultImpl = dAudioImpl;

    std::shared_ptr<IPlatform> _platform;
    INotification& _parent;

public:

    void Platform_init();
    /** Deferred HAL init — called from DeviceSettingsImp::Configure() */
    void InitialiseHAL() { std::static_pointer_cast<DefaultImpl>(_platform)->InitialiseHAL(); }

    // Audio Port Management
    uint32_t GetAudioPort(const AudioPortType type, const int32_t index, int32_t &handle);
    uint32_t GetMS12Capabilities(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions);
    uint32_t GetAudioCapabilities(const int32_t handle, int32_t &capabilities);
    uint32_t GetAudioMS12Capabilities(const int32_t handle, int32_t &capabilities);

    // Audio Format & Encoding
    uint32_t GetAudioFormat(const int32_t handle, AudioFormat &audioFormat);
    uint32_t GetAudioEncoding(const int32_t handle, AudioEncoding &encoding);
    uint32_t GetSupportedCompressions(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions);
    uint32_t GetAudioCompression(const int32_t handle, AudioCompression &compression);
    uint32_t SetAudioCompression(const int32_t handle, const AudioCompression compression);

    // Audio Level & Volume Control
    uint32_t SetAudioLevel(const int32_t handle, const float audioLevel);
    uint32_t GetAudioLevel(const int32_t handle, float &audioLevel);
    uint32_t SetAudioGain(const int32_t handle, const float gainLevel);
    uint32_t GetAudioGain(const int32_t handle, float &gainLevel);
    uint32_t SetAudioMute(const int32_t handle, const bool mute);
    uint32_t IsAudioMuted(const int32_t handle, bool &muted);

    // Audio Ducking
    uint32_t SetAudioDucking(const int32_t handle, const AudioDuckingType duckingType, const AudioDuckingAction duckingAction, const uint8_t level);

    // Stereo Mode
    uint32_t GetStereoMode(const int32_t handle, AudioStereoMode &mode, const bool persist);
    uint32_t SetStereoMode(const int32_t handle, const AudioStereoMode mode, const bool persist);
    uint32_t GetStereoAuto(const int32_t handle, int32_t &mode);
    uint32_t SetStereoAuto(const int32_t handle, const int32_t mode, const bool persist);

    // Associated Audio Mixing
    uint32_t SetAssociatedAudioMixing(const int32_t handle, const bool mixing);
    uint32_t GetAssociatedAudioMixing(const int32_t handle, bool &mixing);

    // Audio Fader Control
    uint32_t SetAudioFaderControl(const int32_t handle, const int32_t mixerBalance);
    uint32_t GetAudioFaderControl(const int32_t handle, int32_t &mixerBalance);

    // Audio Language Settings
    uint32_t SetAudioPrimaryLanguage(const int32_t handle, const std::string& primaryAudioLanguage);
    uint32_t GetAudioPrimaryLanguage(const int32_t handle, std::string &primaryAudioLanguage);
    uint32_t SetAudioSecondaryLanguage(const int32_t handle, const std::string& secondaryAudioLanguage);
    uint32_t GetAudioSecondaryLanguage(const int32_t handle, std::string &secondaryAudioLanguage);

    // Output Connection Status
    uint32_t IsAudioOutputConnected(const int32_t handle, bool &isConnected);

    // Dolby Atmos
    uint32_t GetAudioSinkDeviceAtmosCapability(const int32_t handle, DolbyAtmosCapability &atmosCapability);
    uint32_t SetAudioAtmosOutputMode(const int32_t handle, const bool enable);

    // Additional Audio Port Methods
    uint32_t IsAudioPortEnabled(const int32_t handle, bool &enabled);
    uint32_t EnableAudioPort(const int32_t handle, const bool enable);
    uint32_t GetSupportedARCTypes(const int32_t handle, int32_t &types);
    uint32_t SetSAD(const int32_t handle, const uint8_t sadList[], const uint8_t count);
    uint32_t EnableARC(const int32_t handle, const AudioARCStatus arcStatus);

    // Audio Persistence Configuration
    uint32_t GetAudioEnablePersist(const int32_t handle, bool &enabled, std::string &portName);
    uint32_t SetAudioEnablePersist(const int32_t handle, const bool enable, const std::string portName);

    // Audio Decoder Status
    uint32_t IsAudioMSDecoded(const int32_t handle, bool &hasms11Decode);
    uint32_t IsAudioMS12Decoded(const int32_t handle, bool &hasms12Decode);

    // Loudness Equivalence Configuration
    uint32_t GetAudioLEConfig(const int32_t handle, bool &enabled);
    uint32_t EnableAudioLEConfig(const int32_t handle, const bool enable);

    // Application Audio Configuration
    uint32_t SetApplicationAudioConfig(const int32_t handle, const std::string& audioConfig, const bool enable);
    uint32_t GetApplicationAudioConfig(const int32_t handle, const std::string& audioConfig, bool& enabled);
    uint32_t GetApplicationAudioConfigList(const int32_t handle, IDeviceSettingsAudioApplicationConfigIterator*& configList) const;

    // Audio Delay Controls
    uint32_t SetAudioDelay(const int32_t handle, const uint32_t audioDelay);
    uint32_t GetAudioDelay(const int32_t handle, uint32_t &audioDelay);
    uint32_t SetAudioDelayOffset(const int32_t handle, const uint32_t delayOffset);
    uint32_t GetAudioDelayOffset(const int32_t handle, uint32_t &delayOffset);

    // Audio Dynamic Range Control
    uint32_t SetAudioCompression(const int32_t handle, const int32_t compressionLevel);
    uint32_t GetAudioCompression(const int32_t handle, int32_t &compressionLevel);

    // Dialog Enhancement
    uint32_t SetAudioDialogEnhancement(const int32_t handle, const int32_t level);
    uint32_t GetAudioDialogEnhancement(const int32_t handle, int32_t &level);

    // Dolby Volume Mode
    uint32_t SetAudioDolbyVolumeMode(const int32_t handle, const bool enable);
    uint32_t GetAudioDolbyVolumeMode(const int32_t handle, bool &enabled);

    // Intelligent Equalizer
    uint32_t SetAudioIntelligentEqualizerMode(const int32_t handle, const int32_t mode);
    uint32_t GetAudioIntelligentEqualizerMode(const int32_t handle, int32_t &mode);

    // Volume Leveller
    uint32_t SetAudioVolumeLeveller(const int32_t handle, const VolumeLeveller volumeLeveller);
    uint32_t GetAudioVolumeLeveller(const int32_t handle, VolumeLeveller &volumeLeveller);

    // Bass Enhancer
    uint32_t SetAudioBassEnhancer(const int32_t handle, const int32_t boost);
    uint32_t GetAudioBassEnhancer(const int32_t handle, int32_t &boost);

    // Surround Decoder
    uint32_t EnableAudioSurroundDecoder(const int32_t handle, const bool enable);
    uint32_t IsAudioSurroundDecoderEnabled(const int32_t handle, bool &enabled);

    // DRC Mode
    uint32_t SetAudioDRCMode(const int32_t handle, const int32_t drcMode);
    uint32_t GetAudioDRCMode(const int32_t handle, int32_t &drcMode);

    // Surround Virtualizer
    uint32_t SetAudioSurroudVirtualizer(const int32_t handle, const SurroundVirtualizer surroundVirtualizer);
    uint32_t GetAudioSurroudVirtualizer(const int32_t handle, SurroundVirtualizer &surroundVirtualizer);

    // MI Steering
    uint32_t SetAudioMISteering(const int32_t handle, const bool enable);
    uint32_t GetAudioMISteering(const int32_t handle, bool &enable);

    // Graphic Equalizer
    uint32_t SetAudioGraphicEqualizerMode(const int32_t handle, const int32_t mode);
    uint32_t GetAudioGraphicEqualizerMode(const int32_t handle, int32_t &mode);

    // MS12 Profile Management
    uint32_t GetAudioMS12ProfileList(const int32_t handle, IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) const;
    uint32_t GetAudioMS12Profile(const int32_t handle, std::string &profile);
    uint32_t SetAudioMS12Profile(const int32_t handle, const std::string& profile);

    // Audio Mixer Levels
    uint32_t SetAudioMixerLevels(const int32_t handle, const AudioInput audioInput, const int32_t volume);

    // MS12 Settings Override
    uint32_t SetAudioMS12SettingsOverride(const int32_t handle, const std::string& profileName, const std::string& profileSettingsName, const std::string& profileSettingValue, const std::string profileState);

    // Reset Functions
    uint32_t ResetAudioDialogEnhancement(const int32_t handle);
    uint32_t ResetAudioBassEnhancer(const int32_t handle);
    uint32_t ResetAudioSurroundVirtualizer(const int32_t handle);
    uint32_t ResetAudioVolumeLeveller(const int32_t handle);

    // HDMI ARC
    uint32_t GetAudioHDMIARCPortId(const int32_t handle, int32_t &portId);

    // Event handler methods for audio state changes
    void OnAssociatedAudioMixingChanged(bool mixing);
    void OnAudioFaderControlChanged(int32_t mixerBalance);
    void OnAudioPrimaryLanguageChanged(const std::string& primaryLanguage);
    void OnAudioSecondaryLanguageChanged(const std::string& secondaryLanguage);
    void OnAudioPortStateChanged(AudioPortState audioPortState);
    void OnAudioLevelChanged(float audioLevel);
    void OnAudioModeChanged(AudioPortType portType, AudioStereoMode mode);
    void OnAudioFormatUpdate(AudioFormat audioFormat);
    void OnAudioOutHotPlug(AudioPortType portType, uint32_t portNumber, bool isPortConnected);
    void OnDolbyAtmosCapabilitiesChanged(DolbyAtmosCapability atmosCapability, bool status);

    template <typename IMPL = DefaultImpl, typename... Args>
    static Audio Create(INotification& parent, Args&&... args)
    {
        ENTRY_LOG;
        static_assert(std::is_base_of<IPlatform, IMPL>::value, "Impl must derive from hal::dAudio::IPlatform");
        auto impl = std::shared_ptr<IMPL>(new IMPL(std::forward<Args>(args)...));
        ASSERT(impl != nullptr);
        EXIT_LOG;
        return Audio(parent, std::move(impl));
    }
    
    private:
    Audio(INotification& parent, std::shared_ptr<IPlatform> platform);

    inline IPlatform& platform() const
    {
        return *_platform;
    }
};