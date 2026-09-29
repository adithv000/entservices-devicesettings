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
#include <type_traits>

#include <core/Portability.h>
#include <core/Proxy.h>
#include <core/Trace.h>

#include <com/com.h>
#include <core/core.h>
#include <plugins/plugins.h>

#include <interfaces/IDeviceSettingsHDMIIn.h>
#include "DeviceSettingsTypes.h"
#include "hal/dHdmiInImpl.h"
#include "hal/dHdmiInAIDLImpl.h"

class HdmiIn {
    using IPlatform = hal::dHdmiIn::IPlatform;

    std::shared_ptr<IPlatform> _platform;
public:
    class INotification {

        public:
            virtual ~INotification() = default;

            virtual void OnHDMIInEventHotPlugNotification(const HDMIInPort port, const bool isConnected) = 0;
            virtual void OnHDMIInEventSignalStatusNotification(const HDMIInPort port, const HDMIInSignalStatus signalStatus) = 0;
            virtual void OnHDMIInEventStatusNotification(const HDMIInPort activePort, const bool isPresented) = 0;
            virtual void OnHDMIInVideoModeUpdateNotification(const HDMIInPort port, const HDMIVideoPortResolution videoPortResolution) = 0;
            virtual void OnHDMIInAllmStatusNotification(const HDMIInPort port, const bool allmStatus) = 0;
            virtual void OnHDMIInAVIContentTypeNotification(const HDMIInPort port, const HDMIInAviContentType aviContentType) = 0;
            virtual void OnHDMIInAVLatencyNotification(const int32_t audioDelay, const int32_t videoDelay) = 0;
            virtual void OnHDMIInVRRStatusNotification(const HDMIInPort port, const HDMIInVRRType vrrType) = 0;
    };

    void Platform_init();
    /** Deferred HAL init — called from DeviceSettingsImp::Configure() */
    void InitialiseHAL() {
        if (_platform) {
            _platform->InitialiseHAL();
        }
    }

    uint32_t GetHDMIInNumberOfInputs(int32_t &count);
    uint32_t GetHDMIInStatus(HDMIInStatus &hdmiStatus, IHDMIInPortConnectionStatusIterator*& portConnectionStatus);
    uint32_t SelectHDMIInPort(const HDMIInPort port, const bool requestAudioMix, const bool topMostPlane, const HDMIVideoPlaneType videoPlaneType);
    uint32_t ScaleHDMIInVideo(const HDMIInVideoRectangle videoPosition);
    uint32_t SelectHDMIZoomMode(const HDMIInVideoZoom zoomMode);
    uint32_t GetSupportedGameFeaturesList(IHDMIInGameFeatureListIterator *& gameFeatureList);
    uint32_t GetHDMIInAVLatency(uint32_t &videoLatency, uint32_t &audioLatency);
    uint32_t GetHDMIInAllmStatus(const HDMIInPort port, bool &allmStatus);
    uint32_t GetHDMIInEdid2AllmSupport(const HDMIInPort port, bool &allmSupport);
    uint32_t SetHDMIInEdid2AllmSupport(const HDMIInPort port, bool allmSupport);
    uint32_t GetEdidBytes(const HDMIInPort port, const uint16_t edidBytesLength, uint8_t edidBytes[]);
    uint32_t GetHDMISPDInformation(const HDMIInPort port, const uint16_t spdBytesLength, uint8_t spdBytes[]);
    uint32_t GetHDMIEdidVersion(const HDMIInPort port, HDMIInEdidVersion &edidVersion);
    uint32_t SetHDMIEdidVersion(const HDMIInPort port, const HDMIInEdidVersion edidVersion);
    uint32_t GetHDMIVideoMode(HDMIVideoPortResolution &videoPortResolution);
    uint32_t GetHDMIVersion(const HDMIInPort port, HDMIInCapabilityVersion &capabilityVersion);
    uint32_t SetVRRSupport(const HDMIInPort port, const bool vrrSupport);
    uint32_t GetVRRSupport(const HDMIInPort port, bool &vrrSupport);
    uint32_t GetVRRStatus(const HDMIInPort port, HDMIInVRRStatus &vrrStatus);

private:
    HdmiIn (INotification& parent, std::shared_ptr<IPlatform> platform);

    inline IPlatform& platform() const
    {
        return *_platform;
    }

    INotification& _parent;

public:
    /**
     * @brief Factory method with automatic HAL implementation selection
     *
     * This method intelligently selects between AIDL HAL and RDKV HAL implementations:
     * - If AIDL HAL services are available on the platform, uses dHdmiInAIDLImpl
     * - Otherwise, falls back to dHdmiInImpl (legacy RDKV HAL)
     *
     * This provides transparent support for both HAL implementations without
     * requiring caller code changes.
     *
     * @param parent Reference to INotification handler for events
     * @return HdmiIn instance with appropriate HAL backend
     */
    static HdmiIn Create(INotification& parent)
    {
        ENTRY_LOG;

        LOGINFO("HdmiIn::Create - Detecting available HAL implementation");

        std::shared_ptr<IPlatform> impl;

        // Try to use AIDL implementation if available
        if (dHdmiInAIDLImpl::IsHdmiInAIDLServiceAvailable()) {
            LOGINFO("HdmiIn::Create - AIDL HAL is available, using dHdmiInAIDLImpl");
            impl = std::make_shared<dHdmiInAIDLImpl>();
        } else {
            // Fall back to RDKV implementation
            LOGINFO("HdmiIn::Create - AIDL HAL not available, using legacy dHdmiInImpl (RDKV)");
            impl = std::make_shared<dHdmiInImpl>();
        }

        ASSERT(impl != nullptr);
        EXIT_LOG;
        return HdmiIn(parent, std::move(impl));
    }

    void OnHDMIInHotPlugEvent(const HDMIInPort port, const bool isConnected);
    void OnHDMIInSignalStatusEvent(const HDMIInPort port, const HDMIInSignalStatus signalStatus);
    void OnHDMIInStatusEvent(const HDMIInPort activePort, const bool isPresented);
    void OnHDMIInVideoModeUpdateEvent(const HDMIInPort port, const HDMIVideoPortResolution videoPortResolution);
    void OnHDMIInAllmStatusEvent(const HDMIInPort port, const bool allmStatus);
    void OnHDMIInAVIContentTypeEvent(const HDMIInPort port, const HDMIInAviContentType aviContentType);
    void OnHDMIInAVLatencyEvent(const int32_t audioDelay, const int32_t videoDelay);
    void OnHDMIInVRRStatusEvent(const HDMIInPort port, const HDMIInVRRType vrrType);
    ~HdmiIn() {};

};
