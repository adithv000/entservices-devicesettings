// Out-of-line virtual destructor definition for RTTI/typeinfo
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
#include <vector>
#include <sstream>
#include <iomanip>
#include <list>
#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <cstring>

#include "dHdmiIn.h"
#include "dsHdmiIn.h"
#include "dsError.h"
#include "dsHdmiInTypes.h"
#include "dsVideoDeviceTypes.h"
#include "dsUtl.h"
#include "dsTypes.h"

#include <WPEFramework/interfaces/IDeviceSettingsHDMIIn.h>
#include "DeviceSettingsTypes.h"

static int m_hdmiInInitialized = 0;
static int m_hdmiInPlatInitialized = 0;
static bool isDalsEnabled = 0;
static dsHdmiInCap_t hdmiInCap_gs;
static bool m_edidallmsupport[dsHDMI_IN_PORT_MAX];
static bool m_vrrsupport[dsHDMI_IN_PORT_MAX];
static bool m_hdmiPortVrrCaps[dsHDMI_IN_PORT_MAX];

static tv_hdmi_edid_version_t m_edidversion[dsHDMI_IN_PORT_MAX];

static std::function<void(DeviceSettingsHDMIIn::HDMIInPort, bool)> g_HdmiInHotPlugCallback;
static std::function<void(DeviceSettingsHDMIIn::HDMIInPort, DeviceSettingsHDMIIn::HDMIInSignalStatus)> g_HdmiInSignalStatusCallback;
static std::function<void(DeviceSettingsHDMIIn::HDMIInPort, DeviceSettingsHDMIIn::HDMIVideoPortResolution)> g_HdmiInVideoModeUpdateCallback;
static std::function<void(DeviceSettingsHDMIIn::HDMIInPort, bool)> g_HdmiInAllmStatusCallback;
static std::function<void(DeviceSettingsHDMIIn::HDMIInPort, DeviceSettingsHDMIIn::HDMIInAviContentType)> g_HdmiInAviContentTypeCallback;
static std::function<void(int32_t, int32_t)> g_HdmiInAVLatencyCallback;
static std::function<void(DeviceSettingsHDMIIn::HDMIInPort, DeviceSettingsHDMIIn::HDMIInVRRType)> g_HdmiInVRRStatusCallback;
static std::function<void(DeviceSettingsHDMIIn::HDMIInPort, bool)> g_HdmiInStatusCallback;

class dHdmiInImpl : public hal::dHdmiIn::IPlatform {

    // delete copy constructor and assignment operator
    dHdmiInImpl(const dHdmiInImpl&) = delete;
    dHdmiInImpl& operator=(const dHdmiInImpl&) = delete;

public:
    dHdmiInImpl()
    {
        DSLOG_INFO("Constructor");
        InitialiseHAL();
    }

    virtual ~dHdmiInImpl()
    {
        DSLOG_ERR("Destructor");
        DeInitialiseHAL();
    }

    void InitialiseHAL()
    {
        getDynamicAutoLatencyConfig();

        profileType = searchRdkProfile();
        DSLOG_INFO("profileType %d", profileType);

        if (TV == profileType)
        {
            if (!m_hdmiInPlatInitialized)
            {
                dsError_t eError = dsHdmiInInit();
                if (eError != dsERR_NONE) {
                    DSLOG_ERR("dsHdmiInInit failed: %d", eError);
                } else {
                    DSLOG_INFO("dsHdmiInInit succeeded: %d", eError);
                }
            }
            m_hdmiInPlatInitialized++;
        }
    }

    void DeInitialiseHAL()
    {
        // profileType is already initialized in DeviceSettingsImplementation.cpp
        DSLOG_INFO("profileType %d", profileType);
        getDynamicAutoLatencyConfig();

        if (TV == profileType)
        {
            if (m_hdmiInPlatInitialized)
            {
                m_hdmiInPlatInitialized--;
                if (!m_hdmiInPlatInitialized)
                {
                    dsHdmiInTerm();
                }
                m_hdmiInPlatInitialized = 0;
            }
        }
    }

    static void* resolve(const std::string& libName, const std::string& symbolName) {
        return WPEFramework::Plugin::DeviceSettingsHALLoader::ResolveSymbol(libName, symbolName);
    }

    bool getHdmiInPortPersistValue(const std::string& propertyName, int portIndex) {
        try {
            // Use HostPersistence from DeviceSettingsTypes.h with default value support
            std::string value = device::HostPersistence::getInstance().getProperty(propertyName, "TRUE");
            bool support = (value == "TRUE");
            DSLOG_INFO("Port property %s: Value: %s, Parsed: %d", propertyName.c_str(), value.c_str(), support);
            return support;
        } catch(...) {
            DSLOG_ERR("Port property %s: Exception in getting property from persistence storage, using default TRUE", propertyName.c_str());
            return true;
        }
    }

