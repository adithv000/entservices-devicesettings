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

#include "Module.h"

#include <memory>
#include <unordered_map>
#include <chrono>
#include <cstdint>
#include <list>
#include <vector>
#include <functional>
#include <utility>

#include <com/com.h>
#include <core/core.h>
#include <plugins/plugins.h>

#include <interfaces/IDeviceSettingsAudio.h>
#include <interfaces/IDeviceSettings.h>

#include "Audio.h"
#include "DeviceSettingsTypes.h"

namespace WPEFramework {
namespace Plugin {
    class DeviceSettingsAudioImpl : public Audio::INotification
    {
    public:
        // Note: No need to inherit from Exchange::IDeviceSettingsAudio anymore
        // DeviceSettingsImp handles the WPEFramework interface contract
        // This class only needs Audio::INotification for hardware callbacks
        
        DeviceSettingsAudioImpl();
        ~DeviceSettingsAudioImpl() override;

        static DeviceSettingsAudioImpl* Create()
        {
            return new DeviceSettingsAudioImpl();
        }

        // We do not allow this plugin to be copied !!
        DeviceSettingsAudioImpl(const DeviceSettingsAudioImpl&)            = delete;
        DeviceSettingsAudioImpl& operator=(const DeviceSettingsAudioImpl&) = delete;

        // INTERFACE_MAP not needed - this is an implementation class aggregated by DeviceSettingsImp
        // DeviceSettingsImp handles QueryInterface for all component interfaces

    public:
        class EXTERNAL LambdaJob : public Core::IDispatch {
        protected:
            LambdaJob(DeviceSettingsAudioImpl* impl, std::function<void()> lambda)
                : _impl(impl)
                , _lambda(std::move(lambda))
            {
            }

        public:
            LambdaJob()                            = delete;
            LambdaJob(const LambdaJob&)            = delete;
            LambdaJob& operator=(const LambdaJob&) = delete;
            ~LambdaJob() {}

            static Core::ProxyType<Core::IDispatch> Create(DeviceSettingsAudioImpl* impl, std::function<void()> lambda)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<LambdaJob>::Create(impl, std::move(lambda))));
            }

            virtual void Dispatch()
            {
                _lambda();
            }

