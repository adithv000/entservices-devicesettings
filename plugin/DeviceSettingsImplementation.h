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

#include <com/com.h>
#include <core/core.h>
#include <plugins/plugins.h>

#include <interfaces/IDeviceSettings.h>
#include <interfaces/IDeviceSettingsAudio.h>
#include <interfaces/IDeviceSettingsCompositeIn.h>
#include <interfaces/IDeviceSettingsDisplay.h>
#include <interfaces/IDeviceSettingsFPD.h>
#include <interfaces/IDeviceSettingsHDMIIn.h>
#include <interfaces/IDeviceSettingsHost.h>
#include <interfaces/IDeviceSettingsVideoDevice.h>
#include <interfaces/IDeviceSettingsVideoPort.h>

#include "DeviceSettingsTypes.h"
#include "DeviceSettingsVideoPortImplementation.h"
#include "DeviceSettingsVideoDeviceImplementation.h"
#include "DeviceSettingsHostImplementation.h"
#include "DeviceSettingsDisplayImplementation.h"
#include "DeviceSettingsCompositeInImplementation.h"

namespace WPEFramework {
namespace Plugin {
    // Forward declare implementation classes
    class DeviceSettingsFPDImpl;
    class DeviceSettingsHdmiInImp;
    class DeviceSettingsAudioImpl;
    class DSController;