    static dsError_t getVRRSupport (dsHdmiInPort_t iHdmiPort, bool *vrrSupport) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsHdmiInGetVRRSupport_t)(dsHdmiInPort_t iHdmiPort, bool *vrrSupport);
        static dsHdmiInGetVRRSupport_t dsHdmiInGetVRRSupportFunc = 0;

        if (dsHdmiInGetVRRSupportFunc == 0) {
            dsHdmiInGetVRRSupportFunc = (dsHdmiInGetVRRSupport_t)resolve(RDK_DSHAL_NAME, "dsHdmiInGetVRRSupport");
            if(dsHdmiInGetVRRSupportFunc == 0) {
                DSLOG_WARN("dsHdmiInGetVRRSupport is not defined");
            }
            else {
                DSLOG_INFO("dsHdmiInGetVRRSupport loaded");
            }
        }
        if (0 != dsHdmiInGetVRRSupportFunc) {
            eRet = dsHdmiInGetVRRSupportFunc (iHdmiPort, vrrSupport);
            DSLOG_INFO("dsHdmiInGetVRRSupportFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsHdmiInGetVRRSupportFunc = %p", dsHdmiInGetVRRSupportFunc);
        }
        return eRet;
    }

    static dsError_t setVRRSupport (dsHdmiInPort_t iHdmiPort, bool vrrSupport) {
        dsError_t eRet = dsERR_GENERAL;
        if (!m_hdmiPortVrrCaps[iHdmiPort]) {
            return dsERR_OPERATION_NOT_SUPPORTED;
        }
        typedef dsError_t (*dsHdmiInSetVRRSupport_t)(dsHdmiInPort_t iHdmiPort, bool vrrSupport);
        static dsHdmiInSetVRRSupport_t dsHdmiInSetVRRSupportFunc = 0;

        if (dsHdmiInSetVRRSupportFunc == 0) {
            dsHdmiInSetVRRSupportFunc = (dsHdmiInSetVRRSupport_t)resolve(RDK_DSHAL_NAME, "dsHdmiInSetVRRSupport");
            if(dsHdmiInSetVRRSupportFunc == 0) {
                DSLOG_ERR("dsHdmiInSetVRRSupport is not defined");
            }
            else {
                DSLOG_INFO("dsHdmiInSetVRRSupport loaded");
            }
        }
        DSLOG_INFO("to ds-hal:  EDID VRR Bit: %d", vrrSupport);
        if (0 != dsHdmiInSetVRRSupportFunc) {
            eRet = dsHdmiInSetVRRSupportFunc (iHdmiPort, vrrSupport);
            DSLOG_INFO("[srv] %s: dsHdmiInSetVRRSupportFunc eRet: %d", __FUNCTION__, eRet);
        }
        else {
            DSLOG_INFO("%s:  dsHdmiInSetVRRSupportFunc = %p\n", __FUNCTION__, dsHdmiInSetVRRSupportFunc);
        }
        DSLOG_INFO("to ds-hal:  EDID VRR Bit: %d\n", vrrSupport);
        if (0 != dsHdmiInSetVRRSupportFunc) {
            eRet = dsHdmiInSetVRRSupportFunc (iHdmiPort, vrrSupport);
            DSLOG_INFO("dsHdmiInSetVRRSupportFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsHdmiInSetVRRSupportFunc = %p", dsHdmiInSetVRRSupportFunc);
        }
        return eRet;
    }

    static dsError_t setEdid2AllmSupport (dsHdmiInPort_t iHdmiPort, bool allmSupport) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsSetEdid2AllmSupport_t)(dsHdmiInPort_t iHdmiPort, bool allmSupport);
        static dsSetEdid2AllmSupport_t dsSetEdid2AllmSupportFunc = 0;

        if (dsSetEdid2AllmSupportFunc == 0) {
            dsSetEdid2AllmSupportFunc = (dsSetEdid2AllmSupport_t)resolve(RDK_DSHAL_NAME, "dsSetEdid2AllmSupport");
            if(dsSetEdid2AllmSupportFunc == 0) {
                DSLOG_ERR("dsSetEdid2AllmSupport is not defined");
            }
            else {
                DSLOG_INFO("dsSetEdid2AllmSupport loaded");
            }
        }
        DSLOG_INFO("to ds-hal:  EDID Allm Bit: %d", allmSupport);
        if (0 != dsSetEdid2AllmSupportFunc) {
            eRet = dsSetEdid2AllmSupportFunc (iHdmiPort, allmSupport);
            DSLOG_INFO("dsSetEdid2AllmSupportFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsSetEdid2AllmSupportFunc = %p", dsSetEdid2AllmSupportFunc);
        }
        return eRet;
    }

    static dsError_t isHdmiARCPort (int iPort, bool* isArcEnabled) {
        dsError_t eRet = dsERR_GENERAL; 

        typedef bool (*dsIsHdmiARCPort_t)(int iPortArg, bool *boolArg);
        static dsIsHdmiARCPort_t dsIsHdmiARCPortFunc = 0;
        if (dsIsHdmiARCPortFunc == 0) {
            dsIsHdmiARCPortFunc = (dsIsHdmiARCPort_t)resolve(RDK_DSHAL_NAME, "dsIsHdmiARCPort");
            if(dsIsHdmiARCPortFunc == 0) {
                DSLOG_ERR("dsIsHdmiARCPort is not defined");
                eRet = dsERR_GENERAL;
            }
            else {
                DSLOG_INFO("dsIsHdmiARCPort loaded");
            }
        }
        if (0 != dsIsHdmiARCPortFunc) { 
            dsIsHdmiARCPortFunc (iPort, isArcEnabled);
            DSLOG_INFO("dsIsHdmiARCPort port %d isArcEnabled:%d", iPort, *isArcEnabled);
        }
        else {
            DSLOG_INFO("dsIsHdmiARCPort  dsIsHdmiARCPortFunc = %p", dsIsHdmiARCPortFunc);
        }
        return eRet;
    }

    static dsError_t setEdidVersion (dsHdmiInPort_t iHdmiPort, tv_hdmi_edid_version_t iEdidVersion) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsSetEdidVersion_t)(dsHdmiInPort_t iHdmiPort, tv_hdmi_edid_version_t iEdidVersion);
        static dsSetEdidVersion_t dsSetEdidVersionFunc = 0;
        char edidVer[2];
        sprintf(edidVer,"%d",iEdidVersion);

        if (dsSetEdidVersionFunc == 0) {
            dsSetEdidVersionFunc = (dsSetEdidVersion_t)resolve(RDK_DSHAL_NAME, "dsSetEdidVersion");
            if(dsSetEdidVersionFunc == 0) {
                DSLOG_ERR("dsSetEdidVersion is not defined");
            }
            else {
                DSLOG_INFO("dsSetEdidVersion loaded");
            }
        }

        if (0 != dsSetEdidVersionFunc) {
            eRet = dsSetEdidVersionFunc (iHdmiPort, iEdidVersion);
            if (eRet == dsERR_NONE) {
                switch (iHdmiPort) {
                    case dsHDMI_IN_PORT_0:
                        device::HostPersistence::getInstance().persistHostProperty("HDMI0.edidversion", edidVer);
                        DSLOG_INFO("Port %s: Persist EDID Version: %d", "HDMI0", iEdidVersion);
                        break;
                    case dsHDMI_IN_PORT_1:
                        device::HostPersistence::getInstance().persistHostProperty("HDMI1.edidversion", edidVer);
                        DSLOG_INFO("Port %s: Persist EDID Version: %d", "HDMI1", iEdidVersion);
                        break;
                    case dsHDMI_IN_PORT_2:
                        device::HostPersistence::getInstance().persistHostProperty("HDMI2.edidversion", edidVer);
                        DSLOG_INFO("Port %s: Persist EDID Version: %d", "HDMI2", iEdidVersion);
                        break;
                    case dsHDMI_IN_PORT_3:
                        device::HostPersistence::getInstance().persistHostProperty("HDMI3.edidversion", edidVer);
                        DSLOG_INFO("Port %s: Persist EDID Version: %d", "HDMI3", iEdidVersion);
                        break;
                    case dsHDMI_IN_PORT_NONE:
                    case dsHDMI_IN_PORT_4:
                    case dsHDMI_IN_PORT_MAX:
                        break;
                }
            // Whenever there is a change in edid version to 2.0, ensure the edid allm support and edid vrr support is updated with latest value
        if(iEdidVersion == HDMI_EDID_VER_20)
            {
            DSLOG_INFO("As the version is changed to 2.0, we are updating the allm bit and the vrr bit in edid");
            setEdid2AllmSupport(iHdmiPort,m_edidallmsupport[iHdmiPort]);
                    setVRRSupport(iHdmiPort,m_vrrsupport[iHdmiPort]);
            }
            }
            DSLOG_INFO("dsSetEdidVersionFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsSetEdidVersionFunc = %p", dsSetEdidVersionFunc);
        }
        return eRet;
    }

    static dsError_t getEdidVersion (dsHdmiInPort_t iHdmiPort, int *iEdidVersion) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsGetEdidVersion_t)(dsHdmiInPort_t iHdmiPort, tv_hdmi_edid_version_t *iEdidVersion);
        static dsGetEdidVersion_t dsGetEdidVersionFunc = 0;
        if (dsGetEdidVersionFunc == 0) {
            dsGetEdidVersionFunc = (dsGetEdidVersion_t)resolve(RDK_DSHAL_NAME, "dsGetEdidVersion");
            if(dsGetEdidVersionFunc == 0) {
                DSLOG_ERR("dsGetEdidVersion is not defined");
            }
            else {
                DSLOG_INFO("dsGetEdidVersion loaded");
            }
        }
        if (0 != dsGetEdidVersionFunc) {
            tv_hdmi_edid_version_t EdidVersion;
            eRet = dsGetEdidVersionFunc (iHdmiPort, &EdidVersion);
            int EdidVer = static_cast<int>(EdidVersion);
            *iEdidVersion = EdidVer;
            DSLOG_INFO("dsGetEdidVersionFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("%s:  dsGetEdidVersionFunc = %p", __FUNCTION__, dsGetEdidVersionFunc);
        }
        return eRet;
    }

    static dsError_t getAllmStatus (dsHdmiInPort_t iHdmiPort, bool *allmStatus) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsGetAllmStatus_t)(dsHdmiInPort_t iHdmiPort, bool *allmStatus);
        static dsGetAllmStatus_t dsGetAllmStatusFunc = 0;
        if (dsGetAllmStatusFunc == 0) {
            dsGetAllmStatusFunc = (dsGetAllmStatus_t)resolve(RDK_DSHAL_NAME, "dsGetAllmStatus");
            if(dsGetAllmStatusFunc == 0) {
                DSLOG_ERR("dsGetAllmStatus is not defined");
            }
            else {
                DSLOG_INFO("dsGetAllmStatus loaded");
            }
        }
        if (0 != dsGetAllmStatusFunc) {
            eRet = dsGetAllmStatusFunc (iHdmiPort, allmStatus);
            DSLOG_INFO("dsGetAllmStatusFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsGetAllmStatusFunc = %p", dsGetAllmStatusFunc);
        }
        return eRet;
    }

    static dsError_t getSupportedGameFeaturesList (dsSupportedGameFeatureList_t *fList) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsGetSupportedGameFeaturesList_t)(dsSupportedGameFeatureList_t *fList);
        static dsGetSupportedGameFeaturesList_t dsGetSupportedGameFeaturesListFunc = 0;
        if (dsGetSupportedGameFeaturesListFunc == 0) {
            dsGetSupportedGameFeaturesListFunc = (dsGetSupportedGameFeaturesList_t)resolve(RDK_DSHAL_NAME, "dsGetSupportedGameFeaturesList");
            if(dsGetSupportedGameFeaturesListFunc == 0) {
                DSLOG_ERR("dsGetSupportedGameFeaturesList is not defined");
            }
            else {
                DSLOG_INFO("dsGetSupportedGameFeaturesList loaded");
            }
        }
        if (0 != dsGetSupportedGameFeaturesListFunc) {
            eRet = dsGetSupportedGameFeaturesListFunc (fList);
            DSLOG_INFO("dsGetSupportedGameFeaturesListFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsGetSupportedGameFeaturesListFunc = %p", dsGetSupportedGameFeaturesListFunc);
        }
        return eRet;
    }

    static dsError_t getAVLatency_hal (int *audio_latency, int *video_latency)
    {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsGetAVLatency_t)(int *audio_latency, int *video_latency);
        static dsGetAVLatency_t dsGetAVLatencyFunc = 0;
        if (dsGetAVLatencyFunc == 0) {
            dsGetAVLatencyFunc = (dsGetAVLatency_t)resolve(RDK_DSHAL_NAME, "dsGetAVLatency");
            if(dsGetAVLatencyFunc == 0) {
                DSLOG_ERR("dsGetAVLatency is not defined");
            }
            else {
                DSLOG_INFO("dsGetAVLatency loaded");
            }
        }
        if (0 != dsGetAVLatencyFunc) {
            eRet = dsGetAVLatencyFunc (audio_latency, video_latency);
            DSLOG_INFO("dsGetAVLatencyFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsGetAVLatencyFunc = %p", dsGetAVLatencyFunc);
        }
        return eRet;
    }

    static dsError_t getHdmiVersion (dsHdmiInPort_t iHdmiPort, dsHdmiMaxCapabilityVersion_t  *capversion) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsGetHdmiVersion_t)(dsHdmiInPort_t iHdmiPort, dsHdmiMaxCapabilityVersion_t  *capversion);
        static dsGetHdmiVersion_t dsGetHdmiVersionFunc = 0;
        if (dsGetHdmiVersionFunc == 0) {
            dsGetHdmiVersionFunc = (dsGetHdmiVersion_t)resolve(RDK_DSHAL_NAME, "dsGetHdmiVersion");
            if(dsGetHdmiVersionFunc == 0) {
                DSLOG_ERR("dsGetHdmiVersion is not defined");
                eRet = dsERR_GENERAL;
            }
            else {
                DSLOG_INFO("dsGetHdmiVersion loaded");
            }
        }
        if (0 != dsGetHdmiVersionFunc) {
            eRet = dsGetHdmiVersionFunc (iHdmiPort, capversion);
            DSLOG_INFO("dsGetHdmiVersionFunc eRet: %d", eRet);
        }
        return eRet;
    }

    void setAllCallbacks(const CallbackBundle bundle) override
    {
        ENTRY_LOG;
        DSLOG_INFO(" profileType %d", profileType);
        if (!m_hdmiInInitialized && m_hdmiInPlatInitialized) {
            DSLOG_INFO("HdmiIn platform callback Initialization");
            if (TV == profileType)
            {
                DSLOG_INFO(" its TV Profile");
                if (bundle.OnHDMIInHotPlugEvent) {
                    DSLOG_INFO("HDMI In Hot Plug Event Callback Registered");
                    g_HdmiInHotPlugCallback = bundle.OnHDMIInHotPlugEvent;
                    dsHdmiInRegisterConnectCB(DS_OnHDMIInHotPlugEvent);
                }

                typedef dsError_t (*dsHdmiInRegisterSignalChangeCB_t)(dsHdmiInSignalChangeCB_t CBFunc);
                static dsHdmiInRegisterSignalChangeCB_t signalChangeCBFunc = 0;
                if (bundle.OnHDMIInSignalStatusEvent) {
                    DSLOG_INFO("HDMI In Signal Status Event Callback Registered");
                    g_HdmiInSignalStatusCallback = bundle.OnHDMIInSignalStatusEvent;
                    if (!signalChangeCBFunc) {
                        signalChangeCBFunc = (dsHdmiInRegisterSignalChangeCB_t)resolve(RDK_DSHAL_NAME, "dsHdmiInRegisterSignalChangeCB");
                    }
                    if (signalChangeCBFunc) {
                        signalChangeCBFunc(DS_OnHDMIInSignalStatusEvent);
                    } else {
                        DSLOG_ERR("Failed to resolve dsHdmiInRegisterSignalChangeCB");
                    }
                }

                typedef dsError_t (*dsHdmiInRegisterStatusChangeCB_t)(dsHdmiInStatusChangeCB_t CBFunc);
                static dsHdmiInRegisterStatusChangeCB_t StatusCBFunc = 0;
                if (bundle.OnHDMIInStatusEvent) {
                    DSLOG_INFO("HDMI In Status Event Callback Registered");
                    g_HdmiInStatusCallback = bundle.OnHDMIInStatusEvent;
                    if (!StatusCBFunc) {
                        StatusCBFunc = (dsHdmiInRegisterStatusChangeCB_t)resolve(RDK_DSHAL_NAME, "dsHdmiInRegisterStatusChangeCB");
                    }
                    if (StatusCBFunc) {
                        StatusCBFunc(DS_OnHDMIInStatusEvent);
                    } else {
                        DSLOG_ERR("Failed to resolve dsHdmiInRegisterStatusChangeCB");
                    }
                }

                typedef dsError_t (*dsHdmiInRegisterVideoModeUpdateCB_t)(dsHdmiInVideoModeUpdateCB_t CBFunc);
                static dsHdmiInRegisterVideoModeUpdateCB_t videoModeUpdateCBFunc = 0;
                if (bundle.OnHDMIInVideoModeUpdateEvent) {
                    DSLOG_INFO("HDMI In Video Mode Update Event Callback Registered");
                    g_HdmiInVideoModeUpdateCallback = bundle.OnHDMIInVideoModeUpdateEvent;
                    if (!videoModeUpdateCBFunc) {
                        videoModeUpdateCBFunc = (dsHdmiInRegisterVideoModeUpdateCB_t)resolve(RDK_DSHAL_NAME, "dsHdmiInRegisterVideoModeUpdateCB");
                    }
                    if (videoModeUpdateCBFunc) {
                        videoModeUpdateCBFunc(DS_OnHDMIInVideoModeUpdateEvent);
                    } else {
                        DSLOG_ERR("Failed to resolve dsHdmiInRegisterVideoModeUpdateCB");
                    }
                }

                typedef dsError_t (*dsHdmiInRegisterAllmChangeCB_t)(dsHdmiInAllmChangeCB_t CBFunc);
                static dsHdmiInRegisterAllmChangeCB_t allmChangeCBFunc = 0;
                if (bundle.OnHDMIInAllmStatusEvent) {
                    DSLOG_INFO("HDMI In ALLM Status Event Callback Registered");
                    g_HdmiInAllmStatusCallback = bundle.OnHDMIInAllmStatusEvent;
                    if (!allmChangeCBFunc) {
                        allmChangeCBFunc = (dsHdmiInRegisterAllmChangeCB_t)resolve(RDK_DSHAL_NAME, "dsHdmiInRegisterAllmChangeCB");
                    }
                    if (allmChangeCBFunc) {
                        allmChangeCBFunc(DS_OnHDMIInAllmStatusEvent);
                    } else {
                        DSLOG_ERR("Failed to resolve dsHdmiInRegisterALLMChangeCB");
                    }
                }

                typedef dsError_t (*dsHdmiInRegisterVRRChangeCB_t)(dsHdmiInVRRChangeCB_t CBFunc);
                static dsHdmiInRegisterVRRChangeCB_t vrrChangeCBFunc = 0;
                if (bundle.OnHDMIInVRRStatusEvent) {
                    DSLOG_INFO("HDMI In VRR Status Event Callback Registered");
                    g_HdmiInVRRStatusCallback = bundle.OnHDMIInVRRStatusEvent;
                    if (!vrrChangeCBFunc) {
                        vrrChangeCBFunc = (dsHdmiInRegisterVRRChangeCB_t)resolve(RDK_DSHAL_NAME, "dsHdmiInRegisterVRRChangeCB");
                    }
                    if (vrrChangeCBFunc) {
                        vrrChangeCBFunc(DS_OnHDMIInVRRStatusEvent);
                    } else {
                        DSLOG_WARN("dsHdmiInRegisterVRRChangeCB not supported on this platform");
                    }
                }

                typedef dsError_t (*dsHdmiInRegisterAviContentTypeChangeCB_t)(dsHdmiInAviContentTypeChangeCB_t CBFunc);
                static dsHdmiInRegisterAviContentTypeChangeCB_t AviContentTypeChangeCBFunc = 0;
                if (bundle.OnHDMIInAVIContentTypeEvent) {
                    DSLOG_INFO("HDMI In AVI Content Type Event Callback Registered");
                    g_HdmiInAviContentTypeCallback = bundle.OnHDMIInAVIContentTypeEvent;
                    if (!AviContentTypeChangeCBFunc) {
                        AviContentTypeChangeCBFunc = (dsHdmiInRegisterAviContentTypeChangeCB_t)resolve(RDK_DSHAL_NAME, "dsHdmiInRegisterAviContentTypeChangeCB");
                    }
                    if (AviContentTypeChangeCBFunc) {
                        AviContentTypeChangeCBFunc(DS_OnHDMIInAVIContentTypeEvent);
                    } else {
                        DSLOG_ERR("Failed to resolve dsHdmiInRegisterAviContentTypeChangeCB");
                    }
                }

                typedef dsError_t (*dsHdmiInRegisterAVLatencyChangeCB_t)(dsAVLatencyChangeCB_t CBFunc);
                static dsHdmiInRegisterAVLatencyChangeCB_t AVLatencyChangeCBFunc = 0;
                if (bundle.OnHDMIInAVLatencyEvent) {
                    DSLOG_INFO("HDMI In AV Latency Event Callback Registered");
                    g_HdmiInAVLatencyCallback = bundle.OnHDMIInAVLatencyEvent;
                    if (!AVLatencyChangeCBFunc) {
                        AVLatencyChangeCBFunc = (dsHdmiInRegisterAVLatencyChangeCB_t)resolve(RDK_DSHAL_NAME, "dsHdmiInRegisterAVLatencyChangeCB");
                    }
                    if (AVLatencyChangeCBFunc && isDalsEnabled) {
                        AVLatencyChangeCBFunc(DS_OnHDMIInAVLatencyEvent);
                    } else {
                        DSLOG_WARN("dsHdmiInRegisterAVLatencyChangeCB not supported or DALS disabled");
                    }
                }
            }
        }
        EXIT_LOG;
    }

    void getPersistenceValue() override
    {
        if (!m_hdmiInInitialized && m_hdmiInPlatInitialized) {
            // Mirrors dsHdmiIn.c _dsHdmiInInit: bound all per-port loops by the HAL-reported input count
            // so every supported port (not just a hardcoded subset) gets its ARC/ALLM/VRR/EDID state loaded.
            uint8_t noOfSupportedHdmiInputs = 0;
            dsHdmiInGetNumberOfInputs(&noOfSupportedHdmiInputs);
            DSLOG_INFO("Number of Inputs: %d", noOfSupportedHdmiInputs);

            int itr = 0;
            bool isARCCapable = false;
            for (itr = 0; itr < noOfSupportedHdmiInputs; itr++) {
                isARCCapable = false;
                isHdmiARCPort (itr, &isARCCapable);
                hdmiInCap_gs.isPortArcCapable[itr] = isARCCapable; 
            }

            for (itr = 0; itr < noOfSupportedHdmiInputs; itr++) {
                std::string port_edidAllmSupport = "HDMI" + std::to_string(itr) + ".edidallmEnable";
                m_edidallmsupport[itr] = getHdmiInPortPersistValue(port_edidAllmSupport, itr);
            }

            for (itr = 0; itr < noOfSupportedHdmiInputs; itr++) {
                std::string port_vrrSupport = "HDMI" + std::to_string(itr) + ".vrrEnable";
                m_vrrsupport[itr] = getHdmiInPortPersistValue(port_vrrSupport, itr);
            }

            for (itr = 0; itr < noOfSupportedHdmiInputs; itr++) {
                std::string port_edidVer = "HDMI" + std::to_string(itr) + ".edidversion";
                std::string _EdidVersion("1");
                try {
                    _EdidVersion = device::HostPersistence::getInstance().getProperty(port_edidVer);
                    m_edidversion[itr] = static_cast<tv_hdmi_edid_version_t>(atoi (_EdidVersion.c_str()));
                } catch(...) {
                    try {
                        DSLOG_ERR("Port HDMI%d: Exception in Getting the EDID version from persistence storage. Try system default...", itr);
                        _EdidVersion = device::HostPersistence::getInstance().getDefaultProperty(port_edidVer);
                        m_edidversion[itr] = static_cast<tv_hdmi_edid_version_t>(atoi (_EdidVersion.c_str()));
                    }
                    catch(...) {
                        DSLOG_ERR("Port HDMI%d: Exception in Getting the EDID version from system default.....", itr);
                        m_edidversion[itr] = HDMI_EDID_VER_20;
                    }
                }
            }

            for (itr = 0; itr < noOfSupportedHdmiInputs; itr++) {
                if (getVRRSupport(static_cast<dsHdmiInPort_t>(itr), &m_hdmiPortVrrCaps[itr]) >= 0) {
                    DSLOG_INFO("Port HDMI%d: VRR capability : %d", itr, m_hdmiPortVrrCaps[itr]);
                }
            }
            for (itr = 0; itr < noOfSupportedHdmiInputs; itr++) {
                if (setEdidVersion (static_cast<dsHdmiInPort_t>(itr), m_edidversion[itr]) >= 0) {
                    DSLOG_INFO("Port HDMI%d: Initialized EDID Version : %d", itr, m_edidversion[itr]);
                }
            }
            m_hdmiInInitialized = 1;
        }

        DSLOG_INFO("Set Callbacks");
    }

    #if 0
    profile_t searchRdkProfile(void) {
        DSLOG_INFO("Entering searchRdkProfile");
        const char* devPropPath = "/etc/device.properties";
        char line[256], *rdkProfile = NULL;
        profile_t ret = PROFILE_INVALID;
        FILE* file;

        file = fopen(devPropPath, "r");
        if (file == NULL) {
            DSLOG_INFO(" device.properties file not found.");
            return PROFILE_INVALID;
        }

        while (fgets(line, sizeof(line), file)) {
            rdkProfile = strstr(line, RDK_PROFILE);
            if (rdkProfile != NULL) {
                rdkProfile = strchr(line, '=');
                DSLOG_INFO(" Found RDK_PROFILE");
                break;
            }
        }
        if(rdkProfile != NULL)
        {
            rdkProfile++; // Move past the '=' character
            if(0 == strncmp(rdkProfile, PROFILE_STR_TV, strlen(PROFILE_STR_TV))) {
                ret = PROFILE_TV;
            } else if (0 == strncmp(rdkProfile, PROFILE_STR_STB, strlen(PROFILE_STR_STB))) {
                ret = PROFILE_STB;
            }
        }
        else
        {
            DSLOG_INFO(" NOT FOUND RDK_PROFILE in device properties file");
            ret = PROFILE_INVALID;
        }

        fclose(file);
        DSLOG_INFO("Exit searchRdkProfile: RDK_PROFILE = %d", ret);
        return ret;
    }
    #endif

    void getDynamicAutoLatencyConfig()
    {
        RFC_ParamData_t param = {0};
        WDMP_STATUS status = getRFCParameter((char*)"dssrv", TVSETTINGS_DALS_RFC_PARAM, &param);
        DSLOG_INFO("DALS Feature Enable = [ %s ]", param.value);
        if(WDMP_SUCCESS == status && (strncasecmp(param.value,"true",4) == 0)) {
            isDalsEnabled = true;
            DSLOG_INFO("Value of isDalsEnabled = [ %d ]", isDalsEnabled);
        }
        else {
            DSLOG_ERR("Fetching RFC for DALS failed or DALS is disabled: %d", status);
        }
    }

    // Missing functions from dsHdmiIn.c
    void updateEdidAllmBitValuesInPersistence(dsHdmiInPort_t iHdmiPort, bool allmSupport)
    {
        DSLOG_INFO("Updating values of edid allm bit in persistence");
        switch(iHdmiPort){
            case dsHDMI_IN_PORT_0:
                device::HostPersistence::getInstance().persistHostProperty("HDMI0.edidallmEnable", allmSupport ? "TRUE" : "FALSE");
                DSLOG_INFO("Port %s: Persist EDID Allm Bit: %d", "HDMI0", allmSupport);
                break;
            case dsHDMI_IN_PORT_1:
                device::HostPersistence::getInstance().persistHostProperty("HDMI1.edidallmEnable", allmSupport ? "TRUE" : "FALSE");
                DSLOG_INFO("Port %s: Persist EDID Allm Bit: %d", "HDMI1", allmSupport);
                break;
            case dsHDMI_IN_PORT_2:
                device::HostPersistence::getInstance().persistHostProperty("HDMI2.edidallmEnable", allmSupport ? "TRUE" : "FALSE");
                DSLOG_INFO("Port %s: Persist EDID Allm Bit: %d", "HDMI2", allmSupport);
                break;
            case dsHDMI_IN_PORT_3:
                device::HostPersistence::getInstance().persistHostProperty("HDMI3.edidallmEnable", allmSupport ? "TRUE" : "FALSE");
                DSLOG_INFO("Port %s: Persist EDID Allm Bit: %d", "HDMI3", allmSupport);
                break;
            default:
                DSLOG_WARN("Invalid HDMI port %d for ALLM persistence update", iHdmiPort);
                break;
        }
    }

    static void DS_OnHDMIInHotPlugEvent(const dsHdmiInPort_t port, const bool isConnected)
    {
        DSLOG_INFO("DS_OnHDMIInHotPlugEvent event Received: port=%d, isConnected=%s", port, isConnected ? "true" : "false");
        if (g_HdmiInHotPlugCallback) {
            g_HdmiInHotPlugCallback(static_cast<DeviceSettingsHDMIIn::HDMIInPort>(port), isConnected);
        }
    }

    static void DS_OnHDMIInSignalStatusEvent(const dsHdmiInPort_t port, const dsHdmiInSignalStatus_t signalStatus)
    {
        DSLOG_INFO("DS_OnHDMIInSignalStatusEvent event Received: port=%d, signalStatus=%d", port, signalStatus);
        if (g_HdmiInSignalStatusCallback) {
            g_HdmiInSignalStatusCallback(static_cast<HDMIInPort>(port), static_cast<HDMIInSignalStatus>(signalStatus));
        }
    }

    static void DS_OnHDMIInStatusEvent(const dsHdmiInStatus_t status)
    {
        DSLOG_INFO("DS_OnHDMIInStatusEvent event Received: Port=%d, isPresented=%s", status.activePort, status.isPresented ? "true" : "false");
        
        if (g_HdmiInStatusCallback) {
            g_HdmiInStatusCallback(static_cast<HDMIInPort>(status.activePort), status.isPresented);
        }
    }

    static void DS_OnHDMIInVideoModeUpdateEvent(const dsHdmiInPort_t port, const dsVideoPortResolution_t videoPortResolution)
    {
        DSLOG_INFO("DS_OnHDMIInVideoModeUpdateEvent event Received: port=%d", port); // adjust as needed
        DSLOG_INFO("Video Mode: %s pixelResolution %d aspectRatio %d stereoScopicMode %d frameRate %d", videoPortResolution.name, videoPortResolution.pixelResolution, videoPortResolution.aspectRatio, videoPortResolution.stereoScopicMode, videoPortResolution.frameRate);

        if (g_HdmiInVideoModeUpdateCallback) {
            HDMIVideoPortResolution res;
            res.name              = std::string(videoPortResolution.name);
            res.pixelResolution   = static_cast<HDMIInVideoResolution>(videoPortResolution.pixelResolution);
            res.aspectRatio       = static_cast<HDMIVideoAspectRatio>(videoPortResolution.aspectRatio);
            res.stereoScopicMode  = static_cast<HDMIInVideoStereoScopicMode>(videoPortResolution.stereoScopicMode);
            res.frameRate         = static_cast<HDMIInVideoFrameRate>(videoPortResolution.frameRate);
            res.interlaced        = videoPortResolution.interlaced;
            g_HdmiInVideoModeUpdateCallback(static_cast<HDMIInPort>(port), res);
        }
    }

    static void DS_OnHDMIInAllmStatusEvent(const dsHdmiInPort_t port, const bool allmStatus)
    {
        DSLOG_INFO("DS_OnHDMIInAllmStatusEvent event Received: port=%d, allmStatus=%s", port, allmStatus ? "true" : "false");
        if (g_HdmiInAllmStatusCallback) {
            g_HdmiInAllmStatusCallback(static_cast<HDMIInPort>(port), allmStatus);
        }
    }

    static void DS_OnHDMIInAVIContentTypeEvent(const dsHdmiInPort_t port, const dsAviContentType_t aviContentType)
    {
        DSLOG_INFO("DS_OnHDMIInAVIContentTypeEvent event Received: port=%d, aviContentType=%d", port, aviContentType);
        if (g_HdmiInAviContentTypeCallback) {
            g_HdmiInAviContentTypeCallback(static_cast<HDMIInPort>(port), static_cast<HDMIInAviContentType>(aviContentType));
        }
    }

    static void DS_OnHDMIInAVLatencyEvent(const int32_t audioDelay, const int32_t videoDelay)
    {
        DSLOG_INFO("DS_OnHDMIInAVLatencyEvent event Received: audioDelay=%d, videoDelay=%d", audioDelay, videoDelay);
        if (g_HdmiInAVLatencyCallback) {
            g_HdmiInAVLatencyCallback(audioDelay, videoDelay);
        }
    }

    static void DS_OnHDMIInVRRStatusEvent(const dsHdmiInPort_t port, const dsVRRType_t vrrType)
    {
        DSLOG_INFO("DS_OnHDMIInVRRStatusEvent event Received: port=%d, vrrType=%d", port, vrrType);
        if (g_HdmiInVRRStatusCallback) {
            g_HdmiInVRRStatusCallback(static_cast<HDMIInPort>(port), static_cast<HDMIInVRRType>(vrrType));
        }
    }

    virtual uint32_t GetHDMIInNumberOfInputs(int32_t &count) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        uint8_t NumberofInputs = 0;

        if (dsHdmiInGetNumberOfInputs(&NumberofInputs) == dsERR_NONE) {
            count = static_cast<int32_t>(NumberofInputs);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        DSLOG_INFO(" count=%d, retCode=%d", count, retCode);
        return retCode;
    }

    uint32_t GetHDMIInStatus(HDMIInStatus &hdmiStatus, IHDMIInPortConnectionStatusIterator*& portConnectionStatus) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInStatus_t status;
        if (dsHdmiInGetStatus(&status) == dsERR_NONE) {
            hdmiStatus.activePort = static_cast<HDMIInPort>(status.activePort);
            hdmiStatus.isPresented = status.isPresented;
            DSLOG_INFO(" activePort=%d, isPresented=%s", status.activePort, status.isPresented ? "true" : "false");

            /* Build per-port connection status iterator from dsHdmiInStatus_t.isPortConnected[]. */
            std::vector<DeviceSettingsHDMIIn::HDMIPortConnectionStatus> portStatuses;
            for (int p = 0; p < dsHDMI_IN_PORT_MAX; p++) {
                DeviceSettingsHDMIIn::HDMIPortConnectionStatus ps;
                ps.isPortConnected = status.isPortConnected[p];
                portStatuses.push_back(ps);
                DSLOG_INFO(" port[%d] isPortConnected=%s", p, ps.isPortConnected ? "true" : "false");
            }
            portConnectionStatus = WPEFramework::Core::Service<WPEFramework::RPC::IteratorType<IHDMIInPortConnectionStatusIterator>>::Create<IHDMIInPortConnectionStatusIterator>(portStatuses);

            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t GetHDMIInAVLatency(uint32_t &videoLatency, uint32_t &audioLatency) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        int vLatency = 0;
        int aLatency = 0;
        if (getAVLatency_hal(&aLatency, &vLatency) == dsERR_NONE) {
            audioLatency = static_cast<uint32_t>(aLatency);
            videoLatency = static_cast<uint32_t>(vLatency);
            DSLOG_INFO(" audioLatency=%d, videoLatency=%d", audioLatency, videoLatency);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t GetHDMIInAllmStatus(const HDMIInPort port, bool &allmStatus) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        bool status = false;
        if (getAllmStatus(hdmiPort, &status) == dsERR_NONE) {
            allmStatus = status;
            DSLOG_INFO(" port=%d, allmStatus=%s", hdmiPort, allmStatus ? "true" : "false");
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t GetHDMIInEdid2AllmSupport(const HDMIInPort port, bool &allmSupport) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        if (hdmiPort < dsHDMI_IN_PORT_MAX) {
            allmSupport = m_edidallmsupport[hdmiPort];
            DSLOG_INFO(" port=%d, allmSupport=%s", hdmiPort, allmSupport ? "true" : "false");
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t SetHDMIInEdid2AllmSupport(const HDMIInPort port, bool allmSupport) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        if (hdmiPort < dsHDMI_IN_PORT_MAX) {
            DSLOG_INFO("In SetHDMIInEdid2AllmSupport, checking m_edidversion of port %d : %d", hdmiPort, m_edidversion[hdmiPort]);
            if(m_edidversion[hdmiPort] == HDMI_EDID_VER_20) { // if the edidver is 2.0, then only set the allm bit in edid
                if (setEdid2AllmSupport(hdmiPort, allmSupport) == dsERR_NONE) {
                    updateEdidAllmBitValuesInPersistence(hdmiPort, allmSupport);
                    m_edidallmsupport[hdmiPort] = allmSupport;
                    DSLOG_INFO(" port=%d, allmSupport=%s", hdmiPort, allmSupport ? "true" : "false");
                    retCode = WPEFramework::Core::ERROR_NONE;
                }
            } else {
                DSLOG_INFO("EDID version is not 2.0, cannot set ALLM support for port %d", hdmiPort);
                retCode = WPEFramework::Core::ERROR_UNAVAILABLE;
            }
        }
        return retCode;
    }

    uint32_t GetSupportedGameFeaturesList(IHDMIInGameFeatureListIterator *& gameFeatureList) override
    {
        dsSupportedGameFeatureList_t featureList = {};
        const dsError_t result = getSupportedGameFeaturesList(&featureList);
        if (result != dsERR_NONE) {
            DSLOG_ERR("dsGetSupportedGameFeaturesList failed with error: %d", result);
            gameFeatureList = nullptr;
            return WPEFramework::Core::ERROR_GENERAL;
        }

        std::list<DeviceSettingsHDMIIn::HDMIInGameFeatureList> features;
        std::stringstream stream(featureList.gameFeatureList);
        std::string feature;
        int32_t featureCount = 0;

        while ((featureCount < featureList.gameFeatureCount) && std::getline(stream, feature, ',')) {
            feature.erase(std::remove(feature.begin(), feature.end(), '"'), feature.end());
            feature.erase(std::remove(feature.begin(), feature.end(), ' '), feature.end());
            if (!feature.empty()) {
                DeviceSettingsHDMIIn::HDMIInGameFeatureList gameFeature;
                gameFeature.gameFeature = feature;
                features.emplace_back(gameFeature);
                ++featureCount;
            }
        }

        gameFeatureList = WPEFramework::Core::Service<WPEFramework::RPC::IteratorType<IHDMIInGameFeatureListIterator>>
                              ::Create<IHDMIInGameFeatureListIterator>(features);
        DSLOG_INFO("HAL reported %d game features; returned %zu features", featureList.gameFeatureCount, features.size());

        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SelectHDMIInPort(const HDMIInPort port, const bool requestAudioMix, const bool topMostPlane, const HDMIVideoPlaneType videoPlaneType) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        dsVideoPlaneType_t videoType = static_cast<dsVideoPlaneType_t>(videoPlaneType);
        if (dsHdmiInSelectPort(hdmiPort, requestAudioMix, videoType, topMostPlane) == dsERR_NONE) {
            DSLOG_INFO(" port=%d, requestAudioMix=%s, topMostPlane=%s, videoPlaneType=%d", hdmiPort, requestAudioMix ? "true" : "false", topMostPlane ? "true" : "false", videoPlaneType);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t ScaleHDMIInVideo(const HDMIInVideoRectangle videoPosition) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        if (dsHdmiInScaleVideo(videoPosition.x, videoPosition.y, videoPosition.width, videoPosition.height) == dsERR_NONE) {
            DSLOG_INFO("Successfully set the video position x=%d, y=%d, width=%d, height=%d",
                    videoPosition.x, videoPosition.y, videoPosition.width, videoPosition.height);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t SelectHDMIZoomMode(const HDMIInVideoZoom zoomMode) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsVideoZoom_t zoom = static_cast<dsVideoZoom_t>(zoomMode);
        if ((retCode = dsHdmiInSelectZoomMode(zoom)) == dsERR_NONE) {
            DSLOG_INFO("Successfully set the zoom mode: %d", zoom);
            retCode = WPEFramework::Core::ERROR_NONE;
        } else {
            DSLOG_INFO("Failed to select zoom %d and return errorcode %d", zoom, retCode);
        }
        return retCode;
    }

    static dsError_t getEDIDBytesInfo (dsHdmiInPort_t iHdmiPort, unsigned char *edid, int *length) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsGetEDIDBytesInfo_t)(dsHdmiInPort_t iHdmiPort, unsigned char *edid, int *length);
        static dsGetEDIDBytesInfo_t dsGetEDIDBytesInfoFunc = 0;
        if (dsGetEDIDBytesInfoFunc == 0) {
            dsGetEDIDBytesInfoFunc = (dsGetEDIDBytesInfo_t)resolve(RDK_DSHAL_NAME, "dsGetEDIDBytesInfo");
            if(dsGetEDIDBytesInfoFunc == 0) {
                DSLOG_ERR("dsGetEDIDBytesInfo is not defined");
                eRet = dsERR_GENERAL;
            } else {
                DSLOG_INFO("dsGetEDIDBytesInfo loaded");
            }
        }
        if (0 != dsGetEDIDBytesInfoFunc) {
            DSLOG_INFO("Entering dsGetEDIDBytesInfoFunc");
            eRet = dsGetEDIDBytesInfoFunc (iHdmiPort, edid, length);
            DSLOG_INFO("dsGetEDIDBytesInfoFunc eRet: %d data len: %d", eRet, *length);
        }
        return eRet;
    }

    uint32_t GetEdidBytes(const HDMIInPort port, const uint16_t edidBytesLength, uint8_t edidBytes[]) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        int length = static_cast<int>(edidBytesLength);
        if (getEDIDBytesInfo(hdmiPort, edidBytes, &length) == dsERR_NONE) {
            DSLOG_INFO(" port=%d, edidBytesLength=%d, actualLength=%d", hdmiPort, edidBytesLength, length);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    static dsError_t getHDMISPDInfo (dsHdmiInPort_t iHdmiPort, unsigned char *spd) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsGetHDMISPDInfo_t)(dsHdmiInPort_t iHdmiPort, unsigned char *data);
        static dsGetHDMISPDInfo_t dsGetHDMISPDInfoFunc = 0;
        if (dsGetHDMISPDInfoFunc == 0) {
            dsGetHDMISPDInfoFunc = (dsGetHDMISPDInfo_t)resolve(RDK_DSHAL_NAME, "dsGetHDMISPDInfo");
            if(dsGetHDMISPDInfoFunc == 0) {
                DSLOG_ERR("dsGetHDMISPDInfo is not defined");
                eRet = dsERR_GENERAL;
            } else {
                DSLOG_INFO("dsGetHDMISPDInfo loaded");
            }
        }
        if (0 != dsGetHDMISPDInfoFunc) {
            eRet = dsGetHDMISPDInfoFunc (iHdmiPort, spd);
            DSLOG_INFO("dsGetHDMISPDInfoFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsGetHDMISPDInfoFunc = %p", dsGetHDMISPDInfoFunc);
        }
        return eRet;
    }

    uint32_t GetHDMISPDInformation(const HDMIInPort port, const uint16_t spdBytesLength, uint8_t spdBytes[]) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);

        if (getHDMISPDInfo(hdmiPort, spdBytes) == dsERR_NONE) {
            DSLOG_INFO(" port=%d, spdBytesLength=%d", hdmiPort, spdBytesLength);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t GetHDMIEdidVersion(const HDMIInPort port, HDMIInEdidVersion &edidVersion) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        int edidVer = 0;
        if (getEdidVersion(hdmiPort, &edidVer) == dsERR_NONE) {
            edidVersion = static_cast<HDMIInEdidVersion>(edidVer);
            DSLOG_INFO(" port=%d, edidVersion=%d", hdmiPort, edidVer);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t SetHDMIEdidVersion(const HDMIInPort port, const HDMIInEdidVersion edidVersion) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        tv_hdmi_edid_version_t edidVer = static_cast<tv_hdmi_edid_version_t>(edidVersion);
        if (setEdidVersion(hdmiPort, edidVer) == dsERR_NONE) {
            m_edidversion[hdmiPort] = edidVer;
            DSLOG_INFO(" port=%d, edidVersion=%d", hdmiPort, edidVer);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t GetHDMIVideoMode(HDMIVideoPortResolution &videoPortResolution) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsVideoPortResolution_t videoRes;

        memset(&videoRes, 0, sizeof(videoRes));

        if (dsHdmiInGetCurrentVideoMode(&videoRes) == dsERR_NONE) {
            // Validate that we have reasonable data before logging
                DSLOG_INFO(" Raw HAL data - name=<invalid>, pixelRes=%u, aspectRatio=%u, stereoScopicMode=%u, frameRate=%u, interlaced=%d",
                        videoRes.pixelResolution, videoRes.aspectRatio, videoRes.stereoScopicMode, videoRes.frameRate, videoRes.interlaced);

            if (videoRes.name[0] != '\0' && strlen(videoRes.name) < sizeof(videoRes.name)) {
                videoPortResolution.name = std::string(videoRes.name);
            } else {
                videoPortResolution.name = "UNKNOWN";
                DSLOG_WARN(" Invalid video mode name, using 'UNKNOWN'");
            }

            videoPortResolution.pixelResolution = static_cast<HDMIInVideoResolution>(videoRes.pixelResolution);
            videoPortResolution.aspectRatio = static_cast<HDMIVideoAspectRatio>(videoRes.aspectRatio);
            videoPortResolution.stereoScopicMode = static_cast<HDMIInVideoStereoScopicMode>(videoRes.stereoScopicMode);
            videoPortResolution.frameRate = static_cast<HDMIInVideoFrameRate>(videoRes.frameRate);
            videoPortResolution.interlaced = videoRes.interlaced;

            // Debug print all the assigned data
            DSLOG_INFO(" Assigned data - name='%s', pixelResolution=%u, aspectRatio=%u, stereoScopicMode=%u, frameRate=%u, interlaced=%d",
                    videoPortResolution.name.c_str(), 
                    videoPortResolution.pixelResolution,
                    videoPortResolution.aspectRatio,
                    videoPortResolution.stereoScopicMode,
                    videoPortResolution.frameRate,
                    videoPortResolution.interlaced);

            retCode = WPEFramework::Core::ERROR_NONE;
        } else {
            DSLOG_ERR(" dsHdmiInGetCurrentVideoMode failed");
            // Initialize output with safe defaults
            videoPortResolution.name = "ERROR";
            videoPortResolution.pixelResolution = static_cast<HDMIInVideoResolution>(0);
            videoPortResolution.aspectRatio = static_cast<HDMIVideoAspectRatio>(0);
            videoPortResolution.stereoScopicMode = static_cast<HDMIInVideoStereoScopicMode>(0);
            videoPortResolution.frameRate = static_cast<HDMIInVideoFrameRate>(0);
            videoPortResolution.interlaced = false;
        }
        return retCode;
    }

    uint32_t GetHDMIVersion(const HDMIInPort port, HDMIInCapabilityVersion &capabilityVersion) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        dsHdmiMaxCapabilityVersion_t capversion;
        if (getHdmiVersion(hdmiPort, &capversion) == dsERR_NONE) {
            capabilityVersion = static_cast<HDMIInCapabilityVersion>(capversion);
            DSLOG_INFO(" port=%d, capabilityVersion=%d", hdmiPort, capversion);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    uint32_t SetVRRSupport(const HDMIInPort port, const bool vrrSupport) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        if (hdmiPort < dsHDMI_IN_PORT_MAX) {
            DSLOG_INFO("In SetVRRSupport, checking m_edidversion of port %d : %d", hdmiPort, m_edidversion[hdmiPort]);
            if(m_edidversion[hdmiPort] == HDMI_EDID_VER_20) { // if the edidver is 2.0, then only set the vrr bit in edid
                if (setVRRSupport(hdmiPort, vrrSupport) == dsERR_NONE) {
                    updateVRRBitValuesInPersistence(hdmiPort, vrrSupport);
                    m_vrrsupport[hdmiPort] = vrrSupport;
                    DSLOG_INFO(" port=%d, vrrSupport=%d", hdmiPort, vrrSupport);
                    retCode = WPEFramework::Core::ERROR_NONE;
                }
            } else {
                DSLOG_INFO("EDID version is not 2.0, cannot set VRR support for port %d", hdmiPort);
                retCode = WPEFramework::Core::ERROR_UNAVAILABLE;
            }
        }
        return retCode;
    }

    uint32_t GetVRRSupport(const HDMIInPort port, bool &vrrSupport) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        if (hdmiPort < dsHDMI_IN_PORT_MAX) {
            vrrSupport = m_vrrsupport[hdmiPort];
            DSLOG_INFO(" port=%d, vrrSupport=%d", hdmiPort, vrrSupport);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    static dsError_t getVRRStatus (dsHdmiInPort_t iHdmiPort, dsHdmiInVrrStatus_t *vrrStatus) {
        dsError_t eRet = dsERR_GENERAL;
        typedef dsError_t (*dsHdmiInGetVRRStatus_t)(dsHdmiInPort_t iHdmiPort, dsHdmiInVrrStatus_t *vrrStatus);
        static dsHdmiInGetVRRStatus_t dsHdmiInGetVRRStatusFunc = 0;
        if (dsHdmiInGetVRRStatusFunc == 0) {
            dsHdmiInGetVRRStatusFunc = (dsHdmiInGetVRRStatus_t)resolve(RDK_DSHAL_NAME, "dsHdmiInGetVRRStatus");
            if(dsHdmiInGetVRRStatusFunc == 0) {
                DSLOG_ERR("dsHdmiInGetVRRStatus is not defined");
            } else {
                DSLOG_INFO("dsHdmiInGetVRRStatus loaded");
            }
        }
        if (0 != dsHdmiInGetVRRStatusFunc) {
            eRet = dsHdmiInGetVRRStatusFunc (iHdmiPort, vrrStatus);
            DSLOG_INFO("dsHdmiInGetVRRStatusFunc eRet: %d", eRet);
        }
        else {
            DSLOG_INFO("dsHdmiInGetVRRStatusFunc = %p", dsHdmiInGetVRRStatusFunc);
        }
        return eRet;
    }

    uint32_t GetVRRStatus(const HDMIInPort port, HDMIInVRRStatus &vrrStatus) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        dsHdmiInVrrStatus_t status;
        if (getVRRStatus(hdmiPort, &status) == dsERR_NONE) {
            vrrStatus.vrrType = static_cast<HDMIInVRRType>(status.vrrType);
            vrrStatus.vrrFreeSyncFramerateHz = status.vrrAmdfreesyncFramerate_Hz;
            DSLOG_INFO(" port=%d, vrrType=%d, vrrFreeSyncFramerateHz=%f", hdmiPort, vrrStatus.vrrType, vrrStatus.vrrFreeSyncFramerateHz);
            retCode = WPEFramework::Core::ERROR_NONE;
        }
        return retCode;
    }

    // Helper function to convert dsError_t to WPEFramework error codes
    static uint32_t convertDsErrorToWPEError(dsError_t dsErr) {
        switch (dsErr) {
            case dsERR_NONE:
                return WPEFramework::Core::ERROR_NONE;
            case dsERR_GENERAL:
                return WPEFramework::Core::ERROR_GENERAL;
            case dsERR_INVALID_PARAM:
                return WPEFramework::Core::ERROR_BAD_REQUEST;
            case dsERR_INVALID_STATE:
                return WPEFramework::Core::ERROR_ILLEGAL_STATE;
            case dsERR_OPERATION_NOT_SUPPORTED:
                return WPEFramework::Core::ERROR_UNAVAILABLE;
            default:
                return WPEFramework::Core::ERROR_GENERAL;
        }
    }

    private:
};
