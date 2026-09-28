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

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <iostream>
#include <functional>
#include <string>
#include "dVideoPort.h"
#include "utils/VideoResolutionUtils.h"
#include "dsVideoPort.h"
#include "dsError.h"
#include "dsUtl.h"
#include "dsTypes.h"

#include <WPEFramework/interfaces/IDeviceSettingsVideoPort.h>
#include "DeviceSettingsTypes.h"
#include "DeviceSettingsHALConfig.h"
#include "DeviceSettingsHdmiStatus.h"
#include "DeviceSettingsTelemetry.h"

// Resolution defaults — matches dsVideoPort.c naming
#define DS_VP_DEFAULT_RESOLUTION       "720p"
#define DS_VP_DEFAULT_RESOLUTION_1080P  "1080p"
#define DS_VP_DEFAULT_RESOLUTION_2160P  "2160p"

static int videoPort_isInitialized = 0;
static int videoPort_isPlatInitialized = 0;

// Persistent resolution settings — initialised in getPersistenceValue() based on profileType.
// TV profile (profileType=1) defaults to DS_VP_DEFAULT_RESOLUTION_2160P; STB defaults to DS_VP_DEFAULT_RESOLUTION_1080P.
static std::string _dsHDMIResolution = DS_VP_DEFAULT_RESOLUTION_1080P;
static std::string _dsCompResolution = DS_VP_DEFAULT_RESOLUTION_1080P;
static std::string _dsRFResolution   = DS_VP_DEFAULT_RESOLUTION_1080P;
static std::string _dsBBResolution   = DS_VP_DEFAULT_RESOLUTION_1080P;

// Color depth settings - following dsVideoPort.c pattern
static const dsDisplayColorDepth_t DEFAULT_COLOR_DEPTH = dsDISPLAY_COLORDEPTH_AUTO;
// static dsDisplayColorDepth_t hdmiColorDepth = DEFAULT_COLOR_DEPTH; // Unused variable - commented out

// Static global callback functions for VideoPort events - following HdmiIn pattern
static std::function<void(const ResolutionChange)> g_VideoPortResolutionPreChangeCallback;
static std::function<void(const ResolutionChange)> g_VideoPortResolutionPostChangeCallback;
static std::function<void(const VideoPortHdcpStatus)> g_VideoPortHDCPStatusChangeCallback;
static std::function<void(const HDRStandard)> g_VideoPortVideoFormatUpdateCallback;

class dVideoPortImpl : public hal::dVideoPort::IPlatform {

    // delete copy constructor and assignment operator
    dVideoPortImpl(const dVideoPortImpl&) = delete;
    dVideoPortImpl& operator=(const dVideoPortImpl&) = delete;

    static std::atomic<int>& cachedHdcpStatus()
    {
        static std::atomic<int> status { dsHDCP_STATUS_UNAUTHENTICATED };
        return status;
    }

public:
    dVideoPortImpl()
    {
        DSLOG_INFO("Constructor");
        getInstance() = this; // Set static instance for callback access
        InitialiseHAL();
    }

    virtual ~dVideoPortImpl()
    {
        DSLOG_INFO("Destructor");
        DeInitialiseHAL();
        getInstance() = nullptr; // Clear static instance
    }

    // Singleton getInstance method - following HdmiIn pattern
    static dVideoPortImpl*& getInstance()
    {
        static dVideoPortImpl* instance = nullptr;
        return instance;
    }

    // Legacy parity helper: apply persisted preferred color depth constrained by sink capabilities.
    // This mirrors resetColorDepthOnHdmiReset() flow from dsVideoPort.c.
    static void ApplyPreferredColorDepthAfterHdmiReset(intptr_t preferredHandle = 0)
    {
        intptr_t targetHandle = preferredHandle;
        if (targetHandle == 0) {
            targetHandle = dsGetDefaultPortHandle();
        }

        if (targetHandle == 0) {
            DSLOG_WARN("Skipping preferred color depth reconcile: no HDMI/INTERNAL handle");
            return;
        }

        bool connected = false;
        dsError_t connectedError = dsIsDisplayConnected(targetHandle, &connected);
        if (connectedError != dsERR_NONE || !connected) {
            DSLOG_INFO("Skipping preferred color depth reconcile: connected=%d error=%d",
                static_cast<int>(connected), connectedError);
            return;
        }

        dsDisplayColorDepth_t persisted = static_cast<dsDisplayColorDepth_t>(getPersistentColorDepth());
        DSLOG_INFO("Reconcile preferred color depth after HDMI reset: persisted=0x%x", persisted);

        dsDisplayColorDepth_t current = dsDISPLAY_COLORDEPTH_UNKNOWN;
        dsError_t currentError = getCurrentPreferredColorDepth(targetHandle, &current);
        if (currentError == dsERR_NONE && current == persisted) {
            DSLOG_INFO("Preferred color depth already set to 0x%x", current);
            return;
        }

        dsDisplayColorDepth_t toSet = getBestSupportedColorDepth(targetHandle, persisted);
        dsError_t setError = setPreferredColorDepthHAL(targetHandle, toSet);
        if (setError == dsERR_NONE) {
            DSLOG_INFO("Preferred color depth reconciled to 0x%x (requested persisted=0x%x)", toSet, persisted);
        } else {
            DSLOG_ERR("Failed to reconcile preferred color depth: error=%d", setError);
        }
    }

    void InitialiseHAL()
    {
        // Note: videoPort_isInitialized should only be set in setAllCallbacks after callback registration
        // Don't set it here as it prevents callback registration condition from working

        if (!videoPort_isPlatInitialized) {
            DSLOG_INFO("<dsVideoPort>");
            dsError_t eError = dsVideoPortInit();
            if (dsERR_NONE != eError) {
                DSLOG_ERR(" dsVideoPortInit failed with error: %d", eError);
                return;
            }
            DSLOG_INFO(" dsVideoPortInit succeeded");

            _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPSTATUS, dsHDCP_STATUS_UNAUTHENTICATED);
            _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPVERSION, dsHDCP_VERSION_1X);

            bool connected = false;
            dsError_t displayError = dsIsDisplayConnected(dsGetDefaultPortHandle(), &connected);
            if (displayError != dsERR_NONE) {
                DSLOG_ERR(" dsIsDisplayConnected failed with error: %d", displayError);
            }
            _dsSyncHdmiStatus(DS_HDMI_TAG_HOTPLUP,
                    connected ? dsDISPLAY_EVENT_CONNECTED : dsDISPLAY_EVENT_DISCONNECTED);
            
            // Load persistence values after successful initialization - following dsVideoPort.c pattern
            getPersistenceValue();

                // Legacy parity: apply persisted preferred color depth constrained by sink caps
                // immediately after VideoPort HAL initialization.
                ApplyPreferredColorDepthAfterHdmiReset();
            
