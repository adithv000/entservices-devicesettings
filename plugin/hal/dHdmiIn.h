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

#include "dsHdmiIn.h"
#include "dsError.h"
#include "dsHdmiInTypes.h"
#include "dsUtl.h"
#include "dsTypes.h"

#include "rfcapi.h"

#include <core/Portability.h>
#include <WPEFramework/interfaces/IDeviceSettingsHDMIIn.h>
#include "DeviceSettingsTypes.h"

#include <cstdint>

namespace hal {
namespace dHdmiIn {

    class IPlatform {

    protected:
        static void updateVRRBitValuesInPersistence(dsHdmiInPort_t iHdmiPort, bool vrrSupport)
        {
            DSLOG_INFO("Updating values of vrr bit in persistence");
            switch(iHdmiPort){
                case dsHDMI_IN_PORT_0:
                    device::HostPersistence::getInstance().persistHostProperty("HDMI0.vrrEnable", vrrSupport ? "TRUE" : "FALSE");
                    DSLOG_INFO("Port %s: Persist EDID VRR Bit: %d", "HDMI0", vrrSupport);
                    break;
                case dsHDMI_IN_PORT_1:
                    device::HostPersistence::getInstance().persistHostProperty("HDMI1.vrrEnable", vrrSupport ? "TRUE" : "FALSE");
                    DSLOG_INFO("Port %s: Persist EDID VRR Bit: %d", "HDMI1", vrrSupport);
                    break;
                case dsHDMI_IN_PORT_2:
                    device::HostPersistence::getInstance().persistHostProperty("HDMI2.vrrEnable", vrrSupport ? "TRUE" : "FALSE");
                    DSLOG_INFO("Port %s: Persist EDID VRR Bit: %d", "HDMI2", vrrSupport);
                    break;
                case dsHDMI_IN_PORT_3:
                    device::HostPersistence::getInstance().persistHostProperty("HDMI3.vrrEnable", vrrSupport ? "TRUE" : "FALSE");
                    DSLOG_INFO("Port %s: Persist EDID VRR Bit: %d", "HDMI3", vrrSupport);
                    break;
                default:
                    DSLOG_WARN("Invalid HDMI port %d for VRR persistence update", iHdmiPort);
                    break;
            }
        }

    public:
        virtual ~IPlatform();
        virtual void InitialiseHAL() = 0;
        virtual void DeInitialiseHAL() = 0;
        virtual void setAllCallbacks(const CallbackBundle bundle) = 0;
        virtual void getPersistenceValue() = 0;
        //virtual void deinit();

        static void DS_OnHDMIInHotPlugEvent(const dsHdmiInPort_t port, const bool isConnected);
        static void DS_OnHDMIInSignalStatusEvent(const dsHdmiInPort_t port, const dsHdmiInSignalStatus_t signalStatus);
        static void DS_OnHDMIInStatusEvent(const dsHdmiInStatus_t status);
        static void DS_OnHDMIInVideoModeUpdateEvent(const dsHdmiInPort_t port, const dsVideoPortResolution_t videoPortResolution);
        static void DS_OnHDMIInAllmStatusEvent(const dsHdmiInPort_t port, const bool allmStatus);
        static void DS_OnHDMIInAVIContentTypeEvent(const dsHdmiInPort_t port, const dsAviContentType_t aviContentType);
        static void DS_OnHDMIInAVLatencyEvent(const int32_t audioDelay, const int32_t videoDelay);
        static void DS_OnHDMIInVRRStatusEvent(const dsHdmiInPort_t port, const dsVRRType_t vrrType);

        virtual uint32_t GetHDMIInNumberOfInputs(int32_t &count) = 0;
        virtual uint32_t GetHDMIInStatus(HDMIInStatus &hdmiStatus, IHDMIInPortConnectionStatusIterator*& portConnectionStatus) = 0;
        virtual uint32_t SelectHDMIInPort(const HDMIInPort port, const bool requestAudioMix, const bool topMostPlane, const HDMIVideoPlaneType videoPlaneType) = 0;
        virtual uint32_t ScaleHDMIInVideo(const HDMIInVideoRectangle videoPosition) = 0;
        virtual uint32_t SelectHDMIZoomMode(const HDMIInVideoZoom zoomMode) = 0;
        virtual uint32_t GetSupportedGameFeaturesList(IHDMIInGameFeatureListIterator *& gameFeatureList) = 0;
        virtual uint32_t GetHDMIInAVLatency(uint32_t &videoLatency, uint32_t &audioLatency) = 0;
        virtual uint32_t GetHDMIInAllmStatus(const HDMIInPort port, bool &allmStatus) = 0;
        virtual uint32_t GetHDMIInEdid2AllmSupport(const HDMIInPort port, bool &allmSupport) = 0;
        virtual uint32_t SetHDMIInEdid2AllmSupport(const HDMIInPort port, bool allmSupport) = 0;
        virtual uint32_t GetEdidBytes(const HDMIInPort port, const uint16_t edidBytesLength, uint8_t edidBytes[]) = 0;
        virtual uint32_t GetHDMISPDInformation(const HDMIInPort port, const uint16_t spdBytesLength, uint8_t spdBytes[]) = 0;
        virtual uint32_t GetHDMIEdidVersion(const HDMIInPort port, HDMIInEdidVersion &edidVersion) = 0;
        virtual uint32_t SetHDMIEdidVersion(const HDMIInPort port, const HDMIInEdidVersion edidVersion) = 0;
        virtual uint32_t GetHDMIVideoMode(HDMIVideoPortResolution &videoPortResolution) = 0;
        virtual uint32_t GetHDMIVersion(const HDMIInPort port, HDMIInCapabilityVersion &capabilityVersion) = 0;
        virtual uint32_t SetVRRSupport(const HDMIInPort port, const bool vrrSupport) = 0;
        virtual uint32_t GetVRRSupport(const HDMIInPort port, bool &vrrSupport) = 0;
        virtual uint32_t GetVRRStatus(const HDMIInPort port, HDMIInVRRStatus &vrrStatus) = 0;
    };
} // namespace power
} // namespace hal