    class DeviceSettingsImp : public Exchange::IDeviceSettings
                             , public Exchange::IDeviceSettingsFPD
                             , public Exchange::IDeviceSettingsHDMIIn
                             , public Exchange::IDeviceSettingsAudio
                             , public Exchange::IDeviceSettingsVideoPort
                             , public Exchange::IDeviceSettingsVideoDevice
                             , public Exchange::IDeviceSettingsHost
                             , public Exchange::IDeviceSettingsCompositeIn   // ✅ IMPLEMENTED
                             , public Exchange::IDeviceSettingsDisplay       // ✅ IMPLEMENTED
    {
    public:
        // We do not allow this plugin to be copied !!
        DeviceSettingsImp();
        ~DeviceSettingsImp();

        static DeviceSettingsImp* instance(DeviceSettingsImp* DeviceSettingsImpl = nullptr);

        // We do not allow this plugin to be copied !!
        DeviceSettingsImp(const DeviceSettingsImp&)            = delete;
        DeviceSettingsImp& operator=(const DeviceSettingsImp&) = delete;

        // Build QueryInterface implementation, specifying all possible interfaces to be returned.
        BEGIN_INTERFACE_MAP(DeviceSettingsImp)
            INTERFACE_ENTRY(Exchange::IDeviceSettings)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsFPD)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsHDMIIn)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsAudio)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsVideoPort)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsVideoDevice)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsHost)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsCompositeIn)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsDisplay)
        END_INTERFACE_MAP

        // IDeviceSettings interface implementation
        Core::hresult Configure(PluginHost::IShell* service) override;
        Core::hresult GetDeviceSettingConfigs(Exchange::IDeviceSettings::DeviceSettingConfigs& configs) override;

        // IDeviceSettingsFPD interface implementation - delegate to _fpdSettings interface
        Core::hresult Register(const string clientName, Exchange::IDeviceSettingsFPD::INotification* notification) override;
        Core::hresult Unregister(Exchange::IDeviceSettingsFPD::INotification* notification) override;
        Core::hresult SetFPDTime(const FPDTimeFormat timeFormat, const uint32_t minutes, const uint32_t seconds) override;
        Core::hresult SetFPDScroll(const uint32_t scrollHoldDuration, const uint32_t nHorizontalScrollIterations, const uint32_t nVerticalScrollIterations) override;
        Core::hresult SetFPDBlink(const FPDIndicator indicator, const uint32_t blinkDuration, const uint32_t blinkIterations) override;
        Core::hresult SetFPDBrightness(const FPDIndicator indicator, const uint32_t brightNess, const bool persist) override;
        Core::hresult GetFPDBrightness(const FPDIndicator indicator, uint32_t &brightNess, const bool persist) override;
        Core::hresult SetFPDState(const FPDIndicator indicator, const FPDState state) override;
        Core::hresult GetFPDState(const FPDIndicator indicator, FPDState &state) override;
        Core::hresult GetFPDColor(const FPDIndicator indicator, uint32_t &color) override;
        Core::hresult SetFPDColor(const FPDIndicator indicator, const uint32_t color) override;
        Core::hresult SetFPDTextBrightness(const FPDTextDisplay textDisplay, const uint32_t brightNess) override;
        Core::hresult GetFPDTextBrightness(const FPDTextDisplay textDisplay, uint32_t &brightNess) override;
        Core::hresult EnableFPDClockDisplay(const bool enable) override;
        Core::hresult GetFPDTimeFormat(FPDTimeFormat &fpdTimeFormat) override;
        Core::hresult SetFPDTimeFormat(const FPDTimeFormat fpdTimeFormat) override;
        Core::hresult SetFPDMode(const FPDMode fpdMode) override;
        
        // IDeviceSettingsHDMIIn interface implementation - delegate to _hdmiInSettings interface
        Core::hresult Register(const string clientName, Exchange::IDeviceSettingsHDMIIn::INotification* notification) override;
        Core::hresult Unregister(Exchange::IDeviceSettingsHDMIIn::INotification* notification) override;
            Core::hresult GetHDMIInNumberOfInputs(int32_t &count) override;
        Core::hresult GetHDMIInStatus(HDMIInStatus &hdmiStatus, IHDMIInPortConnectionStatusIterator*& portConnectionStatus) override;
        Core::hresult SelectHDMIInPort(const HDMIInPort port, const bool requestAudioMix, const bool topMostPlane, const HDMIVideoPlaneType videoPlaneType) override;
        Core::hresult ScaleHDMIInVideo(const HDMIInVideoRectangle videoPosition) override;
        Core::hresult SelectHDMIZoomMode(const HDMIInVideoZoom zoomMode) override;
        Core::hresult GetSupportedGameFeaturesList(IHDMIInGameFeatureListIterator *& gameFeatureList) override;
        Core::hresult GetHDMIInAVLatency(uint32_t &videoLatency, uint32_t &audioLatency) override;
        Core::hresult GetHDMIInAllmStatus(const HDMIInPort port, bool &allmStatus) override;
        Core::hresult GetHDMIInEdid2AllmSupport(const HDMIInPort port, bool &allmSupport) override;
        Core::hresult SetHDMIInEdid2AllmSupport(const HDMIInPort port, bool allmSupport) override;
        Core::hresult GetEdidBytes(const HDMIInPort port, const uint16_t edidBytesLength, uint8_t edidBytes[]) override;
        Core::hresult GetHDMISPDInformation(const HDMIInPort port, const uint16_t spdBytesLength, uint8_t spdBytes[]) override;
        Core::hresult GetHDMIEdidVersion(const HDMIInPort port, HDMIInEdidVersion &edidVersion) override;
        Core::hresult SetHDMIEdidVersion(const HDMIInPort port, const HDMIInEdidVersion edidVersion) override;
        Core::hresult GetHDMIVideoMode(HDMIVideoPortResolution &videoPortResolution) override;
        Core::hresult GetHDMIVersion(const HDMIInPort port, HDMIInCapabilityVersion &capabilityVersion) override;
        Core::hresult SetVRRSupport(const HDMIInPort port, const bool vrrSupport) override;
        Core::hresult GetVRRSupport(const HDMIInPort port, bool &vrrSupport) override;
        Core::hresult GetVRRStatus(const HDMIInPort port, HDMIInVRRStatus &vrrStatus) override;
        
        // IDeviceSettingsAudio interface implementation - delegate to _audioSettings interface
        Core::hresult Register(const string clientName, Exchange::IDeviceSettingsAudio::INotification* notification) override;
        Core::hresult Unregister(Exchange::IDeviceSettingsAudio::INotification* notification) override;
        Core::hresult GetAudioPort(const AudioPortType type, const int32_t index, int32_t &handle) override;
        Core::hresult GetMS12Capabilities(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions);
        Core::hresult GetAudioCapabilities(const int32_t handle, int32_t &capabilities);
        Core::hresult GetAudioMS12Capabilities(const int32_t handle, int32_t &capabilities);
        Core::hresult GetAudioFormat(const int32_t handle, AudioFormat &audioFormat) override;
        Core::hresult GetAudioEncoding(const int32_t handle, AudioEncoding &encoding) override;
        Core::hresult GetSupportedCompressions(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions);
        Core::hresult GetAudioCompression(const int32_t handle, AudioCompression &compression);
        Core::hresult SetAudioCompression(const int32_t handle, const AudioCompression compression);
        Core::hresult SetAudioLevel(const int32_t handle, const float audioLevel) override;
        Core::hresult GetAudioLevel(const int32_t handle, float &audioLevel) override;
        Core::hresult SetAudioGain(const int32_t handle, const float gainLevel) override;
        Core::hresult GetAudioGain(const int32_t handle, float &gainLevel) override;
        Core::hresult SetAudioMute(const int32_t handle, const bool mute) override;
        Core::hresult IsAudioMuted(const int32_t handle, bool &muted) override;
        Core::hresult SetAudioDucking(const int32_t handle, const AudioDuckingType duckingType, const AudioDuckingAction duckingAction, const uint8_t level) override;
        Core::hresult GetStereoMode(const int32_t handle, AudioStereoMode &mode, const bool persist) override;
        Core::hresult SetStereoMode(const int32_t handle, const AudioStereoMode mode, const bool persist) override;
        Core::hresult GetStereoAuto(const int32_t handle, int32_t &mode) override;
        Core::hresult SetStereoAuto(const int32_t handle, const int32_t mode, const bool persist) override;
        Core::hresult SetAssociatedAudioMixing(const int32_t handle, const bool mixing);
        Core::hresult GetAssociatedAudioMixing(const int32_t handle, bool &mixing);
        Core::hresult SetAudioFaderControl(const int32_t handle, const int32_t mixerBalance);
        Core::hresult GetAudioFaderControl(const int32_t handle, int32_t &mixerBalance);
        Core::hresult SetAudioPrimaryLanguage(const int32_t handle, const string& primaryAudioLanguage);
        Core::hresult GetAudioPrimaryLanguage(const int32_t handle, string &primaryAudioLanguage);
        Core::hresult SetAudioSecondaryLanguage(const int32_t handle, const string& secondaryAudioLanguage);
        Core::hresult GetAudioSecondaryLanguage(const int32_t handle, string &secondaryAudioLanguage);
        Core::hresult IsAudioOutputConnected(const int32_t handle, bool &isConnected);
        Core::hresult GetAudioSinkDeviceAtmosCapability(const int32_t handle, DolbyAtmosCapability &atmosCapability);
        Core::hresult SetAudioAtmosOutputMode(const int32_t handle, const bool enable);

        // Additional Audio Port Methods
        Core::hresult IsAudioPortEnabled(const int32_t handle, bool &enabled) override;
        Core::hresult EnableAudioPort(const int32_t handle, const bool enable) override;
        Core::hresult GetSupportedARCTypes(const int32_t handle, int32_t &types) override;
        Core::hresult SetSAD(const int32_t handle, const uint8_t sadList[], const uint8_t count) override;
        Core::hresult EnableARC(const int32_t handle, const AudioARCStatus arcStatus) override;

        // Audio Persistence Configuration
        Core::hresult GetAudioEnablePersist(const int32_t handle, bool &enabled, string &portName) override;
            Core::hresult SetAudioEnablePersist(const int32_t handle, const bool enable, const string& portName) override;

        // Audio Decoder Status
        Core::hresult IsAudioMSDecoded(const int32_t handle, bool &hasms11Decode) override;
        Core::hresult IsAudioMS12Decoded(const int32_t handle, bool &hasms12Decode) override;

        // Loudness Equivalence Configuration
        Core::hresult GetAudioLEConfig(const int32_t handle, bool &enabled) override;
        Core::hresult EnableAudioLEConfig(const int32_t handle, const bool enable) override;

        // Application Audio Configuration
        Core::hresult SetApplicationAudioConfig(const int32_t handle, const string& audioConfig, const bool enable) override;
        Core::hresult GetApplicationAudioConfig(const int32_t handle, const string& audioConfig, bool& enabled) override;
        Core::hresult GetApplicationAudioConfigList(const int32_t handle, IDeviceSettingsAudioApplicationConfigIterator*& configList) const override;

        // Audio Delay Controls
        Core::hresult SetAudioDelay(const int32_t handle, const uint32_t audioDelay) override;
        Core::hresult GetAudioDelay(const int32_t handle, uint32_t &audioDelay) override;
        Core::hresult SetAudioDelayOffset(const int32_t handle, const uint32_t delayOffset) override;
        Core::hresult GetAudioDelayOffset(const int32_t handle, uint32_t &delayOffset) override;

        // Audio Dynamic Range Control
        Core::hresult SetAudioCompression(const int32_t handle, const int32_t compressionLevel) override;
        Core::hresult GetAudioCompression(const int32_t handle, int32_t &compressionLevel) override;

        // Dialog Enhancement
        Core::hresult SetAudioDialogEnhancement(const int32_t handle, const int32_t level) override;
        Core::hresult GetAudioDialogEnhancement(const int32_t handle, int32_t &level) override;

        // Dolby Volume Mode
        Core::hresult SetAudioDolbyVolumeMode(const int32_t handle, const bool enable) override;
        Core::hresult GetAudioDolbyVolumeMode(const int32_t handle, bool &enabled) override;

        // Intelligent Equalizer
        Core::hresult SetAudioIntelligentEqualizerMode(const int32_t handle, const int32_t mode) override;
        Core::hresult GetAudioIntelligentEqualizerMode(const int32_t handle, int32_t &mode) override;

        // Volume Leveller
        Core::hresult SetAudioVolumeLeveller(const int32_t handle, const VolumeLeveller volumeLeveller) override;
        Core::hresult GetAudioVolumeLeveller(const int32_t handle, VolumeLeveller &volumeLeveller) override;

        // Bass Enhancer
        Core::hresult SetAudioBassEnhancer(const int32_t handle, const int32_t boost) override;
        Core::hresult GetAudioBassEnhancer(const int32_t handle, int32_t &boost) override;

        // Surround Decoder
            Core::hresult EnableAudioSurroundDecoder(const int32_t handle, const bool enable) override;
            Core::hresult IsAudioSurroundDecoderEnabled(const int32_t handle, bool &enabled) override;

        // DRC Mode
        Core::hresult SetAudioDRCMode(const int32_t handle, const int32_t drcMode) override;
        Core::hresult GetAudioDRCMode(const int32_t handle, int32_t &drcMode) override;

        // Surround Virtualizer
        Core::hresult SetAudioSurroundVirtualizer(const int32_t handle, const SurroundVirtualizer surroundVirtualizer) override;
        Core::hresult GetAudioSurroundVirtualizer(const int32_t handle, SurroundVirtualizer &surroundVirtualizer) override;

        // MI Steering
        Core::hresult SetAudioMISteering(const int32_t handle, const bool enable) override;
        Core::hresult GetAudioMISteering(const int32_t handle, bool &enable) override;

        // Graphic Equalizer
        Core::hresult SetAudioGraphicEqualizerMode(const int32_t handle, const int32_t mode) override;
        Core::hresult GetAudioGraphicEqualizerMode(const int32_t handle, int32_t &mode) override;

        // MS12 Profile Management
        Core::hresult GetAudioMS12ProfileList(const int32_t handle, IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) const override;
        Core::hresult GetAudioMS12Profile(const int32_t handle, string &profile) override;
        Core::hresult SetAudioMS12Profile(const int32_t handle, const string& profile) override;

        // Audio Mixer Levels
        Core::hresult SetAudioMixerLevels(const int32_t handle, const AudioInput audioInput, const int32_t volume) override;

        // MS12 Settings Override
            Core::hresult SetAudioMS12SettingsOverride(const int32_t handle, const string& profileName, const string& profileSettingsName, const string& profileSettingValue, const AudioMS12ProfileState profileState) override;

        // Reset Functions
        Core::hresult ResetAudioDialogEnhancement(const int32_t handle) override;
        Core::hresult ResetAudioBassEnhancer(const int32_t handle) override;
        Core::hresult ResetAudioSurroundVirtualizer(const int32_t handle) override;
        Core::hresult ResetAudioVolumeLeveller(const int32_t handle) override;

        // HDMI ARC
        Core::hresult GetAudioHDMIARCPortId(const int32_t handle, int32_t &portId) override;
        
        // IDeviceSettingsVideoPort interface implementation - delegate to _videoPortSettings interface
        Core::hresult Register(const string clientName, Exchange::IDeviceSettingsVideoPort::INotification* notification) override;
        Core::hresult Unregister(Exchange::IDeviceSettingsVideoPort::INotification* notification) override;
        Core::hresult GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t &handle) override;
        Core::hresult IsVideoPortEnabled(const int32_t handle, bool &enabled) override;

        Core::hresult GetVideoPortResolutionConfig(VideoPortType videoPortType,
                         IVideoPortResolutionIterator*& videoPortResolutions) const override;
        Core::hresult EnableVideoPort(const int32_t handle, const bool enabled) override;
        Core::hresult IsVideoPortDisplayConnected(const int32_t handle, bool &connected) override;
        Core::hresult IsVideoPortActive(const int32_t handle, bool &active) override;
        Core::hresult GetVideoPortResolution(const int32_t handle, VideoPortResolution &resolution) override;
        Core::hresult getIgnoreEDIDStatus(const int32_t handle, bool &ignoreEDID);
        Core::hresult GetColorDepth(const int32_t handle, uint32_t &colorDepth) override;
        Core::hresult GetColorSpace(const int32_t handle, VideoPortColorSpace &colorSpace) override;
        Core::hresult GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange &quantizationRange) override;
        Core::hresult GetHDCPStatusOnVideoPort(const int32_t handle, Exchange::IDeviceSettingsVideoPort::HDCPStatus &hdcpStatus) override;
        Core::hresult GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) override;
        Core::hresult GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) override;
        Core::hresult GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) override;
        
        // Additional required VideoPort methods from WPE interface
        Core::hresult IsVideoPortDisplaySurround(const int32_t handle, bool &surround) override;
        Core::hresult GetVideoPortDisplaySurroundMode(const int32_t handle, Exchange::IDeviceSettingsVideoPort::VideoPortSurroundMode &surroundMode) override;
            Core::hresult SetVideoPortResolution(const int32_t handle, const VideoPortResolution& videoPortResolution, const bool persist, const bool forceCompatibility) override;
        Core::hresult EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t hdcpKey[], const uint16_t hdcpKeySize) override;
        Core::hresult IsHDCPEnabledOnVideoPort(const int32_t handle, bool &hdcpEnabled) override;
        Core::hresult GetTVHDRCapabilities(const int32_t handle, int32_t &capabilities) override;
        Core::hresult GetTVSupportedResolutions(const int32_t handle, int32_t &resolutions) override;
        Core::hresult SetForceDisable4K(const int32_t handle, const bool disable) override;
        Core::hresult GetForceDisable4K(const int32_t handle, bool &disabled) override;
        Core::hresult IsVideoPortOutputHDR(const int32_t handle, bool &isHDR) override;
        Core::hresult ResetVideoPortOutputToSDR() override;
        Core::hresult GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion &hdcpVersion) override;
        Core::hresult SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion) override;
        Core::hresult GetVideoEOTF(const int32_t handle, Exchange::IDeviceSettingsVideoPort::HDRStandard &hdrStandard) override;
        Core::hresult GetMatrixCoefficients(const int32_t handle, Exchange::IDeviceSettingsVideoPort::DisplayMatrixCoefficients &matrixCoefficients) override;
        Core::hresult GetCurrentOutputSettings(const int32_t handle, Exchange::IDeviceSettingsVideoPort::DSOutputSettings &outputSettings) override;
        Core::hresult SetBackgroundColor(const int32_t handle, const Exchange::IDeviceSettingsVideoPort::VideoBackgroundColor backgroundColor) override;
        Core::hresult SetForceHDRMode(const int32_t handle, const Exchange::IDeviceSettingsVideoPort::HDRStandard hdrMode) override;
        Core::hresult GetColorDepthCapabilities(const int32_t handle, uint32_t &colorDepthCapabilities) override;
        Core::hresult GetPreferredColorDepth(const int32_t handle, Exchange::IDeviceSettingsVideoPort::DisplayColorDepth &colorDepth, const bool persist) override;
        Core::hresult SetPreferredColorDepth(const int32_t handle, const Exchange::IDeviceSettingsVideoPort::DisplayColorDepth colorDepth, const bool persist) override;
        // Core::hresult IsContentProtected(const int32_t handle, bool &isContentProtected) override; // Method not in WPE interface
        
        //=========================================================================
        // IDeviceSettingsVideoDevice interface methods
        //=========================================================================
        Core::hresult Register(const string clientName, Exchange::IDeviceSettingsVideoDevice::INotification* notification ) override;
        Core::hresult Unregister(Exchange::IDeviceSettingsVideoDevice::INotification* notification ) override;
        
        Core::hresult GetVideoDeviceHandle(const int32_t index, int32_t &handle /* @out */) override;
        Core::hresult SetVideoDeviceDFC(const int32_t handle , const Exchange::IDeviceSettingsVideoDevice::VideoZoom zoomSetting ) override;
        Core::hresult GetVideoDeviceDFC(const int32_t handle , Exchange::IDeviceSettingsVideoDevice::VideoZoom &zoomSetting /* @out */) override;
        Core::hresult GetHDRCapabilities(const int32_t handle , int32_t &capabilities /* @out */) override;
        Core::hresult GetSupportedVideoCodingFormats(const int32_t handle , int32_t &supportedFormats /* @out */) override;
        Core::hresult GetCodecInfo(const int32_t handle , const Exchange::IDeviceSettingsVideoDevice::VideoCodec videoCodec , Exchange::IDeviceSettingsVideoDevice::IDeviceSettingsVideoCodecProfileSupportIterator *&codecInfo /* @out */) override;
        Core::hresult DisableHDR(const int32_t handle , const bool disable ) override;
        Core::hresult SetFRFMode(const int32_t handle , const int32_t frfmode ) override;
        Core::hresult GetFRFMode(const int32_t handle , int32_t &frfmode /* @out */) override;
        Core::hresult GetCurrentDisplayFrameRate(const int32_t handle , string &framerate /* @out */) override;
        Core::hresult SetDisplayFrameRate(const int32_t handle , const string& framerate ) override;
        
        //=========================================================================
        // IDeviceSettingsHost interface methods
        //=========================================================================
        Core::hresult GetEDID(uint8_t edId[] /* @out @length:edIdLength @maxlength:edIdLength */, const uint16_t edIdLength ) override;
        Core::hresult GetMS12ConfigType(string &ms12Config /* @out */) override;
        
        //=========================================================================
        // IDeviceSettingsDisplay interface methods
        //=========================================================================
        Core::hresult Register(const string clientName, IDisplayNotification* notification ) override;
        Core::hresult Unregister(IDisplayNotification* notification ) override;
        Core::hresult Register(const string clientName, IDisplayHDMIHotPlugNotification* notification ) override;
        Core::hresult Unregister(IDisplayHDMIHotPlugNotification* notification ) override;

        Core::hresult GetDisplayEdid(const int32_t handle, DisplayEDID &edId /* @out */, IDSVideoPortResolutionIterator*& supportedResolutionList /* @out */) override;
        Core::hresult GetDisplayEdidBytes(const int32_t handle, uint8_t edIdBytes[] /* @out @length:edidLength @maxlength:edidLength */, const uint16_t edidLength) override;
        Core::hresult GetDisplay(const DisplayPortType portType, const int32_t index, int32_t &handle /* @out */) override;
        Core::hresult GetDisplayAspectRatio(const int32_t handle, Exchange::IDeviceSettingsDisplay::DisplayVideoAspectRatio &aspectRatio /* @out */) override;
        Core::hresult SetAllmEnabled(const int32_t handle, const bool enabled) override;
        Core::hresult SetAVIContentType(const int32_t handle, const DisplayAVIContentType contentType) override;
        Core::hresult SetAVIScanInformation(const int32_t handle, const DisplayAVIScanInformation scanInfo) override;

        //=========================================================================
        // IDeviceSettingsCompositeIn interface methods
        //=========================================================================
        Core::hresult Register(const string clientName, Exchange::IDeviceSettingsCompositeIn::INotification* notification ) override;
        Core::hresult Unregister(Exchange::IDeviceSettingsCompositeIn::INotification* notification ) override;

        Core::hresult GetNrOfCompositeInputs(int32_t &nrCompositeInputs /* @out */) override;
        Core::hresult GetCompositeInStatus(CompositeInStatus &status /* @out */) override;
        Core::hresult SelectCompositeInPort(const CompositeInPort port ) override;
        Core::hresult ScaleCompositeInVideo(const CompositeInVideoRectangle videoRect ) override;

    private:
        // DSController must be initialized first as it provides system infrastructure
        DSController* _dsController;
        
        // Component implementation instances
        DeviceSettingsFPDImpl* _fpdSettings;
        DeviceSettingsHdmiInImp* _hdmiInSettings;
        DeviceSettingsAudioImpl* _audioSettings;
        DeviceSettingsVideoPortImpl* _videoPortSettings;
        DeviceSettingsVideoDeviceImpl* _videoDeviceSettings;
        DeviceSettingsHostImpl* _hostSettings;
        DeviceSettingsDisplayImpl* _displaySettings;
        DeviceSettingsCompositeInImpl* _compositeInSettings;
        
        // Interface pointers for future implementation (currently unused)
        // Exchange::IDeviceSettingsCompositeIn* _compositeInSettings;
        // Exchange::IDeviceSettingsDisplay* _displaySettings;
        // Exchange::IDeviceSettingsHost* _hostSettings;
        // Exchange::IDeviceSettingsVideoDevice* _videoDeviceSettings;
        
        uint32_t mConnectionId;
        static DeviceSettingsImp* _instance;

        // Cached consolidated config — populated once on first GetDeviceSettingConfigs() call
        Exchange::IDeviceSettings::DeviceSettingConfigs _cachedConfigs;
        std::atomic<bool>  _configLoaded{false};
        mutable std::mutex _configMutex;
    };
} // namespace Plugin
} // namespace WPEFramework