            videoPort_isPlatInitialized = 1;
            DSLOG_INFO("completed: videoPort_isPlatInitialized=%d, videoPort_isInitialized=%d",
                    videoPort_isPlatInitialized, videoPort_isInitialized);
        }
    }

    void DeInitialiseHAL()
    {
        if (videoPort_isPlatInitialized)
        {
            dsVideoPortTerm();
            videoPort_isPlatInitialized = 0;
        }
        videoPort_isInitialized = 0;
    }

    static void* resolve(const std::string& libName, const std::string& symbolName) {
        return WPEFramework::Plugin::DeviceSettingsHALLoader::ResolveSymbol(libName, symbolName);
    }

    // Implementation of all VideoPort Platform interface methods
    uint32_t GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t& handle) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" videoPort=%d, index=%d", static_cast<int>(videoPort), index);
        
        dsVideoPortType_t dsVideoPort = convertVideoPortType(videoPort);
        intptr_t dsHandle;
        
        dsError_t eError = dsGetVideoPort(dsVideoPort, index, &dsHandle);
        if (eError == dsERR_NONE) {
            handle = static_cast<int32_t>(dsHandle);
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - handle=%d", handle);
        } else {
            DSLOG_ERR(" dsGetVideoPort failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t IsVideoPortEnabled(const int32_t handle, bool& enabled) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        bool dsEnabled = false;
        dsError_t eError = dsIsVideoPortEnabled(handle, &dsEnabled);
        if (eError == dsERR_NONE) {
            enabled = dsEnabled;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - enabled=%s", enabled ? "true" : "false");
        } else {
            DSLOG_ERR(" dsIsVideoPortEnabled failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t EnableVideoPort(const int32_t handle, const bool enabled) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, enabled=%s", handle, enabled ? "true" : "false");
        
        dsError_t eError = dsEnableVideoPort(handle, enabled);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS");
        } else {
            DSLOG_ERR(" dsEnableVideoPort failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t IsVideoPortDisplayConnected(const int32_t handle, bool& connected) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        bool dsConnected = false;
        dsError_t eError = dsIsDisplayConnected(handle, &dsConnected);
        if (eError == dsERR_NONE) {
            connected = dsConnected;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - connected=%s", connected ? "true" : "false");
        } else {
            DSLOG_ERR(" dsIsDisplayConnected failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t IsVideoPortActive(const int32_t handle, bool& active) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        bool dsActive = false;
        dsError_t eError = dsIsVideoPortActive(handle, &dsActive);
        if (eError == dsERR_NONE) {
            active = dsActive;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - active=%s", active ? "true" : "false");
        } else {
            DSLOG_ERR(" dsIsVideoPortActive failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t GetVideoPortResolution(const int32_t handle, VideoPortResolution& resolution) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);

        dsVideoPortType_t portType = dsVIDEOPORT_TYPE_MAX;
        if (!resolvePortTypeByHandle(handle, portType)) {
            DSLOG_ERR(" unable to resolve video port type for handle=%d", handle);
            return retCode;
        }

        // Legacy dsVideoPort.c behavior:
        // - HDMI/INTERNAL: use HAL current resolution when connected, persist/cache fallback otherwise.
        // - COMPONENT/BB/RF: return cached/persisted resolution string.
        if (portType == dsVIDEOPORT_TYPE_COMPONENT || portType == dsVIDEOPORT_TYPE_BB || portType == dsVIDEOPORT_TYPE_RF) {
            resolution = buildResolutionFromName(getCachedResolutionForPort(portType));
            DSLOG_INFO(" returning cached analog resolution '%s'", resolution.name.c_str());
            return WPEFramework::Core::ERROR_NONE;
        }

        bool isConnected = false;
        dsError_t connectedError = dsIsDisplayConnected(handle, &isConnected);
        if (connectedError != dsERR_NONE) {
            DSLOG_ERR(" dsIsDisplayConnected failed with error: %d", connectedError);
        }

        if (connectedError == dsERR_NONE && !isConnected) {
            resolution = buildResolutionFromName(getCachedResolutionForPort(portType));
            DSLOG_INFO(" display disconnected, returning cached resolution '%s'", resolution.name.c_str());
            return WPEFramework::Core::ERROR_NONE;
        }

        dsVideoPortResolution_t dsResolution;
        memset(&dsResolution, 0, sizeof(dsResolution));
        dsError_t eError = dsGetResolution(handle, &dsResolution);
        if (eError == dsERR_NONE) {
            resolution = convertVideoPortResolution(dsResolution);
            if (resolution.name.empty()) {
                resolution = buildResolutionFromName(getCachedResolutionForPort(portType));
            } else {
                updateCachedResolutionForPort(portType, resolution.name);
            }
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - current resolution '%s'", resolution.name.c_str());
        } else {
            DSLOG_ERR(" dsGetResolution failed with error: %d, using cached fallback", eError);
            resolution = buildResolutionFromName(getCachedResolutionForPort(portType));
            retCode = WPEFramework::Core::ERROR_NONE;
        }

        return retCode;
    }

    uint32_t getIgnoreEDIDStatus(const int32_t handle, bool& ignoreEDID) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO("handle=%d", handle);

        typedef dsError_t (*dsGetIgnoreEDIDStatus_t)(intptr_t handle, bool* ignoreEDID);
        static dsGetIgnoreEDIDStatus_t dsGetIgnoreEDIDStatusFunc = nullptr;

        if (dsGetIgnoreEDIDStatusFunc == nullptr) {
            dsGetIgnoreEDIDStatusFunc = (dsGetIgnoreEDIDStatus_t)resolve(RDK_DSHAL_NAME, "dsGetIgnoreEDIDStatus");
            if (dsGetIgnoreEDIDStatusFunc == nullptr) {
                DSLOG_INFO("dsGetIgnoreEDIDStatus not defined — optional symbol absent");
                ignoreEDID = false;
                return WPEFramework::Core::ERROR_NONE;
            }
            DSLOG_INFO("dsGetIgnoreEDIDStatus loaded");
        }

        dsError_t eError = dsGetIgnoreEDIDStatusFunc(static_cast<intptr_t>(handle), &ignoreEDID);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO("SUCCESS - ignoreEDID=%d", static_cast<int>(ignoreEDID));
        } else {
            DSLOG_ERR("dsGetIgnoreEDIDStatus failed with error: %d", eError);
            ignoreEDID = false;
        }
        return retCode;
    }

    uint32_t GetColorDepth(const int32_t handle, uint32_t& colorDepth) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetColorDepth_t)(intptr_t handle, unsigned int* color_depth);
        static dsGetColorDepth_t dsGetColorDepthFunc = 0;

        if (dsGetColorDepthFunc == 0) {
            dsGetColorDepthFunc = (dsGetColorDepth_t)resolve(RDK_DSHAL_NAME, "dsGetColorDepth");
            if (dsGetColorDepthFunc == 0) {
                DSLOG_ERR(" dsGetColorDepth_t(int, unsigned int*) is not defined");
            }
            else {
                DSLOG_INFO(" dsGetColorDepth_t(int, unsigned int*) is defined and loaded");
            }
        }

        if (dsGetColorDepthFunc != 0) {
            unsigned int dsColorDepth = 0;
            dsError_t eError = dsGetColorDepthFunc(handle, &dsColorDepth);
            if (eError == dsERR_NONE) {
                colorDepth = dsColorDepth;
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - colorDepth=%u", colorDepth);
            } else {
                DSLOG_ERR(" dsGetColorDepth failed with error: %d", eError);
                colorDepth = 0; // Default value on error
            }
        } else {
            DSLOG_ERR(" not able to load function dsGetColorDepthFunc:%p", dsGetColorDepthFunc);
            colorDepth = 0; // Default value
        }
        
        return retCode;
    }

    uint32_t SetVideoPortColorDepth(const int32_t handle, const uint32_t colorDepth) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, colorDepth=%u", handle, colorDepth);
        
        // Use dsSetPreferredColorDepth instead since dsSetVideoPortColorDepth may not exist
        dsDisplayColorDepth_t dsColorDepth = static_cast<dsDisplayColorDepth_t>(colorDepth);
        dsError_t eError = dsSetPreferredColorDepth(handle, dsColorDepth);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS (via dsSetPreferredColorDepth)");
        } else {
            DSLOG_ERR(" dsSetPreferredColorDepth failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange& quantizationRange) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetQuantizationRange_t)(intptr_t handle, dsDisplayQuantizationRange_t* quantization_range);
        static dsGetQuantizationRange_t dsGetQuantizationRangeFunc = 0;

        if (dsGetQuantizationRangeFunc == 0) {
            dsGetQuantizationRangeFunc = (dsGetQuantizationRange_t)resolve(RDK_DSHAL_NAME, "dsGetQuantizationRange");
            if(dsGetQuantizationRangeFunc == 0) {
                DSLOG_ERR("dsGetQuantizationRange is not defined");
            }
            else {
                DSLOG_INFO("dsGetQuantizationRange loaded");
            }
        }

        if (dsGetQuantizationRangeFunc != 0) {
            dsDisplayQuantizationRange_t dsQuantizationRange;
            dsError_t eError = dsGetQuantizationRangeFunc(handle, &dsQuantizationRange);
            if (eError == dsERR_NONE) {
                quantizationRange = convertQuantizationRange(dsQuantizationRange);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" dsGetQuantizationRange failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetQuantizationRange function not available");
            quantizationRange = static_cast<VideoPortQuantizationRange>(dsDISPLAY_QUANTIZATIONRANGE_UNKNOWN);
        }
        
        return retCode;
    }

    uint32_t SetVideoPortQuantizationRange(const int32_t handle, const VideoPortQuantizationRange quantizationRange) override
    {
        // dsVideoPort.c has no dsSetQuantizationRange; quantization range is a read-only sink attribute
        DSLOG_WARN(" not supported by DS HAL (read-only sink property)");
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetColorSpace(const int32_t handle, VideoPortColorSpace& colorSpace) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetColorSpace_t)(intptr_t handle, dsDisplayColorSpace_t* color_space);
        static dsGetColorSpace_t dsGetColorSpaceFunc = 0;

        if (dsGetColorSpaceFunc == 0) {
            dsGetColorSpaceFunc = (dsGetColorSpace_t)resolve(RDK_DSHAL_NAME, "dsGetColorSpace");
            if(dsGetColorSpaceFunc == 0) {
                DSLOG_ERR("dsGetColorSpace is not defined");
            }
            else {
                DSLOG_INFO("dsGetColorSpace loaded");
            }
        }

        if (dsGetColorSpaceFunc != 0) {
            dsDisplayColorSpace_t dsColorSpace;
            dsError_t eError = dsGetColorSpaceFunc(handle, &dsColorSpace);
            if (eError == dsERR_NONE) {
                colorSpace = static_cast<VideoPortColorSpace>(dsColorSpace);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - colorSpace=%d", static_cast<int>(colorSpace));
            } else {
                DSLOG_ERR(" dsGetColorSpace failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetColorSpace function not available");
            colorSpace = static_cast<VideoPortColorSpace>(dsDISPLAY_COLORSPACE_RGB); // Default fallback
        }
        
        return retCode;
    }

    uint32_t SetColorSpace(const int32_t handle, const VideoPortColorSpace colorSpace) override
    {
        // dsVideoPort.c has no dsSetColorSpace; color space is a read-only EDID-negotiated property
        DSLOG_WARN(" not supported by DS HAL (read-only sink property)");
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetVideoPortFrameRate(const int32_t handle, uint32_t& frameRate) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);

        // No standalone dsGetFrameRate API; frame rate is embedded in the resolution name (e.g. "1080p60", "2160p30")
        dsVideoPortResolution_t dsResolution;
        dsError_t eError = dsGetResolution(handle, &dsResolution);
        if (eError == dsERR_NONE) {
            const auto* info = hal::video::getFrameRateInfo(dsResolution.frameRate);
            frameRate = (info && info->nominalFrameRate <= 60) ? info->nominalFrameRate : 60;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - frameRate=%u (from resolution %s)", frameRate, dsResolution.name);
        } else {
            DSLOG_ERR(" dsGetResolution failed: %d", eError);
            frameRate = 60;
        }

        return retCode;
    }

    uint32_t SetVideoPortFrameRate(const int32_t handle, const uint32_t frameRate) override
    {
        // No standalone dsSetFrameRate API; frame rate is set via dsSetResolution as part of the resolution name
        DSLOG_WARN(" not a separate HAL operation — frame rate is implicit in SetVideoPortResolution");
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetVideoPortHDCPStatus(const int32_t handle, VideoPortHdcpStatus& hdcpStatus) override
    {
        DSLOG_INFO(" handle=%d", handle);
        hdcpStatus = convertHdcpStatus(static_cast<dsHdcpStatus_t>(cachedHdcpStatus().load()));
        DSLOG_INFO(" SUCCESS - cached status=%d", static_cast<int>(hdcpStatus));
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetHDCPProtocol_t)(intptr_t handle, dsHdcpProtocolVersion_t* protocolVersion);
        static dsGetHDCPProtocol_t dsGetHDCPProtocolFunc = 0;

        if (dsGetHDCPProtocolFunc == 0) {
            dsGetHDCPProtocolFunc = (dsGetHDCPProtocol_t)resolve(RDK_DSHAL_NAME, "dsGetHDCPProtocol");
            if(dsGetHDCPProtocolFunc == 0) {
                DSLOG_ERR("dsGetHDCPProtocol is not defined");
            }
            else {
                DSLOG_INFO("dsGetHDCPProtocol loaded");
            }
        }

        if (dsGetHDCPProtocolFunc != 0) {
            dsHdcpProtocolVersion_t dsHdcpVersion;
            dsError_t eError = dsGetHDCPProtocolFunc(handle, &dsHdcpVersion);
            if (eError == dsERR_NONE) {
                hdcpVersion = convertHdcpProtocolVersion(dsHdcpVersion);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" dsGetHDCPProtocol failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetHDCPProtocol function not available");
        }
        
        return retCode;
    }

    uint32_t GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetHDCPReceiverProtocol_t)(intptr_t handle, dsHdcpProtocolVersion_t* protocolVersion);
        static dsGetHDCPReceiverProtocol_t dsGetHDCPReceiverProtocolFunc = 0;

        if (dsGetHDCPReceiverProtocolFunc == 0) {
            dsGetHDCPReceiverProtocolFunc = (dsGetHDCPReceiverProtocol_t)resolve(RDK_DSHAL_NAME, "dsGetHDCPReceiverProtocol");
            if(dsGetHDCPReceiverProtocolFunc == 0) {
                DSLOG_ERR("dsGetHDCPReceiverProtocol is not defined");
            }
            else {
                DSLOG_INFO("dsGetHDCPReceiverProtocol loaded");
            }
        }

        if (dsGetHDCPReceiverProtocolFunc != 0) {
            dsHdcpProtocolVersion_t dsHdcpVersion;
            dsError_t eError = dsGetHDCPReceiverProtocolFunc(handle, &dsHdcpVersion);
            if (eError == dsERR_NONE) {
                hdcpVersion = convertHdcpProtocolVersion(dsHdcpVersion);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" dsGetHDCPReceiverProtocol failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetHDCPReceiverProtocol function not available");
        }
        
        return retCode;
    }

    uint32_t GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetHDCPCurrentProtocol_t)(intptr_t handle, dsHdcpProtocolVersion_t* protocolVersion);
        static dsGetHDCPCurrentProtocol_t dsGetHDCPCurrentProtocolFunc = 0;

        if (dsGetHDCPCurrentProtocolFunc == 0) {
            dsGetHDCPCurrentProtocolFunc = (dsGetHDCPCurrentProtocol_t)resolve(RDK_DSHAL_NAME, "dsGetHDCPCurrentProtocol");
            if(dsGetHDCPCurrentProtocolFunc == 0) {
                DSLOG_ERR("dsGetHDCPCurrentProtocol is not defined");
            }
            else {
                DSLOG_INFO("dsGetHDCPCurrentProtocol loaded");
            }
        }

        if (dsGetHDCPCurrentProtocolFunc != 0) {
            dsHdcpProtocolVersion_t dsHdcpVersion;
            dsError_t eError = dsGetHDCPCurrentProtocolFunc(handle, &dsHdcpVersion);
            if (eError == dsERR_NONE) {
                hdcpVersion = convertHdcpProtocolVersion(dsHdcpVersion);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" dsGetHDCPCurrentProtocol failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetHDCPCurrentProtocol function not available");
        }
        
        return retCode;
    }

    uint32_t GetVideoEOTF(const int32_t handle, HDRStandard& hdrStandard) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetVideoEOTF_t)(intptr_t handle, dsHDRStandard_t* video_eotf);
        static dsGetVideoEOTF_t dsGetVideoEOTFFunc = 0;

        if (dsGetVideoEOTFFunc == 0) {
            dsGetVideoEOTFFunc = (dsGetVideoEOTF_t)resolve(RDK_DSHAL_NAME, "dsGetVideoEOTF");
            if(dsGetVideoEOTFFunc == 0) {
                DSLOG_ERR("dsGetVideoEOTF is not defined");
            }
            else {
                DSLOG_INFO("dsGetVideoEOTF loaded");
            }
        }

        if (dsGetVideoEOTFFunc != 0) {
            dsHDRStandard_t dsVideoEotf;
            dsError_t eError = dsGetVideoEOTFFunc(handle, &dsVideoEotf);
            if (eError == dsERR_NONE) {
                hdrStandard = static_cast<HDRStandard>(dsVideoEotf);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - hdrStandard=%d", static_cast<int>(hdrStandard));
            } else {
                DSLOG_ERR(" dsGetVideoEOTF failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetVideoEOTF function not available");
            hdrStandard = static_cast<HDRStandard>(dsHDRSTANDARD_NONE);
        }
        
        return retCode;
    }

    uint32_t GetMatrixCoefficients(const int32_t handle, DisplayMatrixCoefficients& matrixCoefficients) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetMatrixCoefficients_t)(intptr_t handle, dsDisplayMatrixCoefficients_t* matrix_coefficients);
        static dsGetMatrixCoefficients_t dsGetMatrixCoefficientsFunc = 0;

        if (dsGetMatrixCoefficientsFunc == 0) {
            dsGetMatrixCoefficientsFunc = (dsGetMatrixCoefficients_t)resolve(RDK_DSHAL_NAME, "dsGetMatrixCoefficients");
            if(dsGetMatrixCoefficientsFunc == 0) {
                DSLOG_ERR("dsGetMatrixCoefficients is not defined");
            }
            else {
                DSLOG_INFO("dsGetMatrixCoefficients loaded");
            }
        }

        if (dsGetMatrixCoefficientsFunc != 0) {
            dsDisplayMatrixCoefficients_t dsMatrixCoefficients;
            dsError_t eError = dsGetMatrixCoefficientsFunc(handle, &dsMatrixCoefficients);
            if (eError == dsERR_NONE) {
                matrixCoefficients = static_cast<DisplayMatrixCoefficients>(dsMatrixCoefficients);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - matrixCoefficients=%d", static_cast<int>(matrixCoefficients));
            } else {
                DSLOG_ERR(" dsGetMatrixCoefficients failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetMatrixCoefficients function not available");
            matrixCoefficients = static_cast<DisplayMatrixCoefficients>(dsDISPLAY_MATRIXCOEFFICIENT_UNKNOWN);
        }
        
        return retCode;
    }

    uint32_t IsVideoPortDisplaySurround(const int32_t handle, bool& surround) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsIsDisplaySurround_t)(intptr_t handle, bool *surround);
        static dsIsDisplaySurround_t dsIsDisplaySurroundFunc = 0;

        if (dsIsDisplaySurroundFunc == 0) {
            dsIsDisplaySurroundFunc = (dsIsDisplaySurround_t)resolve(RDK_DSHAL_NAME, "dsIsDisplaySurround");
            if(dsIsDisplaySurroundFunc == 0) {
                DSLOG_ERR("dsIsDisplaySurround is not defined");
            }
            else {
                DSLOG_INFO("dsIsDisplaySurround loaded");
            }
        }

        if (dsIsDisplaySurroundFunc != 0) {
            bool dsSurround = false;
            dsError_t eError = dsIsDisplaySurroundFunc(handle, &dsSurround);
            if (eError == dsERR_NONE) {
                surround = dsSurround;
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - surround=%s", surround ? "true" : "false");
            } else {
                DSLOG_ERR(" dsIsDisplaySurround failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsIsDisplaySurround function not available");
            surround = false;
        }
        
        return retCode;
    }

    uint32_t GetVideoPortDisplaySurroundMode(const int32_t handle, VideoPortSurroundMode& surroundMode) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetSurroundMode_t)(intptr_t handle, int *surround);
        static dsGetSurroundMode_t dsGetSurroundModeFunc = 0;

        if (dsGetSurroundModeFunc == 0) {
            dsGetSurroundModeFunc = (dsGetSurroundMode_t)resolve(RDK_DSHAL_NAME, "dsGetSurroundMode");
            if(dsGetSurroundModeFunc == 0) {
                DSLOG_ERR("dsGetSurroundMode is not defined");
            }
            else {
                DSLOG_INFO("dsGetSurroundMode loaded");
            }
        }

        if (dsGetSurroundModeFunc != 0) {
            int dsSurroundMode = 0;
            dsError_t eError = dsGetSurroundModeFunc(handle, &dsSurroundMode);
            if (eError == dsERR_NONE) {
                surroundMode = static_cast<VideoPortSurroundMode>(dsSurroundMode);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - surroundMode=%d", static_cast<int>(surroundMode));
            } else {
                DSLOG_ERR(" dsGetSurroundMode failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetSurroundMode function not available");
            surroundMode = VideoPortSurroundMode::DS_VIDEO_PORT_SURROUNDMODE_NONE;
        }
        
        return retCode;
    }

    uint32_t GetCurrentOutputSettings(const int32_t handle, DSOutputSettings& outputSettings) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetCurrentOutputSettings_t)(intptr_t handle, dsHDRStandard_t* video_eotf, dsDisplayMatrixCoefficients_t* matrix_coefficients, dsDisplayColorSpace_t* color_space, unsigned int* color_depth, dsDisplayQuantizationRange_t* quantization_range);
        static dsGetCurrentOutputSettings_t dsGetCurrentOutputSettingsFunc = 0;

        if (dsGetCurrentOutputSettingsFunc == 0) {
            dsGetCurrentOutputSettingsFunc = (dsGetCurrentOutputSettings_t)resolve(RDK_DSHAL_NAME, "dsGetCurrentOutputSettings");
            if(dsGetCurrentOutputSettingsFunc == 0) {
                DSLOG_ERR("dsGetCurrentOutputSettings is not defined");
            }
            else {
                DSLOG_INFO("dsGetCurrentOutputSettings loaded");
            }
        }

        if (dsGetCurrentOutputSettingsFunc != 0) {
            dsHDRStandard_t dsVideoEotf;
            dsDisplayMatrixCoefficients_t dsMatrixCoefficients;
            dsDisplayColorSpace_t dsColorSpace;
            unsigned int dsColorDepth;
            dsDisplayQuantizationRange_t dsQuantizationRange;
            
            dsError_t eError = dsGetCurrentOutputSettingsFunc(handle, &dsVideoEotf, &dsMatrixCoefficients, &dsColorSpace, &dsColorDepth, &dsQuantizationRange);
            if (eError == dsERR_NONE) {
                outputSettings.videoEotf = static_cast<HDRStandard>(dsVideoEotf);
                outputSettings.matrixCoefficients = static_cast<DisplayMatrixCoefficients>(dsMatrixCoefficients);
                outputSettings.colorDepth = static_cast<uint32_t>(dsColorDepth);
                outputSettings.colorSpace = static_cast<VideoPortColorSpace>(dsColorSpace);
                outputSettings.quantizationRange = static_cast<VideoPortQuantizationRange>(dsQuantizationRange);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - eotf=%d, matrix=%d, colorDepth=%u, colorSpace=%d, quantization=%d",
                       static_cast<int>(outputSettings.videoEotf), static_cast<int>(outputSettings.matrixCoefficients),
                       outputSettings.colorDepth, static_cast<int>(outputSettings.colorSpace), static_cast<int>(outputSettings.quantizationRange));
            } else {
                DSLOG_ERR(" dsGetCurrentOutputSettings failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetCurrentOutputSettings function not available");
            // Set default values
            outputSettings.videoEotf = static_cast<HDRStandard>(dsHDRSTANDARD_NONE);
            outputSettings.matrixCoefficients = static_cast<DisplayMatrixCoefficients>(dsDISPLAY_MATRIXCOEFFICIENT_UNKNOWN);
            outputSettings.colorDepth = 0;
            outputSettings.colorSpace = static_cast<VideoPortColorSpace>(dsDISPLAY_COLORSPACE_UNKNOWN);
            outputSettings.quantizationRange = static_cast<VideoPortQuantizationRange>(dsDISPLAY_QUANTIZATIONRANGE_UNKNOWN);
        }
        
        return retCode;
    }

    uint32_t GetPreferredColorDepth(const int32_t handle, DisplayColorDepth& colorDepth, const bool persist) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, persist=%s", handle, persist ? "true" : "false");
        
        if (persist) {
            // Use persistent color depth - following dsVideoPort.c pattern
            DisplayColorDepth persistentColorDepth = getPersistentColorDepth();
            colorDepth = persistentColorDepth;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS (from persistence) - colorDepth=%d", static_cast<int>(colorDepth));
        } else {
            // Get from HAL
            typedef dsError_t (*dsGetPreferredColorDepth_t)(intptr_t handle, dsDisplayColorDepth_t *colorDepth);
            static dsGetPreferredColorDepth_t dsGetPreferredColorDepthFunc = 0;

            if (dsGetPreferredColorDepthFunc == 0) {
                dsGetPreferredColorDepthFunc = (dsGetPreferredColorDepth_t)resolve(RDK_DSHAL_NAME, "dsGetPreferredColorDepth");
                if(dsGetPreferredColorDepthFunc == 0) {
                    DSLOG_ERR("dsGetPreferredColorDepth is not defined");
                }
                else {
                    DSLOG_INFO("dsGetPreferredColorDepth loaded");
                }
            }

            if (dsGetPreferredColorDepthFunc != 0) {
                dsDisplayColorDepth_t dsColorDepth;
                dsError_t eError = dsGetPreferredColorDepthFunc(handle, &dsColorDepth);
                if (eError == dsERR_NONE) {
                    colorDepth = static_cast<DisplayColorDepth>(dsColorDepth);
                    retCode = WPEFramework::Core::ERROR_NONE;
                    DSLOG_INFO(" SUCCESS (from HAL) - colorDepth=%d", static_cast<int>(colorDepth));
                } else {
                    DSLOG_ERR(" dsGetPreferredColorDepth failed with error: %d", eError);
                }
            } else {
                DSLOG_ERR(" dsGetPreferredColorDepth function not available");
                colorDepth = static_cast<DisplayColorDepth>(dsDISPLAY_COLORDEPTH_UNKNOWN);
            }
        }
        
        return retCode;
    }

    uint32_t SetPreferredColorDepth(const int32_t handle, const DisplayColorDepth colorDepth, const bool persist) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, colorDepth=%d, persist=%s", handle, static_cast<int>(colorDepth), persist ? "true" : "false");

        // dsVideoPort.c setPreferredColorDepth: ignore the request entirely if the port isn't connected.
        bool isConnected = false;
        dsError_t connectedError = dsIsDisplayConnected(static_cast<intptr_t>(handle), &isConnected);
        if (connectedError == dsERR_NONE && !isConnected) {
            DSLOG_INFO(" port not connected, ignoring set color depth request");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        const dsDisplayColorDepth_t requestedColorDepth = static_cast<dsDisplayColorDepth_t>(colorDepth);

        // dsVideoPort.c: if the requested depth already matches the platform's current depth, skip the HAL set call.
        dsDisplayColorDepth_t platformColorDepth = dsDISPLAY_COLORDEPTH_UNKNOWN;
        if (getCurrentPreferredColorDepth(static_cast<intptr_t>(handle), &platformColorDepth) == dsERR_NONE &&
            requestedColorDepth == platformColorDepth) {
            DSLOG_INFO(" Same color depth requested, skipping HAL set");
            retCode = WPEFramework::Core::ERROR_NONE;
            if (persist) {
                try {
                    device::HostPersistence::getInstance().persistHostProperty("HDMI0.colorDepth", std::to_string(static_cast<int>(colorDepth)));
                } catch(...) {
                    DSLOG_ERR("Failed to persist color depth setting");
                }
            }
            return retCode;
        }

        // dsVideoPort.c: negotiate the requested depth against sink EDID capabilities before setting.
        const dsDisplayColorDepth_t colorDepthToSet = getBestSupportedColorDepth(static_cast<intptr_t>(handle), requestedColorDepth);
        dsError_t eError = setPreferredColorDepthHAL(static_cast<intptr_t>(handle), colorDepthToSet);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - negotiated colorDepth=0x%x (requested=0x%x)", colorDepthToSet, requestedColorDepth);
            
            // Persist the originally-requested color depth (not the negotiated value) — matches dsVideoPort.c
            if (persist) {
                try {
                    std::string colorDepthStr = std::to_string(static_cast<int>(colorDepth));
                    device::HostPersistence::getInstance().persistHostProperty("HDMI0.colorDepth", colorDepthStr);
                    DSLOG_INFO("Color depth persisted: %s", colorDepthStr.c_str());
                } catch(...) {
                    DSLOG_ERR("Failed to persist color depth setting");
                }
            }
        } else {
            DSLOG_ERR(" dsSetPreferredColorDepth failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t SetVideoPortResolution(const int32_t handle, const VideoPortResolution resolution, const bool persist, const bool forceCompatibility) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, persist=%s, forceCompatibility=%s", handle, persist ? "true" : "false", forceCompatibility ? "true" : "false");

        dsVideoPortType_t portType = dsVIDEOPORT_TYPE_MAX;
        if (!resolvePortTypeByHandle(handle, portType)) {
            DSLOG_ERR(" unable to resolve video port type for handle=%d", handle);
            return retCode;
        }

        bool isConnected = false;
        dsError_t connectedError = dsIsDisplayConnected(handle, &isConnected);
        if (connectedError == dsERR_NONE && !isConnected) {
            DSLOG_INFO(" Port type=%d not connected, ignoring resolution request", static_cast<int>(portType));
            return WPEFramework::Core::ERROR_GENERAL;
        }

        bool forceDisable4K = false;
        dsError_t force4KError = dsGetForceDisable4KSupport(handle, &forceDisable4K);
        if (force4KError == dsERR_NONE && forceDisable4K) {
            if (resolution.name.find("2160") != std::string::npos) {
                DSLOG_INFO(" Cannot set 4K resolution while force-disable-4K is enabled");
                return WPEFramework::Core::ERROR_GENERAL;
            }
        }

        bool ignoreEDID = false;
        getIgnoreEDIDStatus(handle, ignoreEDID);
        DSLOG_INFO(" ResOverride SetVideoPortResolution ignoreEDID=%d", static_cast<int>(ignoreEDID));

        dsVideoPortResolution_t dsResolution = convertVideoPortResolution(resolution);

        dsVideoPortResolution_t platformResolution;
        memset(&platformResolution, 0, sizeof(platformResolution));
        dsError_t platformResolutionError = dsGetResolution(handle, &platformResolution);
        if (platformResolutionError == dsERR_NONE) {
            DSLOG_INFO(" Requested resolution=%s, platform resolution=%s", dsResolution.name, platformResolution.name);
            if (strcmp(dsResolution.name, platformResolution.name) == 0) {
                updateCachedResolutionForPort(portType, platformResolution.name);
                if (persist) {
                    persistVideoPortResolution(handle, platformResolution, forceCompatibility);
                }
                DSLOG_INFO(" Same resolution requested, skipping dsSetResolution");
                return WPEFramework::Core::ERROR_NONE;
            }
        }

        // Trigger resolution pre-change callback
        VideoPortPreResolutionChange(&dsResolution);

        dsError_t eError = dsSetResolution(handle, &dsResolution);
        // Legacy ordering calls post-change notification immediately after dsSetResolution call.
        VideoPortPostResolutionChange(&dsResolution);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS");
            updateCachedResolutionForPort(portType, dsResolution.name);
            
            // Persist resolution setting if requested - following dsVideoPort.c pattern
            if (persist) {
                persistVideoPortResolution(handle, dsResolution, forceCompatibility);
            }
        } else {
            DSLOG_ERR(" dsSetResolution failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t* hdcpKey, const uint16_t hdcpKeySize) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, hdcpEnable=%s, hdcpKeySize=%u", handle, hdcpEnable ? "true" : "false", hdcpKeySize);
        
        dsError_t eError = dsEnableHDCP(handle, hdcpEnable, (char*)hdcpKey, static_cast<int>(hdcpKeySize));
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS");
        } else {
            DSLOG_ERR(" dsEnableHDCP failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t IsHDCPEnabledOnVideoPort(const int32_t handle, bool& hdcpEnabled) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        bool dsHdcpEnabled = false;
        dsError_t eError = dsIsHDCPEnabled(handle, &dsHdcpEnabled);
        if (eError == dsERR_NONE) {
            hdcpEnabled = dsHdcpEnabled;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - hdcpEnabled=%s", hdcpEnabled ? "true" : "false");
        } else {
            DSLOG_ERR(" dsIsHDCPEnabled failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t GetTVHDRCapabilities(const int32_t handle, int32_t& capabilities) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetTVHDRCapabilitiesFunc_t)(intptr_t handle, int* capabilities);
        static dsGetTVHDRCapabilitiesFunc_t dsGetTVHDRCapabilitiesFunc = 0;

        if (dsGetTVHDRCapabilitiesFunc == 0) {
            dsGetTVHDRCapabilitiesFunc = (dsGetTVHDRCapabilitiesFunc_t)resolve(RDK_DSHAL_NAME, "dsGetTVHDRCapabilities");
            if(dsGetTVHDRCapabilitiesFunc == 0) {
                DSLOG_ERR("dsGetTVHDRCapabilities is not defined");
            }
            else {
                DSLOG_INFO("dsGetTVHDRCapabilities loaded");
            }
        }

        if (dsGetTVHDRCapabilitiesFunc != 0) {
            int dsCapabilities = 0;
            dsError_t eError = dsGetTVHDRCapabilitiesFunc(handle, &dsCapabilities);
            if (eError == dsERR_NONE) {
                capabilities = static_cast<int32_t>(dsCapabilities);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - capabilities=0x%x", capabilities);
            } else {
                DSLOG_ERR(" dsGetTVHDRCapabilities failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetTVHDRCapabilities function not available");
            capabilities = 0; // Default value
        }
        
        return retCode;
    }

    uint32_t GetTVSupportedResolutions(const int32_t handle, int32_t& resolutions) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsSupportedTvResolutionsFunc_t)(intptr_t handle, int* resolutions);
        static dsSupportedTvResolutionsFunc_t dsSupportedTvResolutionsFunc = 0;

        if (dsSupportedTvResolutionsFunc == 0) {
            dsSupportedTvResolutionsFunc = (dsSupportedTvResolutionsFunc_t)resolve(RDK_DSHAL_NAME, "dsSupportedTvResolutions");
            if(dsSupportedTvResolutionsFunc == 0) {
                DSLOG_ERR("dsSupportedTvResolutions is not defined");
            }
            else {
                DSLOG_INFO("dsSupportedTvResolutions loaded");
            }
        }

        if (dsSupportedTvResolutionsFunc != 0) {
            int dsResolutions = 0;
            dsError_t eError = dsSupportedTvResolutionsFunc(handle, &dsResolutions);
            if (eError == dsERR_NONE) {
                resolutions = static_cast<int32_t>(dsResolutions);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - resolutions=0x%x", resolutions);
            } else {
                DSLOG_ERR(" dsSupportedTvResolutions failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsSupportedTvResolutions function not available");
            resolutions = 0; // Default value
        }
        
        return retCode;
    }

    uint32_t SetForceDisable4K(const int32_t handle, const bool disable) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, disable=%s", handle, disable ? "true" : "false");
        
        dsError_t eError = dsSetForceDisable4KSupport(handle, disable);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS");
            /* Persist 4K disable state — matches dsVideoPort.c _dsSetForceDisable4K() */
            try {
                device::HostPersistence::getInstance().persistHostProperty(
                    "VideoDevice.force4KDisabled", disable ? "true" : "false");
                DSLOG_INFO(" persisted VideoDevice.force4KDisabled=%s",
                        disable ? "true" : "false");
            } catch (...) {
                DSLOG_ERR(" failed to persist force4KDisabled");
            }
        } else {
            DSLOG_ERR(" dsSetForceDisable4KSupport failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t GetForceDisable4K(const int32_t handle, bool& disabled) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        // Use correct DS HAL function: dsGetForceDisable4KSupport
        bool dsDisabled = false;
        dsError_t eError = dsGetForceDisable4KSupport(handle, &dsDisabled);
        if (eError == dsERR_NONE) {
            disabled = dsDisabled;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - disabled=%s", disabled ? "true" : "false");
        } else {
            DSLOG_ERR(" dsGetForceDisable4KSupport failed with error: %d", eError);
            disabled = false; // Default value on error
        }
        
        return retCode;
    }

    uint32_t IsVideoPortOutputHDR(const int32_t handle, bool& isHDR) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsIsOutputHDR_t)(intptr_t handle, bool* isHDR);
        static dsIsOutputHDR_t dsIsOutputHDRFunc = 0;

        if (dsIsOutputHDRFunc == 0) {
            dsIsOutputHDRFunc = (dsIsOutputHDR_t)resolve(RDK_DSHAL_NAME, "dsIsOutputHDR");
            if(dsIsOutputHDRFunc == 0) {
                DSLOG_ERR("dsIsOutputHDR is not defined");
            }
            else {
                DSLOG_INFO("dsIsOutputHDR loaded");
            }
        }

        if (dsIsOutputHDRFunc != 0) {
            bool dsIsHDR = false;
            dsError_t eError = dsIsOutputHDRFunc(handle, &dsIsHDR);
            if (eError == dsERR_NONE) {
                isHDR = dsIsHDR;
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - isHDR=%s", isHDR ? "true" : "false");
            } else {
                DSLOG_ERR(" dsIsOutputHDR failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsIsOutputHDR function not available");
            isHDR = false; // Default value
        }
        
        return retCode;
    }

    uint32_t ResetVideoPortOutputToSDR() override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        
        typedef dsError_t (*dsResetOutputToSDR_t)(void);
        static dsResetOutputToSDR_t dsResetOutputToSDRFunc = 0;

        if (dsResetOutputToSDRFunc == 0) {
            dsResetOutputToSDRFunc = (dsResetOutputToSDR_t)resolve(RDK_DSHAL_NAME, "dsResetOutputToSDR");
            if(dsResetOutputToSDRFunc == 0) {
                DSLOG_ERR("dsResetOutputToSDR is not defined");
            }
            else {
                DSLOG_INFO("dsResetOutputToSDR loaded");
            }
        }

        if (dsResetOutputToSDRFunc != 0) {
            dsError_t eError = dsResetOutputToSDRFunc();
            if (eError == dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" dsResetOutputToSDR failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsResetOutputToSDR function not available");
        }
        
        return retCode;
    }

    uint32_t GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetHdmiPreference_t)(intptr_t handle, dsHdcpProtocolVersion_t* hdcpVersion);
        static dsGetHdmiPreference_t dsGetHdmiPreferenceFunc = 0;

        if (dsGetHdmiPreferenceFunc == 0) {
            dsGetHdmiPreferenceFunc = (dsGetHdmiPreference_t)resolve(RDK_DSHAL_NAME, "dsGetHdmiPreference");
            if(dsGetHdmiPreferenceFunc == 0) {
                DSLOG_ERR("dsGetHdmiPreference is not defined");
            }
            else {
                DSLOG_INFO("dsGetHdmiPreference loaded");
            }
        }

        if (dsGetHdmiPreferenceFunc != 0) {
            dsHdcpProtocolVersion_t dsHdcpVersion;
            dsError_t eError = dsGetHdmiPreferenceFunc(handle, &dsHdcpVersion);
            if (eError == dsERR_NONE) {
                hdcpVersion = convertHdcpProtocolVersion(dsHdcpVersion);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - hdcpVersion=%d", static_cast<int>(hdcpVersion));
            } else {
                DSLOG_ERR(" dsGetHdmiPreference failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsGetHdmiPreference function not available");
        }
        
        return retCode;
    }

    uint32_t SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, hdcpVersion=%d", handle, static_cast<int>(hdcpVersion));
        
        typedef dsError_t (*dsSetHdmiPreference_t)(intptr_t handle, dsHdcpProtocolVersion_t* hdcpVersion);
        static dsSetHdmiPreference_t dsSetHdmiPreferenceFunc = 0;

        if (dsSetHdmiPreferenceFunc == 0) {
            dsSetHdmiPreferenceFunc = (dsSetHdmiPreference_t)resolve(RDK_DSHAL_NAME, "dsSetHdmiPreference");
            if(dsSetHdmiPreferenceFunc == 0) {
                DSLOG_ERR("dsSetHdmiPreference is not defined");
            }
            else {
                DSLOG_INFO("dsSetHdmiPreference loaded");
            }
        }

        if (dsSetHdmiPreferenceFunc != 0) {
            dsHdcpProtocolVersion_t dsHdcpVersion = convertHdcpProtocolVersionToDSHal(hdcpVersion);
            dsError_t eError = dsSetHdmiPreferenceFunc(handle, &dsHdcpVersion);
            if (eError == dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" dsSetHdmiPreference failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsSetHdmiPreference function not available");
        }
        
        return retCode;
    }

    uint32_t SetBackgroundColor(const int32_t handle, const VideoBackgroundColor backgroundColor) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, backgroundColor=%d", handle, static_cast<int>(backgroundColor));
        
        dsVideoBackgroundColor_t dsBackgroundColor = static_cast<dsVideoBackgroundColor_t>(backgroundColor);
        dsError_t eError = dsSetBackgroundColor(handle, dsBackgroundColor);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS");
        } else {
            DSLOG_ERR(" dsSetBackgroundColor failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t SetForceHDRMode(const int32_t handle, const HDRStandard hdrMode) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, hdrMode=%d", handle, static_cast<int>(hdrMode));
        
        typedef dsError_t (*dsSetForceHDRMode_t)(intptr_t handle, dsHDRStandard_t hdrMode);
        static dsSetForceHDRMode_t dsSetForceHDRModeFunc = 0;

        if (dsSetForceHDRModeFunc == 0) {
            dsSetForceHDRModeFunc = (dsSetForceHDRMode_t)resolve(RDK_DSHAL_NAME, "dsSetForceHDRMode");
            if(dsSetForceHDRModeFunc == 0) {
                DSLOG_ERR("dsSetForceHDRMode is not defined");
            }
            else {
                DSLOG_INFO("dsSetForceHDRMode loaded");
            }
        }

        if (dsSetForceHDRModeFunc != 0) {
            dsHDRStandard_t dsHdrMode = static_cast<dsHDRStandard_t>(hdrMode);
            dsError_t eError = dsSetForceHDRModeFunc(handle, dsHdrMode);
            if (eError == dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else if (eError == dsERR_OPERATION_NOT_SUPPORTED) {
                DSLOG_WARN(" not supported on this platform");
            } else {
                DSLOG_ERR(" dsSetForceHDRMode failed with error: %d", eError);
            }
        } else {
            DSLOG_ERR(" dsSetForceHDRMode function not available");
        }
        
        return retCode;
    }

    uint32_t GetColorDepthCapabilities(const int32_t handle, uint32_t& colorDepthCapabilities) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsColorDepthCapabilities_t)(intptr_t handle, unsigned int* colorDepthCapability);
        static dsColorDepthCapabilities_t dsColorDepthCapabilitiesFunc = 0;

        if (dsColorDepthCapabilitiesFunc == 0) {
            dsColorDepthCapabilitiesFunc = (dsColorDepthCapabilities_t)resolve(RDK_DSHAL_NAME, "dsColorDepthCapabilities");
            if (dsColorDepthCapabilitiesFunc == 0) {
                DSLOG_ERR(" dsColorDepthCapabilities(intptr_t handle, unsigned int *colorDepthCapability ) is not defined");
            }
            else {
                DSLOG_INFO(" dsColorDepthCapabilities(intptr_t handle, unsigned int *colorDepthCapability ) is defined and loaded");
            }
        }

        if (dsColorDepthCapabilitiesFunc != 0) {
            unsigned int dsColorDepthCapabilities = 0;
            dsError_t eError = dsColorDepthCapabilitiesFunc(handle, &dsColorDepthCapabilities);
            if (eError == dsERR_NONE) {
                DSLOG_INFO(" dsColorDepthCapabilities returned:%d  colorDepthCapability: 0x%x",
                        eError, dsColorDepthCapabilities);
                
                // Add auto by default - consistent with _dsColorDepthCapabilities in dsVideoPort.c
                dsColorDepthCapabilities = (dsColorDepthCapabilities | dsDISPLAY_COLORDEPTH_AUTO);
                
                colorDepthCapabilities = static_cast<uint32_t>(dsColorDepthCapabilities);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - final colorDepthCapabilities=0x%x", colorDepthCapabilities);
            } else {
                DSLOG_ERR(" dsColorDepthCapabilities failed with error: %d", eError);
                colorDepthCapabilities = 0; // Default value on error
            }
        } else {
            DSLOG_ERR(" not able to load function dsColorDepthCapabilitiesFunc:%p", dsColorDepthCapabilitiesFunc);
            colorDepthCapabilities = 0; // Default value
        }
        
        return retCode;
    }

    // VideoPort Event Handling Infrastructure - following HdmiIn singleton pattern
    void setAllCallbacks(const CallbackBundle& bundle) override
    {
        ENTRY_LOG;
        DSLOG_INFO("Registering event callbacks with DS HAL");
        
        // Debug logging to diagnose condition failure
        DSLOG_INFO("VideoPort callback registration check: videoPort_isInitialized=%d, videoPort_isPlatInitialized=%d",
                videoPort_isInitialized, videoPort_isPlatInitialized);
        
        if (videoPort_isPlatInitialized && !videoPort_isInitialized) {
            DSLOG_INFO("VideoPort platform callback Initialization");
            
            // Register Resolution Pre/Post Change callbacks
            if (bundle.OnResolutionPreChange) {
                DSLOG_INFO("VideoPort Resolution PreChange Event Callback Registered");
                g_VideoPortResolutionPreChangeCallback = bundle.OnResolutionPreChange;
                // Resolution callbacks are handled manually during resolution setting
            }
            
            if (bundle.OnResolutionPostChange) {
                DSLOG_INFO("VideoPort Resolution PostChange Event Callback Registered");
                g_VideoPortResolutionPostChangeCallback = bundle.OnResolutionPostChange;
                // Resolution callbacks are handled manually during resolution setting
            }
            
            // Register HDCP Status Callback with DS HAL
            if (bundle.OnHDCPStatusChange) {
                DSLOG_INFO("VideoPort HDCP Status Change Event Callback Registered");
                g_VideoPortHDCPStatusChangeCallback = bundle.OnHDCPStatusChange;
                
                intptr_t handle = 0;
                dsError_t eReturn = dsGetVideoPort(dsVIDEOPORT_TYPE_HDMI, 0, &handle);
                if (dsERR_NONE != eReturn) {
                    eReturn = dsGetVideoPort(dsVIDEOPORT_TYPE_INTERNAL, 0, &handle);
                }
                
                if (dsERR_NONE == eReturn && handle != 0) {
                    DSLOG_INFO("Registering HDCP status callback with handle: %p", (void*)handle);
                    const dsError_t callbackError = dsRegisterHdcpStatusCallback(handle, VideoPortHDCPStatusCallback);
                    if (callbackError != dsERR_NONE) {
                        DSLOG_ERR("dsRegisterHdcpStatusCallback failed with error: %d", callbackError);
                    }
                    if (profileType == STB) {
                        char hdcpKey[HDCP_KEY_MAX_SIZE] = {0};
                        size_t keySize = 0;
                        dsError_t ret = dsEnableHDCP(handle, true, hdcpKey, keySize);
                        if (ret != dsERR_NONE) {
                            DSLOG_ERR("Failed to enable startup HDCP: error=%d", ret);
                        } else {
                            DSLOG_INFO("Setting HDCP done ...");
                        }
                    }
                } else {
                    DSLOG_ERR("Failed to get video port handle for HDCP callback registration");
                }
            }
            
            // Register Video Format Update Callback with DS HAL
            if (bundle.OnVideoFormatUpdate) {
                DSLOG_INFO("VideoPort Video Format Update Event Callback Registered");
                g_VideoPortVideoFormatUpdateCallback = bundle.OnVideoFormatUpdate;
                
                dsError_t eRet = VideoPortRegisterVideoFormatUpdateCB(VideoPortVideoFormatUpdateCallback);
                if (dsERR_NONE != eRet) {
                    DSLOG_ERR("VideoPortRegisterVideoFormatUpdateCB failed with error: %d", eRet);
                } else {
                    DSLOG_INFO("Video format update callback registered successfully");
                }
            }
            
            videoPort_isInitialized = 1;
            DSLOG_INFO("VideoPort platform callback Initialization done");
        } else {
            if (!videoPort_isPlatInitialized) {
                DSLOG_ERR("VideoPort callback registration FAILED: Platform not initialized (videoPort_isPlatInitialized=%d)",
                       videoPort_isPlatInitialized);
            }
            if (videoPort_isInitialized) {
                DSLOG_WARN("VideoPort callback registration SKIPPED: Callbacks already initialized (videoPort_isInitialized=%d)",
                        videoPort_isInitialized);
            }
        }
        
        EXIT_LOG;
    }

    void getPersistenceValue()
    {
        ENTRY_LOG;
        DSLOG_INFO("Loading persistence settings");
        
        try {
            // Match dsVideoPort.c pattern: TV profile (profileType=1) defaults to 2160p, STB to 1080p
            std::string defaultResolution = (profileType == 1) ? DS_VP_DEFAULT_RESOLUTION_2160P : DS_VP_DEFAULT_RESOLUTION_1080P;
            
            _dsHDMIResolution = device::HostPersistence::getInstance().getProperty("HDMI0.resolution", defaultResolution);
            DSLOG_INFO("Persistent HDMI resolution read: %s", _dsHDMIResolution.c_str());
            if (_dsHDMIResolution == "2160p") {
                char telemetryValue[128] = {0};
                snprintf(telemetryValue, sizeof(telemetryValue), "The Persistent HDMI resolution read is %s", _dsHDMIResolution.c_str());
                TELEMETRY_EVENT_STRING("SYS_INFO_4KResolution_split", telemetryValue);
            }
            
            #ifdef HAS_ONLY_COMPOSITE
                _dsCompResolution = device::HostPersistence::getInstance().getProperty("Baseband0.resolution", defaultResolution);
            #else
                _dsCompResolution = device::HostPersistence::getInstance().getProperty("COMPONENT0.resolution", defaultResolution);
            #endif
            DSLOG_INFO("Persistent Component/Composite resolution read: %s", _dsCompResolution.c_str());
            
            _dsRFResolution = device::HostPersistence::getInstance().getProperty("RF0.resolution", defaultResolution);
            DSLOG_INFO("Persistent RF resolution read: %s", _dsRFResolution.c_str());
            
            _dsBBResolution = device::HostPersistence::getInstance().getProperty("Baseband0.resolution", defaultResolution);
            DSLOG_INFO("Persistent BB resolution read: %s", _dsBBResolution.c_str());
            
            // Read 4K disable setting and apply to HAL — matches dsVideoPort.c getPersistenceValue()
            std::string force4KDisabled = "false";
            force4KDisabled = device::HostPersistence::getInstance().getProperty("VideoDevice.force4KDisabled", force4KDisabled);
            if (force4KDisabled.compare("true") == 0) {
                DSLOG_INFO("4K support is force disabled via persistence — applying to HAL");
                intptr_t hdmiHandle = 0;
                if (dsGetVideoPort(dsVIDEOPORT_TYPE_HDMI, 0, &hdmiHandle) == dsERR_NONE) {
                    dsSetForceDisable4KSupport(hdmiHandle, true);
                }
            }
            
        } catch(...) {
            DSLOG_ERR("Error reading persistence values for VideoPort");
        }
        
        EXIT_LOG;
    }

    static intptr_t dsGetDefaultPortHandle()
    {
        std::vector<VideoPortTypeConfig> videoPortTypes;
        std::vector<VideoPortPortConfig> videoPorts;
        DeviceSettingsHAL::PopulateVideoPortConfig(videoPortTypes, videoPorts);

        intptr_t handle = 0;
        for (const auto& port : videoPorts) {
            const dsVideoPortType_t type = convertVideoPortType(port.videoPortType);
            dsError_t error = dsGetVideoPort(type, port.videoPortIndex, &handle);
            if (type == dsVIDEOPORT_TYPE_HDMI || type == dsVIDEOPORT_TYPE_INTERNAL) {
                if (error != dsERR_NONE) {
                    DSLOG_ERR(" dsGetVideoPort failed for type=%d index=%d, error=%d",
                            type, port.videoPortIndex, error);
                    return 0;
                }
                return handle;
            }
        }

        DSLOG_ERR(" HDMI or internal port not found in HAL configuration");
        return handle;
    }

    // Static callback functions for DS HAL integration - following HdmiIn pattern
    static void VideoPortHDCPStatusCallback(intptr_t handle, dsHdcpStatus_t status)
    {
        DSLOG_INFO(" handle=%p, status=%d", (void*)handle, status);
        cachedHdcpStatus().store(status);
        
        // Convert DS HAL HDCP status to VideoPortHdcpStatus
        VideoPortHdcpStatus hdcpStatus;
        switch (status) {
            case dsHDCP_STATUS_UNPOWERED:
                hdcpStatus = VideoPortHdcpStatus::DS_HDCP_STATUS_UNPOWERED;
                break;
            case dsHDCP_STATUS_UNAUTHENTICATED:
                hdcpStatus = VideoPortHdcpStatus::DS_HDCP_STATUS_UNAUTHENTICATED;
                break;
            case dsHDCP_STATUS_AUTHENTICATED:
                hdcpStatus = VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATED;
                break;
            case dsHDCP_STATUS_AUTHENTICATIONFAILURE:
                hdcpStatus = VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATIONFAILURE;
                break;
            case dsHDCP_STATUS_INPROGRESS:
                hdcpStatus = VideoPortHdcpStatus::DS_HDCP_STATUS_INPROGRESS;
                break;
            case dsHDCP_STATUS_PORTDISABLED:
                hdcpStatus = VideoPortHdcpStatus::DS_HDCP_STATUS_PORTDISABLED;
                break;
            default:
                hdcpStatus = VideoPortHdcpStatus::DS_HDCP_STATUS_UNAUTHENTICATED;
                DSLOG_WARN(" unknown HDCP status %d, defaulting to unauthenticated", status);
                break;
        }

        _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPSTATUS, status);

        dsHdcpProtocolVersion_t protocolVersion = dsHDCP_VERSION_1X;
        if (status == dsHDCP_STATUS_AUTHENTICATED) {
            typedef dsError_t (*dsGetHDCPCurrentProtocol_t)(intptr_t, dsHdcpProtocolVersion_t*);
            static dsGetHDCPCurrentProtocol_t getCurrentProtocol = nullptr;
            if (getCurrentProtocol == nullptr) {
                getCurrentProtocol = reinterpret_cast<dsGetHDCPCurrentProtocol_t>(resolve(RDK_DSHAL_NAME, "dsGetHDCPCurrentProtocol"));
            }
            if (getCurrentProtocol != nullptr && getCurrentProtocol(dsGetDefaultPortHandle(), &protocolVersion) != dsERR_NONE) {
                protocolVersion = dsHDCP_VERSION_1X;
            }
        }
        _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPVERSION, protocolVersion);
        
        // Call the stored global callback if available
        if (g_VideoPortHDCPStatusChangeCallback) {
            g_VideoPortHDCPStatusChangeCallback(hdcpStatus);
        }
    }

    static void VideoPortVideoFormatUpdateCallback(dsHDRStandard_t videoFormat)
    {
        DSLOG_INFO(" videoFormat=%d", videoFormat);
        
        // Convert DS HAL HDR standard to HDRStandard
        HDRStandard hdrStandard;
        switch (videoFormat) {
            case dsHDRSTANDARD_NONE:   // 0 = no HDR signal / SDR
            case dsHDRSTANDARD_SDR:
                hdrStandard = HDRStandard::DS_HDRSTANDARD_SDR;
                break;
            case dsHDRSTANDARD_HDR10:
                hdrStandard = HDRStandard::DS_HDRSTANDARD_HDR10;
                break;
            case dsHDRSTANDARD_HDR10PLUS:
                hdrStandard = HDRStandard::DS_HDRSTANDARD_HDR10PLUS;
                break;
            case dsHDRSTANDARD_DolbyVision:
                hdrStandard = HDRStandard::DS_HDRSTANDARD_DOLBYVISION;
                break;
            default:
                hdrStandard = HDRStandard::DS_HDRSTANDARD_SDR;
                DSLOG_WARN("Unrecognised HDR standard %d, treating as SDR", videoFormat);
                break;
        }
        
        // Call the stored global callback if available
        if (g_VideoPortVideoFormatUpdateCallback) {
            g_VideoPortVideoFormatUpdateCallback(hdrStandard);
        }
    }

    // DS HAL Video Format Update Callback Registration
    static dsError_t VideoPortRegisterVideoFormatUpdateCB(dsVideoFormatUpdateCB_t cbFun)
    {
        dsError_t eRet = dsERR_GENERAL;
        DSLOG_INFO(" Registering video format callback");
        
        typedef dsError_t (*dsVideoFormatUpdateRegisterCB_t)(dsVideoFormatUpdateCB_t cbFunArg);
        static dsVideoFormatUpdateRegisterCB_t dsVideoFormatUpdateRegisterCBFunc = 0;
        
        if (dsVideoFormatUpdateRegisterCBFunc == 0) {
            void* dllib = dlopen(RDK_DSHAL_NAME, RTLD_LAZY);
            if (dllib) {
                dsVideoFormatUpdateRegisterCBFunc = (dsVideoFormatUpdateRegisterCB_t) dlsym(dllib, "dsVideoFormatUpdateRegisterCB");
                if (dsVideoFormatUpdateRegisterCBFunc == 0) {
                    DSLOG_ERR("dsVideoFormatUpdateRegisterCB is not defined: %s", dlerror());
                    eRet = dsERR_GENERAL;
                } else {
                    DSLOG_INFO("dsVideoFormatUpdateRegisterCB loaded successfully");
                }
                dlclose(dllib);
            } else {
                DSLOG_ERR("Failed to open RDK_DSHAL_NAME [%s]: %s", RDK_DSHAL_NAME, dlerror());
                eRet = dsERR_GENERAL;
            }
        }
        
        if (dsVideoFormatUpdateRegisterCBFunc != 0) {
            eRet = dsVideoFormatUpdateRegisterCBFunc(cbFun);
            if (dsERR_NONE == eRet) {
                DSLOG_INFO("Video format update callback registered successfully");
            } else {
                DSLOG_ERR("Failed to register video format callback: %d", eRet);
            }
        }
        
        return eRet;
    }

    static void populateResolutionDimensions(dsVideoResolution_t resolution, ResolutionChange& resolutionChange)
    {
        const auto* info = hal::video::getResolutionInfo(resolution);
        if (info) {
            resolutionChange.width = info->width;
            resolutionChange.height = info->height;
            return;
        }

        resolutionChange.width = 1280;
        resolutionChange.height = 720;
        DSLOG_ERR("Unknown pixel resolution: %d, defaulting to 720p", resolution);
    }

    // Resolution Change Helper Functions - Following dsVideoPort.c RPC server pattern
    static void VideoPortPreResolutionChange(dsVideoPortResolution_t* resolution)
    {
        if (!resolution) {
            DSLOG_ERR(" Invalid resolution parameter");
            return;
        }
        
        DSLOG_INFO(" pixelResolution=%d", resolution->pixelResolution);
        
        // Convert dsVideoPortResolution_t to ResolutionChange structure - based on dsVideoPort.c
        ResolutionChange resolutionChange;
        populateResolutionDimensions(resolution->pixelResolution, resolutionChange);
        
        // Call the stored global callback if available
        if (g_VideoPortResolutionPreChangeCallback) {
            g_VideoPortResolutionPreChangeCallback(resolutionChange);
        }
    }

    static void VideoPortPostResolutionChange(dsVideoPortResolution_t* resolution)
    {
        if (!resolution) {
            DSLOG_ERR(" Invalid resolution parameter");
            return;
        }

        DSLOG_INFO(" pixelResolution=%d", resolution->pixelResolution);

        ResolutionChange resolutionChange;
        populateResolutionDimensions(resolution->pixelResolution, resolutionChange);

        // Call the stored global callback if available
        if (g_VideoPortResolutionPostChangeCallback) {
            g_VideoPortResolutionPostChangeCallback(resolutionChange);
        }
    }

    static void convertDSResolutionToResolutionChange(dsVideoPortResolution_t* dsResolution, ResolutionChange& resolutionChange)
    {
        // Convert pixel resolution to width/height based on dsVideoPort.c pattern
        switch (dsResolution->pixelResolution) {
            case dsVIDEO_PIXELRES_720x480:
                resolutionChange.width = 720;
                resolutionChange.height = 480;
                break;
            case dsVIDEO_PIXELRES_720x576:
                resolutionChange.width = 720;
                resolutionChange.height = 576;
                break;
            case dsVIDEO_PIXELRES_1280x720:
                resolutionChange.width = 1280;
                resolutionChange.height = 720;
                break;
            case dsVIDEO_PIXELRES_1920x1080:
                resolutionChange.width = 1920;
                resolutionChange.height = 1080;
                break;
            case dsVIDEO_PIXELRES_3840x2160:
                resolutionChange.width = 3840;
                resolutionChange.height = 2160;
                break;
            case dsVIDEO_PIXELRES_4096x2160:
                resolutionChange.width = 4096;
                resolutionChange.height = 2160;
                break;
            default:
                resolutionChange.width = 1920;
                resolutionChange.height = 1080;
                DSLOG_ERR("Unknown pixel resolution: %d, defaulting to 1920x1080", dsResolution->pixelResolution);
                break;
        }
        
        // Note: ResolutionChange only has width/height members
        // Additional information like pixelResolution, frameRate, interlaced are not part of the interface
    }