        private:
            DeviceSettingsAudioImpl* _impl;
            std::function<void()> _lambda;
        };

    public:
        void InitializeIARM();

        // Audio Port Management
        Core::hresult GetAudioPort(const AudioPortType type, const int32_t index, int32_t &handle);
        Core::hresult GetMS12Capabilities(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions);
        Core::hresult GetAudioCapabilities(const int32_t handle, int32_t &capabilities);
        Core::hresult GetAudioMS12Capabilities(const int32_t handle, int32_t &capabilities);

        // Audio Format & Encoding
        Core::hresult GetAudioFormat(const int32_t handle, AudioFormat &audioFormat);
        Core::hresult GetAudioEncoding(const int32_t handle, AudioEncoding &encoding);
        Core::hresult GetSupportedCompressions(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions);
        Core::hresult GetAudioCompression(const int32_t handle, AudioCompression &compression);
        Core::hresult SetAudioCompression(const int32_t handle, const AudioCompression compression);

        // Audio Level & Volume Control
        Core::hresult SetAudioLevel(const int32_t handle, const float audioLevel);
        Core::hresult GetAudioLevel(const int32_t handle, float &audioLevel);
        Core::hresult SetAudioGain(const int32_t handle, const float gainLevel);
        Core::hresult GetAudioGain(const int32_t handle, float &gainLevel);
        Core::hresult SetAudioMute(const int32_t handle, const bool mute);
        Core::hresult IsAudioMuted(const int32_t handle, bool &muted);

        // Audio Ducking
        Core::hresult SetAudioDucking(const int32_t handle, const AudioDuckingType duckingType, const AudioDuckingAction duckingAction, const uint8_t level);

        // Stereo Mode
        Core::hresult GetStereoMode(const int32_t handle, AudioStereoMode &mode, const bool persist);
        Core::hresult SetStereoMode(const int32_t handle, const AudioStereoMode mode, const bool persist);
        Core::hresult GetStereoAuto(const int32_t handle, int32_t &mode);
        Core::hresult SetStereoAuto(const int32_t handle, const int32_t mode, const bool persist);

        // Associated Audio Mixing
        Core::hresult SetAssociatedAudioMixing(const int32_t handle, const bool mixing);
        Core::hresult GetAssociatedAudioMixing(const int32_t handle, bool &mixing);

        // Audio Fader Control
        Core::hresult SetAudioFaderControl(const int32_t handle, const int32_t mixerBalance);
        Core::hresult GetAudioFaderControl(const int32_t handle, int32_t &mixerBalance);

        // Audio Language Settings
        Core::hresult SetAudioPrimaryLanguage(const int32_t handle, const std::string& primaryAudioLanguage);
        Core::hresult GetAudioPrimaryLanguage(const int32_t handle, std::string &primaryAudioLanguage);
        Core::hresult SetAudioSecondaryLanguage(const int32_t handle, const std::string& secondaryAudioLanguage);
        Core::hresult GetAudioSecondaryLanguage(const int32_t handle, std::string &secondaryAudioLanguage);

        // Output Connection Status
        Core::hresult IsAudioOutputConnected(const int32_t handle, bool &isConnected);

        // Dolby Atmos
        Core::hresult GetAudioSinkDeviceAtmosCapability(const int32_t handle, DolbyAtmosCapability &atmosCapability);
        Core::hresult SetAudioAtmosOutputMode(const int32_t handle, const bool enable);

        // Additional Audio Port Methods
        Core::hresult IsAudioPortEnabled(const int32_t handle, bool &enabled);
        Core::hresult EnableAudioPort(const int32_t handle, const bool enable);
        Core::hresult GetSupportedARCTypes(const int32_t handle, int32_t &types);
        Core::hresult SetSAD(const int32_t handle, const uint8_t sadList[], const uint8_t count);
        Core::hresult EnableARC(const int32_t handle, const AudioARCStatus arcStatus);

        // Audio Persistence Configuration
        Core::hresult GetAudioEnablePersist(const int32_t handle, bool &enabled, std::string &portName);
        Core::hresult SetAudioEnablePersist(const int32_t handle, const bool enable, const std::string& portName);

        // Audio Decoder Status
        Core::hresult IsAudioMSDecoded(const int32_t handle, bool &hasms11Decode);
        Core::hresult IsAudioMS12Decoded(const int32_t handle, bool &hasms12Decode);

        // Loudness Equivalence Configuration
        Core::hresult GetAudioLEConfig(const int32_t handle, bool &enabled);
        Core::hresult EnableAudioLEConfig(const int32_t handle, const bool enable);

        // Audio Delay Controls
        Core::hresult SetAudioDelay(const int32_t handle, const uint32_t audioDelay);
        Core::hresult GetAudioDelay(const int32_t handle, uint32_t &audioDelay);
        Core::hresult SetAudioDelayOffset(const int32_t handle, const uint32_t delayOffset);
        Core::hresult GetAudioDelayOffset(const int32_t handle, uint32_t &delayOffset);

        // Audio Dynamic Range Control
        Core::hresult SetAudioCompression(const int32_t handle, const int32_t compressionLevel);
        Core::hresult GetAudioCompression(const int32_t handle, int32_t &compressionLevel);

        // Dialog Enhancement
        Core::hresult SetAudioDialogEnhancement(const int32_t handle, const int32_t level);
        Core::hresult GetAudioDialogEnhancement(const int32_t handle, int32_t &level);

        // Dolby Volume Mode
        Core::hresult SetAudioDolbyVolumeMode(const int32_t handle, const bool enable);
        Core::hresult GetAudioDolbyVolumeMode(const int32_t handle, bool &enabled);

        // Intelligent Equalizer
        Core::hresult SetAudioIntelligentEqualizerMode(const int32_t handle, const int32_t mode);
        Core::hresult GetAudioIntelligentEqualizerMode(const int32_t handle, int32_t &mode);

        // Volume Leveller
        Core::hresult SetAudioVolumeLeveller(const int32_t handle, const VolumeLeveller volumeLeveller);
        Core::hresult GetAudioVolumeLeveller(const int32_t handle, VolumeLeveller &volumeLeveller);

        // Bass Enhancer
        Core::hresult SetAudioBassEnhancer(const int32_t handle, const int32_t boost);
        Core::hresult GetAudioBassEnhancer(const int32_t handle, int32_t &boost);

        // Surround Decoder
        Core::hresult EnableAudioSurroundDecoder(const int32_t handle, const bool enable);
        Core::hresult IsAudioSurroundDecoderEnabled(const int32_t handle, bool &enabled);

        // DRC Mode
        Core::hresult SetAudioDRCMode(const int32_t handle, const int32_t drcMode);
        Core::hresult GetAudioDRCMode(const int32_t handle, int32_t &drcMode);

        // Surround Virtualizer
        Core::hresult SetAudioSurroundVirtualizer(const int32_t handle, const SurroundVirtualizer surroundVirtualizer);
        Core::hresult GetAudioSurroundVirtualizer(const int32_t handle, SurroundVirtualizer &surroundVirtualizer);

        // MI Steering
        Core::hresult SetAudioMISteering(const int32_t handle, const bool enable);
        Core::hresult GetAudioMISteering(const int32_t handle, bool &enable);

        // Graphic Equalizer
        Core::hresult SetAudioGraphicEqualizerMode(const int32_t handle, const int32_t mode);
        Core::hresult GetAudioGraphicEqualizerMode(const int32_t handle, int32_t &mode);

        // MS12 Profile Management
        Core::hresult GetAudioMS12ProfileList(const int32_t handle, IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) const;
        Core::hresult GetAudioMS12Profile(const int32_t handle, std::string &profile);
        Core::hresult SetAudioMS12Profile(const int32_t handle, const std::string& profile);

        // Audio Mixer Levels
        Core::hresult SetAudioMixerLevels(const int32_t handle, const AudioInput audioInput, const int32_t volume);

        // MS12 Settings Override
        Core::hresult SetAudioMS12SettingsOverride(const int32_t handle, const std::string& profileName, const std::string& profileSettingsName, const std::string& profileSettingValue, const AudioMS12ProfileState profileState);

        // Reset Functions
        Core::hresult ResetAudioDialogEnhancement(const int32_t handle);
        Core::hresult ResetAudioBassEnhancer(const int32_t handle);
        Core::hresult ResetAudioSurroundVirtualizer(const int32_t handle);
        Core::hresult ResetAudioVolumeLeveller(const int32_t handle);

        // HDMI ARC
        Core::hresult GetAudioHDMIARCPortId(const int32_t handle, int32_t &portId);

        // Notification registration/unregistration
        Core::hresult Register(const string& clientName, DeviceSettingsAudio::INotification* notification);
        Core::hresult Unregister(DeviceSettingsAudio::INotification* notification);

        // Audio::INotification interface implementation - hardware callbacks
        void OnAssociatedAudioMixingChanged(bool mixing) override;
        void OnAudioFaderControlChanged(int32_t mixerBalance) override;
        void OnAudioPrimaryLanguageChanged(const std::string& primaryLanguage) override;
        void OnAudioSecondaryLanguageChanged(const std::string& secondaryLanguage) override;
        void OnAudioOutHotPlug(AudioPortType portType, uint32_t uiPortNumber, bool isPortConnected) override;
        void OnAudioFormatUpdate(AudioFormat audioFormat) override;
        void OnDolbyAtmosCapabilitiesChanged(DolbyAtmosCapability atmosCapability, bool status) override;
        void OnAudioPortStateChanged(AudioPortState audioPortState) override;
        void OnAudioLevelChanged(int32_t audioLevel) override;
        void OnAudioModeEvent(AudioPortType audioPortType, AudioStereoMode audioMode) override;

        // Fills IDeviceSettings consolidated config vectors from cached data
        void getCachedConfigs(std::vector<Exchange::IDeviceSettings::AudioTypeConfigInfo>& audioTypes,
                              std::vector<Exchange::IDeviceSettings::AudioPortConfigInfo>& audioPorts) const;

    private:
        template<typename Func, typename... Args>
        void dispatchAudioEvent(Func notifyFunc, Args&&... args);

        template <typename T>
        Core::hresult Register(std::list<std::pair<string, T*>>& list, const string& clientName, T* notification);

        template <typename T>
        Core::hresult Unregister(std::list<std::pair<string, T*>>& list, const T* notification);

        // Declared before _audio: Audio::Create() synchronously fires HAL callbacks
        // (e.g. OnAudioPortStateChanged) during construction, which dispatch through
        // these members, so they must already be constructed when _audio initializes.
        std::list<std::pair<string, DeviceSettingsAudio::INotification*>> _AudioNotifications;
        mutable Core::CriticalSection _configLock;
        mutable Core::CriticalSection _callbackLock;
        std::vector<AudioTypeConfigInfo> _cachedAudioTypeConfigs;
        std::vector<AudioPortConfigInfo> _cachedAudioPortConfigs;

        Audio _audio;

    public:
        /** Called from DeviceSettingsImp::Configure() to trigger deferred HAL init. */
        void InitialiseHAL() { _audio.InitialiseHAL(); }
    };
} // namespace Plugin
} // namespace WPEFramework