private:

    
    // Helper methods for DS VideoPort HAL conversion
    static bool resolvePortTypeByHandle(const int32_t handle, dsVideoPortType_t& portType)
    {
        intptr_t halHandle = 0;

        if (dsGetVideoPort(dsVIDEOPORT_TYPE_HDMI, 0, &halHandle) == dsERR_NONE && static_cast<int32_t>(halHandle) == handle) {
            portType = dsVIDEOPORT_TYPE_HDMI;
            return true;
        }
        if (dsGetVideoPort(dsVIDEOPORT_TYPE_INTERNAL, 0, &halHandle) == dsERR_NONE && static_cast<int32_t>(halHandle) == handle) {
            portType = dsVIDEOPORT_TYPE_INTERNAL;
            return true;
        }
        if (dsGetVideoPort(dsVIDEOPORT_TYPE_COMPONENT, 0, &halHandle) == dsERR_NONE && static_cast<int32_t>(halHandle) == handle) {
            portType = dsVIDEOPORT_TYPE_COMPONENT;
            return true;
        }
        if (dsGetVideoPort(dsVIDEOPORT_TYPE_BB, 0, &halHandle) == dsERR_NONE && static_cast<int32_t>(halHandle) == handle) {
            portType = dsVIDEOPORT_TYPE_BB;
            return true;
        }
        if (dsGetVideoPort(dsVIDEOPORT_TYPE_RF, 0, &halHandle) == dsERR_NONE && static_cast<int32_t>(halHandle) == handle) {
            portType = dsVIDEOPORT_TYPE_RF;
            return true;
        }

        portType = dsVIDEOPORT_TYPE_MAX;
        return false;
    }

    static std::string defaultResolutionByProfile()
    {
        return (profileType == TV) ? DS_VP_DEFAULT_RESOLUTION_2160P : DS_VP_DEFAULT_RESOLUTION_1080P;
    }

    static std::string getCachedResolutionForPort(const dsVideoPortType_t portType)
    {
        switch (portType) {
            case dsVIDEOPORT_TYPE_HDMI:
            case dsVIDEOPORT_TYPE_INTERNAL:
                return _dsHDMIResolution.empty() ? defaultResolutionByProfile() : _dsHDMIResolution;
            case dsVIDEOPORT_TYPE_COMPONENT:
                return _dsCompResolution.empty() ? defaultResolutionByProfile() : _dsCompResolution;
            case dsVIDEOPORT_TYPE_BB:
                return _dsBBResolution.empty() ? DS_VP_DEFAULT_RESOLUTION : _dsBBResolution;
            case dsVIDEOPORT_TYPE_RF:
                return _dsRFResolution.empty() ? DS_VP_DEFAULT_RESOLUTION : _dsRFResolution;
            default:
                return defaultResolutionByProfile();
        }
    }

    static void updateCachedResolutionForPort(const dsVideoPortType_t portType, const std::string& resolutionName)
    {
        if (resolutionName.empty()) {
            return;
        }

        switch (portType) {
            case dsVIDEOPORT_TYPE_HDMI:
            case dsVIDEOPORT_TYPE_INTERNAL:
                _dsHDMIResolution = resolutionName;
                break;
            case dsVIDEOPORT_TYPE_COMPONENT:
                _dsCompResolution = resolutionName;
                break;
            case dsVIDEOPORT_TYPE_BB:
                _dsBBResolution = resolutionName;
                break;
            case dsVIDEOPORT_TYPE_RF:
                _dsRFResolution = resolutionName;
                break;
            default:
                break;
        }
    }

    static VideoPortResolution buildResolutionFromName(const std::string& resolutionName)
    {
        VideoPortResolution resolution;
        resolution.name = resolutionName;
        resolution.aspectRatio = VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9;
        resolution.stereoScopicMode = VideoStereoScopicMode::DS_VIDEO_SSMODE_2D;
        resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_60;
        resolution.interlaced = false;

        if (resolutionName.find("2160") != std::string::npos) {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_3840X2160;
            if (resolutionName.find("24") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_24;
            } else if (resolutionName.find("25") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_25;
            } else if (resolutionName.find("30") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_30;
            } else if (resolutionName.find("50") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_50;
            }
        } else if (resolutionName.find("1080i") != std::string::npos) {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_1920X1080;
            resolution.interlaced = true;
        } else if (resolutionName.find("1080") != std::string::npos) {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_1920X1080;
            if (resolutionName.find("24") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_24;
            } else if (resolutionName.find("25") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_25;
            } else if (resolutionName.find("30") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_30;
            } else if (resolutionName.find("50") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_50;
            }
        } else if (resolutionName.find("720") != std::string::npos) {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_1280X720;
            if (resolutionName.find("50") != std::string::npos) {
                resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_50;
            }
        } else if (resolutionName.find("576i") != std::string::npos) {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_720X576;
            resolution.interlaced = true;
            resolution.aspectRatio = VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_4X3;
            resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_50;
        } else if (resolutionName.find("576") != std::string::npos) {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_720X576;
            resolution.aspectRatio = VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_4X3;
            resolution.frameRate = VideoFrameRate::DS_VIDEO_FRAMERATE_50;
        } else if (resolutionName.find("480i") != std::string::npos) {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_720X480;
            resolution.interlaced = true;
            resolution.aspectRatio = VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_4X3;
        } else if (resolutionName.find("480") != std::string::npos) {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_720X480;
            resolution.aspectRatio = VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_4X3;
        } else {
            resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_1920X1080;
        }

        return resolution;
    }

    static dsVideoPortType_t convertVideoPortType(const VideoPortType videoPort)
    {
        switch (videoPort) {
            case VideoPortType::DS_VIDEO_PORT_TYPE_HDMI:
                return dsVIDEOPORT_TYPE_HDMI;
            case VideoPortType::DS_VIDEO_PORT_TYPE_COMPONENT:
                return dsVIDEOPORT_TYPE_COMPONENT;
            case VideoPortType::DS_VIDEO_PORT_TYPE_SVIDEO:
                return dsVIDEOPORT_TYPE_SVIDEO;
            case VideoPortType::DS_VIDEO_PORT_TYPE_1394:
                return dsVIDEOPORT_TYPE_1394;
            case VideoPortType::DS_VIDEO_PORT_TYPE_DVI:
                return dsVIDEOPORT_TYPE_DVI;
            case VideoPortType::DS_VIDEO_PORT_TYPE_INTERNAL:
                return dsVIDEOPORT_TYPE_INTERNAL;
            default:
                return dsVIDEOPORT_TYPE_HDMI;
        }
    }

    VideoPortType convertVideoPortType(const dsVideoPortType_t dsVideoPort)
    {
        switch (dsVideoPort) {
            case dsVIDEOPORT_TYPE_HDMI:
                return VideoPortType::DS_VIDEO_PORT_TYPE_HDMI;
            case dsVIDEOPORT_TYPE_COMPONENT:
                return VideoPortType::DS_VIDEO_PORT_TYPE_COMPONENT;
            case dsVIDEOPORT_TYPE_SVIDEO:
                return VideoPortType::DS_VIDEO_PORT_TYPE_SVIDEO;
            case dsVIDEOPORT_TYPE_1394:
                return VideoPortType::DS_VIDEO_PORT_TYPE_1394;
            case dsVIDEOPORT_TYPE_DVI:
                return VideoPortType::DS_VIDEO_PORT_TYPE_DVI;
            case dsVIDEOPORT_TYPE_INTERNAL:
                return VideoPortType::DS_VIDEO_PORT_TYPE_INTERNAL;
            default:
                return VideoPortType::DS_VIDEO_PORT_TYPE_HDMI;
        }
    }

    VideoPortResolution convertVideoPortResolution(const dsVideoPortResolution_t& dsResolution)
    {
        VideoPortResolution resolution;

        /* Use the name filled in by dsGetResolution() — this is exactly what
         * device::VideoOutputPort::getResolution().getName() returns in the
         * DS_IARM path (e.g. "1080i", "1080p", "720p", "2160p30").
         * Fall back to deriving the name from pixelResolution + interlaced only
         * when the HAL left the name field empty. */
        if (dsResolution.name[0] != '\0') {
            resolution.name = std::string(dsResolution.name);
        } else {
            switch (dsResolution.pixelResolution) {
                case dsVIDEO_PIXELRES_720x480:
                    resolution.name = std::string("480") + hal::video::getInterlacedStr(dsResolution.interlaced);
                    break;
                case dsVIDEO_PIXELRES_720x576:
                    resolution.name = std::string("576") + hal::video::getInterlacedStr(dsResolution.interlaced) + "50";
                    break;
                case dsVIDEO_PIXELRES_1280x720:
                    resolution.name = "720p";
                    break;
                case dsVIDEO_PIXELRES_1366x768:
                    resolution.name = "768p60";
                    break;
                case dsVIDEO_PIXELRES_1920x1080:
                    resolution.name = std::string("1080") + hal::video::getInterlacedStr(dsResolution.interlaced);
                    break;
                case dsVIDEO_PIXELRES_3840x2160:
                    resolution.name = "2160p60";
                    break;
                case dsVIDEO_PIXELRES_4096x2160:
                    resolution.name = "4096x2160";
                    break;
                default:
                    resolution.name = "1080p";
                    break;
            }
        }

        // Map DS pixel resolution to interface VideoResolution enum
        switch (dsResolution.pixelResolution) {
            case dsVIDEO_PIXELRES_720x480:
                resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_720X480;
                break;
            case dsVIDEO_PIXELRES_720x576:
                resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_720X576;
                break;
            case dsVIDEO_PIXELRES_1280x720:
                resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_1280X720;
                break;
            case dsVIDEO_PIXELRES_1920x1080:
                resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_1920X1080;
                break;
            case dsVIDEO_PIXELRES_3840x2160:
                resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_3840X2160;
                break;
            default:
                resolution.pixelResolution = VideoResolution::DS_VIDEO_PIXELRES_1920X1080;
                break;
        }

        // aspectRatio, stereoScopicMode, frameRate enums align between DS HAL and WPE interface
        resolution.aspectRatio      = static_cast<VideoAspectRatio>(dsResolution.aspectRatio);
        resolution.stereoScopicMode = static_cast<VideoStereoScopicMode>(dsResolution.stereoScopicMode);
        // DS HAL may have extra frameRate values (59fps=15, 23fps=16) beyond WPE MAX=15; clamp to UNKNOWN
        resolution.frameRate = (dsResolution.frameRate < dsVIDEO_FRAMERATE_MAX &&
                                static_cast<int>(dsResolution.frameRate) < static_cast<int>(VideoFrameRate::DS_VIDEO_FRAMERATE_MAX))
                               ? static_cast<VideoFrameRate>(dsResolution.frameRate)
                               : VideoFrameRate::DS_VIDEO_FRAMERATE_UNKNOWN;
        resolution.interlaced = dsResolution.interlaced;

        DSLOG_INFO(" name='%s', pixelRes=%d, frameRate=%d, interlaced=%d",
                resolution.name.c_str(), static_cast<int>(resolution.pixelResolution),
                static_cast<int>(resolution.frameRate), resolution.interlaced);
        return resolution;
    }

    dsVideoPortResolution_t convertVideoPortResolution(const VideoPortResolution& resolution)
    {
        dsVideoPortResolution_t dsResolution = {};

        strncpy(dsResolution.name, resolution.name.c_str(), sizeof(dsResolution.name) - 1);

        switch (resolution.pixelResolution) {
            case VideoResolution::DS_VIDEO_PIXELRES_720X480:   dsResolution.pixelResolution = dsVIDEO_PIXELRES_720x480;   break;
            case VideoResolution::DS_VIDEO_PIXELRES_720X576:   dsResolution.pixelResolution = dsVIDEO_PIXELRES_720x576;   break;
            case VideoResolution::DS_VIDEO_PIXELRES_1280X720:  dsResolution.pixelResolution = dsVIDEO_PIXELRES_1280x720;  break;
            case VideoResolution::DS_VIDEO_PIXELRES_1920X1080: dsResolution.pixelResolution = dsVIDEO_PIXELRES_1920x1080; break;
            case VideoResolution::DS_VIDEO_PIXELRES_3840X2160: dsResolution.pixelResolution = dsVIDEO_PIXELRES_3840x2160; break;
            default:                                            dsResolution.pixelResolution = dsVIDEO_PIXELRES_1920x1080; break;
        }

        // enum ordinals match between interface and DS HAL for these types
        dsResolution.aspectRatio      = static_cast<dsVideoAspectRatio_t>(resolution.aspectRatio);
        dsResolution.stereoScopicMode = static_cast<dsVideoStereoScopicMode_t>(resolution.stereoScopicMode);
        dsResolution.frameRate        = static_cast<dsVideoFrameRate_t>(resolution.frameRate);
        dsResolution.interlaced       = resolution.interlaced;

        return dsResolution;
    }

    // Convert DS HAL HDCP version to interface HDCP version
    VideoPortHdcpProtocolVersion convertHdcpProtocolVersion(const dsHdcpProtocolVersion_t dsHdcpVersion)
    {
        switch (dsHdcpVersion) {
            case dsHDCP_VERSION_1X:
                return VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X;
            case dsHDCP_VERSION_2X:
                return VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_2X;
            default:
                return VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X;
        }
    }

    // Convert interface HDCP version to DS HAL HDCP version
    dsHdcpProtocolVersion_t convertHdcpProtocolVersionToDSHal(const VideoPortHdcpProtocolVersion hdcpVersion)
    {
        switch (hdcpVersion) {
            case VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X:
                return dsHDCP_VERSION_1X;
            case VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_2X:
                return dsHDCP_VERSION_2X;
            default:
                return dsHDCP_VERSION_1X;
        }
    }

    dsDisplayColorSpace_t convertColorSpace(const VideoPortColorSpace colorSpace)
    {
        switch (colorSpace) {
            case VideoPortColorSpace::DS_DISPLAY_COLORSPACE_RGB:
                return dsDISPLAY_COLORSPACE_RGB;
            case VideoPortColorSpace::DS_DISPLAY_COLORSPACE_YCBCR422:
                return dsDISPLAY_COLORSPACE_YCbCr422;
            case VideoPortColorSpace::DS_DISPLAY_COLORSPACE_YCBCR444:
                return dsDISPLAY_COLORSPACE_YCbCr444;
            case VideoPortColorSpace::DS_DISPLAY_COLORSPACE_YCBCR420:
                return dsDISPLAY_COLORSPACE_YCbCr420;
            default:
                return dsDISPLAY_COLORSPACE_RGB;
        }
    }

    VideoPortColorSpace convertColorSpace(const dsDisplayColorSpace_t dsColorSpace)
    {
        switch (dsColorSpace) {
            case dsDISPLAY_COLORSPACE_RGB:
                return VideoPortColorSpace::DS_DISPLAY_COLORSPACE_RGB;
            case dsDISPLAY_COLORSPACE_YCbCr422:
                return VideoPortColorSpace::DS_DISPLAY_COLORSPACE_YCBCR422;
            case dsDISPLAY_COLORSPACE_YCbCr444:
                return VideoPortColorSpace::DS_DISPLAY_COLORSPACE_YCBCR444;
            case dsDISPLAY_COLORSPACE_YCbCr420:
                return VideoPortColorSpace::DS_DISPLAY_COLORSPACE_YCBCR420;
            default:
                return VideoPortColorSpace::DS_DISPLAY_COLORSPACE_RGB;
        }
    }

    dsDisplayQuantizationRange_t convertQuantizationRange(const VideoPortQuantizationRange quantizationRange)
    {
        switch (quantizationRange) {
            case VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_LIMITED:
                return dsDISPLAY_QUANTIZATIONRANGE_LIMITED;
            case VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_FULL:
                return dsDISPLAY_QUANTIZATIONRANGE_FULL;
            default:
                return dsDISPLAY_QUANTIZATIONRANGE_LIMITED;
        }
    }

    VideoPortQuantizationRange convertQuantizationRange(const dsDisplayQuantizationRange_t dsQuantizationRange)
    {
        switch (dsQuantizationRange) {
            case dsDISPLAY_QUANTIZATIONRANGE_LIMITED:
                return VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_LIMITED;
            case dsDISPLAY_QUANTIZATIONRANGE_FULL:
                return VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_FULL;
            default:
                return VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_LIMITED;
        }
    }

    VideoPortHdcpStatus convertHdcpStatus(const dsHdcpStatus_t& dsHdcpStatus)
    {
        switch (dsHdcpStatus) {
            case dsHDCP_STATUS_UNPOWERED:
                return VideoPortHdcpStatus::DS_HDCP_STATUS_UNPOWERED;
            case dsHDCP_STATUS_UNAUTHENTICATED:
                return VideoPortHdcpStatus::DS_HDCP_STATUS_UNAUTHENTICATED;
            case dsHDCP_STATUS_AUTHENTICATED:
                return VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATED;
            case dsHDCP_STATUS_AUTHENTICATIONFAILURE:
                return VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATIONFAILURE;
            case dsHDCP_STATUS_INPROGRESS:
                return VideoPortHdcpStatus::DS_HDCP_STATUS_INPROGRESS;
            case dsHDCP_STATUS_PORTDISABLED:
                return VideoPortHdcpStatus::DS_HDCP_STATUS_PORTDISABLED;
            default:
                return VideoPortHdcpStatus::DS_HDCP_STATUS_UNPOWERED;
        }
    }


    void persistVideoPortResolution(const int32_t handle, const dsVideoPortResolution_t& resolution, const bool forceCompatible)
    {
        DSLOG_INFO(" handle=%d, forceCompatible=%s", handle, forceCompatible ? "true" : "false");
        
        try {
            std::string resolutionName(resolution.name);
            
            dsVideoPortType_t portType = dsVIDEOPORT_TYPE_HDMI;
            intptr_t test_handle = 0;
            if (dsGetVideoPort(dsVIDEOPORT_TYPE_HDMI, 0, &test_handle) == dsERR_NONE && test_handle == handle) {
                portType = dsVIDEOPORT_TYPE_HDMI;
            } else if (dsGetVideoPort(dsVIDEOPORT_TYPE_COMPONENT, 0, &test_handle) == dsERR_NONE && test_handle == handle) {
                portType = dsVIDEOPORT_TYPE_COMPONENT;
            } else if (dsGetVideoPort(dsVIDEOPORT_TYPE_INTERNAL, 0, &test_handle) == dsERR_NONE && test_handle == handle) {
                portType = dsVIDEOPORT_TYPE_INTERNAL;
            } else if (dsGetVideoPort(dsVIDEOPORT_TYPE_BB, 0, &test_handle) == dsERR_NONE && test_handle == handle) {
                portType = dsVIDEOPORT_TYPE_BB;
            } else if (dsGetVideoPort(dsVIDEOPORT_TYPE_RF, 0, &test_handle) == dsERR_NONE && test_handle == handle) {
                portType = dsVIDEOPORT_TYPE_RF;
            }
            
            if (portType == dsVIDEOPORT_TYPE_HDMI || portType == dsVIDEOPORT_TYPE_INTERNAL) {
                device::HostPersistence::getInstance().persistHostProperty("HDMI0.resolution", resolutionName);
                DSLOG_INFO("Persisted HDMI resolution: %s", resolutionName.c_str());
                _dsHDMIResolution = resolutionName;
                
                /* dsVideoPort.c persistResolution: compatibility is ALWAYS checked (cache always
                 * updated); only the disk persistence of the cross-port resolution is gated by forceCompatible. */
                if (!IsCompatibleResolution(resolution.pixelResolution, getPixelResolutionByName(_dsCompResolution))) {
                    DSLOG_INFO("HDMI Resolution is not Compatible with Analog ports");
                    std::string compatibleResolution = getCompatibleAnalogResolution(resolution);
                    DSLOG_INFO("New Compatible resolution is %s", compatibleResolution.c_str());
                    _dsCompResolution = compatibleResolution;
                    if (forceCompatible) {
                        #ifdef HAS_ONLY_COMPOSITE
                            device::HostPersistence::getInstance().persistHostProperty("Baseband0.resolution", compatibleResolution);
                        #else
                            device::HostPersistence::getInstance().persistHostProperty("COMPONENT0.resolution", compatibleResolution);
                        #endif
                    }
                } else {
                    DSLOG_INFO("HDMI and Analog Ports Resolutions are Compatible");
                }
            } else if (portType == dsVIDEOPORT_TYPE_COMPONENT) {
                #ifdef HAS_ONLY_COMPOSITE
                    device::HostPersistence::getInstance().persistHostProperty("Baseband0.resolution", resolutionName);
                #else
                    device::HostPersistence::getInstance().persistHostProperty("COMPONENT0.resolution", resolutionName);
                #endif
                DSLOG_INFO("Persisted Component resolution: %s", resolutionName.c_str());
                _dsCompResolution = resolutionName;
                
                if (!IsCompatibleResolution(resolution.pixelResolution, getPixelResolutionByName(_dsHDMIResolution))) {
                    DSLOG_INFO("HDMI Resolution is not Compatible with Analog ports");
                    std::string compatibleResolution = getCompatibleHDMIResolution(resolution);
                    DSLOG_INFO("New Compatible resolution is %s", compatibleResolution.c_str());
                    _dsHDMIResolution = compatibleResolution;
                    if (forceCompatible) {
                        device::HostPersistence::getInstance().persistHostProperty("HDMI0.resolution", compatibleResolution);
                    }
                } else {
                    DSLOG_INFO("HDMI and Analog Ports Resolutions are Compatible");
                }
            } else if (portType == dsVIDEOPORT_TYPE_BB) {
                /* dsVideoPort.c: _dsSetResolution BB case persists Baseband0.resolution */
                device::HostPersistence::getInstance().persistHostProperty("Baseband0.resolution", resolutionName);
                DSLOG_INFO("Persisted Baseband resolution: %s", resolutionName.c_str());
                _dsBBResolution = resolutionName;

                /* dsVideoPort.c: BB/RF branches always update the HDMI cache on mismatch, never persist it. */
                if (!IsCompatibleResolution(resolution.pixelResolution, getPixelResolutionByName(_dsHDMIResolution))) {
                    std::string compatibleResolution = getCompatibleHDMIResolution(resolution);
                    DSLOG_INFO("New Compatible resolution is %s", compatibleResolution.c_str());
                    _dsHDMIResolution = compatibleResolution;
                }
            } else if (portType == dsVIDEOPORT_TYPE_RF) {
                /* dsVideoPort.c: _dsSetResolution RF case persists RF0.resolution */
                device::HostPersistence::getInstance().persistHostProperty("RF0.resolution", resolutionName);
                DSLOG_INFO("Persisted RF resolution: %s", resolutionName.c_str());
                _dsRFResolution = resolutionName;

                if (!IsCompatibleResolution(resolution.pixelResolution, getPixelResolutionByName(_dsHDMIResolution))) {
                    std::string compatibleResolution = getCompatibleHDMIResolution(resolution);
                    DSLOG_INFO("New Compatible resolution is %s", compatibleResolution.c_str());
                    _dsHDMIResolution = compatibleResolution;
                }
            }
            
        } catch(...) {
            DSLOG_ERR("Exception in persistVideoPortResolution");
        }
    }

    // dsVideoPort.c: IsHDCompatible(p) macro — (p) >= dsVIDEO_PIXELRES_1280x720 && (p) < dsVIDEO_PIXELRES_MAX
    static bool IsHDCompatible(const dsVideoResolution_t pixelResolution)
    {
        return pixelResolution >= dsVIDEO_PIXELRES_1280x720 && pixelResolution < dsVIDEO_PIXELRES_MAX;
    }

    // Mirrors dsVideoPort.c IsCompatibleResolution(): equal, or both HD-and-above.
    static bool IsCompatibleResolution(const dsVideoResolution_t pixelResolution1, const dsVideoResolution_t pixelResolution2)
    {
        return (pixelResolution1 == pixelResolution2) || (IsHDCompatible(pixelResolution1) && IsHDCompatible(pixelResolution2));
    }

    // Mirrors dsVideoPort.c getPixelResolution(): resolve a cached resolution name to its pixelResolution enum.
    static dsVideoResolution_t getPixelResolutionByName(const std::string& resolutionName)
    {
        std::vector<VideoPortResolution> platformResolutions;
        DeviceSettingsHAL::PopulateVideoPortResolutionConfig(VideoPortType::DS_VIDEO_PORT_TYPE_HDMI, platformResolutions);
        for (const auto& res : platformResolutions) {
            if (res.name == resolutionName) {
                return static_cast<dsVideoResolution_t>(static_cast<int>(res.pixelResolution));
            }
        }
        return dsVIDEO_PIXELRES_MAX;
    }

    // Helper function to get compatible analog resolution - simplified from dsVideoPort.c
    std::string getCompatibleAnalogResolution(const dsVideoPortResolution_t& hdmiResolution)
    {
        // Simplified compatibility mapping based on dsVideoPort.c patterns
        switch(hdmiResolution.pixelResolution) {
            case dsVIDEO_PIXELRES_3840x2160:
            case dsVIDEO_PIXELRES_4096x2160:
                return "1080p"; // 4K -> 1080p for analog
            case dsVIDEO_PIXELRES_1920x1080:
                return "1080p";
            case dsVIDEO_PIXELRES_1280x720:
                return "720p";
            case dsVIDEO_PIXELRES_720x480:
                return "480p";
            case dsVIDEO_PIXELRES_720x576:
                return "576p";
            default:
                return "1080p"; // Default fallback
        }
    }

    // Helper function to get compatible HDMI resolution - simplified from dsVideoPort.c
    std::string getCompatibleHDMIResolution(const dsVideoPortResolution_t& analogResolution)
    {
        // For analog to HDMI, generally same resolution or upgrade
        switch(analogResolution.pixelResolution) {
            case dsVIDEO_PIXELRES_720x480:
                return "480p";  // Note: dsVideoPort.c converts 480i to 480p
            case dsVIDEO_PIXELRES_720x576:
                return "576p";
            case dsVIDEO_PIXELRES_1280x720:
                return "720p";
            case dsVIDEO_PIXELRES_1920x1080:
                return "1080p";
            default:
                return "1080p"; // Default fallback
        }
    }

    // Get persistent color depth - following dsVideoPort.c getPersistentColorDepth() pattern
    static DisplayColorDepth getPersistentColorDepth()
    {
        DisplayColorDepth defaultColorDepth = static_cast<DisplayColorDepth>(DEFAULT_COLOR_DEPTH);
        std::string colorDepthStr = std::to_string(static_cast<int>(defaultColorDepth));
        
        try {
            colorDepthStr = device::HostPersistence::getInstance().getProperty("HDMI0.colorDepth", colorDepthStr);
            int colorDepthValue = std::stoi(colorDepthStr);
            DisplayColorDepth persistentColorDepth = static_cast<DisplayColorDepth>(colorDepthValue);
            DSLOG_INFO("Reading HDMI persistent color depth: %d", colorDepthValue);
            return persistentColorDepth;
        } catch(...) {
            DSLOG_ERR("Reading HDMI persistent color depth %s conversion failed", colorDepthStr.c_str());
            return defaultColorDepth;
        }
    }

    static dsError_t getCurrentPreferredColorDepth(intptr_t handle, dsDisplayColorDepth_t* colorDepth)
    {
        if (colorDepth == nullptr) {
            return dsERR_INVALID_PARAM;
        }

        typedef dsError_t (*dsGetPreferredColorDepth_t)(intptr_t, dsDisplayColorDepth_t*);
        static dsGetPreferredColorDepth_t func = nullptr;
        if (func == nullptr) {
            func = reinterpret_cast<dsGetPreferredColorDepth_t>(resolve(RDK_DSHAL_NAME, "dsGetPreferredColorDepth"));
        }

        if (func == nullptr) {
            DSLOG_ERR("dsGetPreferredColorDepth symbol not available");
            return dsERR_GENERAL;
        }

        return func(handle, colorDepth);
    }

    static dsError_t getColorDepthCapabilitiesHAL(intptr_t handle, unsigned int* colorDepthCapability)
    {
        if (colorDepthCapability == nullptr) {
            return dsERR_INVALID_PARAM;
        }

        typedef dsError_t (*dsColorDepthCapabilities_t)(intptr_t, unsigned int*);
        static dsColorDepthCapabilities_t func = nullptr;
        if (func == nullptr) {
            func = reinterpret_cast<dsColorDepthCapabilities_t>(resolve(RDK_DSHAL_NAME, "dsColorDepthCapabilities"));
        }

        if (func == nullptr) {
            DSLOG_ERR("dsColorDepthCapabilities symbol not available");
            return dsERR_GENERAL;
        }

        return func(handle, colorDepthCapability);
    }

    static dsDisplayColorDepth_t getBestSupportedColorDepth(intptr_t handle, dsDisplayColorDepth_t requested)
    {
        unsigned int capability = 0;
        dsError_t capError = getColorDepthCapabilitiesHAL(handle, &capability);
        if (capError != dsERR_NONE) {
            DSLOG_WARN("Color depth capability query failed (%d), fallback to 8bit", capError);
            return dsDISPLAY_COLORDEPTH_8BIT;
        }

        capability |= dsDISPLAY_COLORDEPTH_AUTO;

        if ((capability & requested) && (requested != dsDISPLAY_COLORDEPTH_AUTO)) {
            return requested;
        }
        if (capability & dsDISPLAY_COLORDEPTH_12BIT) {
            return dsDISPLAY_COLORDEPTH_12BIT;
        }
        if (capability & dsDISPLAY_COLORDEPTH_10BIT) {
            return dsDISPLAY_COLORDEPTH_10BIT;
        }
        if (capability & dsDISPLAY_COLORDEPTH_8BIT) {
            return dsDISPLAY_COLORDEPTH_8BIT;
        }
        return dsDISPLAY_COLORDEPTH_8BIT;
    }

    static dsError_t setPreferredColorDepthHAL(intptr_t handle, dsDisplayColorDepth_t colorDepth)
    {
        typedef dsError_t (*dsSetPreferredColorDepth_t)(intptr_t, dsDisplayColorDepth_t);
        static dsSetPreferredColorDepth_t func = nullptr;
        if (func == nullptr) {
            func = reinterpret_cast<dsSetPreferredColorDepth_t>(resolve(RDK_DSHAL_NAME, "dsSetPreferredColorDepth"));
        }

        if (func == nullptr) {
            DSLOG_ERR("dsSetPreferredColorDepth symbol not available");
            return dsERR_GENERAL;
        }

        return func(handle, colorDepth);
    }
};
