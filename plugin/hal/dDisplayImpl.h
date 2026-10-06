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
#include <cstdio>
#include <dlfcn.h>
#include <iostream>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "dDisplay.h"
#include "dsDisplay.h"
#include "dsError.h"
#include "dsUtl.h"
#include "dsTypes.h"

#include <WPEFramework/interfaces/IDeviceSettingsDisplay.h>
#include "DeviceSettingsTypes.h"
#include "DeviceSettingsHdmiStatus.h"
#include "DeviceSettingsHALConfig.h"
#include "dVideoPortImpl.h"
#include "DeviceSettingsTelemetry.h"

#ifndef RDK_DSHAL_NAME
#warning   "RDK_DSHAL_NAME is not defined"
#define RDK_DSHAL_NAME "RDK_DSHAL_NAME is not defined"
#endif

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static int display_isInitialized = 0;
static int display_isPlatInitialized = 0;
/* EDID caches — mirrors isEdidCached / isEdidBytesCached in dsDisplay.c.
 * Populated on first successful HAL read; reset to false on
 * dsDISPLAY_EVENT_DISCONNECTED (matching dsDisplay.c _dsDisplayEventCallback). */
static bool isEdidCached = false;
static bool isEdidBytesCached = false;
static dsDisplayEDID_t s_edidStructCache;          // cache for GetDisplayEdid
static unsigned char   s_edidBytesCache[1024] = {0}; // cache for GetDisplayEdidBytes
static int             s_edidBytesCacheLength = 0;
static pthread_mutex_t dsDisplayLock = PTHREAD_MUTEX_INITIALIZER;

// Static global callback functions for Display events
static std::function<void(const uint8_t, const bool)> g_DisplayRxSenseCallback;
static std::function<void(const uint8_t, const int32_t)> g_DisplayHDCPStatusCallback;
static std::function<void(const uint8_t, const bool)> g_DisplayHDMIHotPlugCallback;

class dDisplayImpl : public hal::dDisplay::IPlatform {

    // delete copy constructor and assignment operator
    dDisplayImpl(const dDisplayImpl&) = delete; 
    dDisplayImpl& operator=(const dDisplayImpl&) = delete;

public:
    dDisplayImpl()
    {
        DSLOG_INFO("Constructor");
        getInstance() = this; // Set static instance for callback access
        InitialiseHAL();
    }

    virtual ~dDisplayImpl()
    {
        DSLOG_INFO("Destructor");
        DeInitialiseHAL();
        getInstance() = nullptr; // Clear static instance
    }

    // Resolve method for dynamic library loading - following dHdmiInImpl.h pattern
    static void* resolve(const std::string& libName, const std::string& symbolName) {
        void* handle = dlopen(libName.c_str(), RTLD_LAZY);
        if (!handle) {
            DSLOG_ERR("dlopen failed for %s: %s", libName.c_str(), dlerror());
            return nullptr;
        }
        void* symbol = dlsym(handle, symbolName.c_str());
        if (!symbol) {
            DSLOG_ERR("dlsym failed for %s: %s", symbolName.c_str(), dlerror());
        }
        dlclose(handle);
        return symbol;
    }

    // Singleton getInstance method - following VideoPort pattern
    static dDisplayImpl*& getInstance()
    {
        static dDisplayImpl* instance = nullptr;
        return instance;
    }

    void InitialiseHAL()
    {
        
        if (!display_isPlatInitialized) {
            DSLOG_INFO("<dsDisplay>");
            dsError_t eError = dsDisplayInit();
            if (dsERR_NONE != eError) {
                DSLOG_ERR(" dsDisplayInit failed with error: %d", eError);
                return;
            }
            DSLOG_INFO(" dsDisplayInit succeeded");
            
            // Load persistence values after successful initialization
            getPersistenceValue();
            
            display_isPlatInitialized = 1;
            DSLOG_INFO("completed: display_isPlatInitialized=%d, display_isInitialized=%d",
                    display_isPlatInitialized, display_isInitialized);
        }
    }

    void DeInitialiseHAL()
    {
        if (display_isPlatInitialized)
        {
            dsDisplayTerm();
            display_isPlatInitialized = 0;
        }
        display_isInitialized = 0;
    }

    void setAllCallbacks(const CallbackBundle& bundle) override
    {
        DSLOG_INFO("dDisplayImpl setAllCallbacks");
        
        if (!display_isInitialized) {
            // Set the global callback function pointers
            g_DisplayRxSenseCallback = bundle.OnDisplayRxSense;
            g_DisplayHDCPStatusCallback = bundle.OnDisplayHDCPStatus;
            g_DisplayHDMIHotPlugCallback = bundle.OnDisplayHDMIHotPlug;

            // Register HAL callbacks
            registerDisplayEventCallbacks();
            
            display_isInitialized = 1;
            DSLOG_INFO("dDisplayImpl setAllCallbacks: Display callbacks registered successfully");
        } else {
            DSLOG_INFO("dDisplayImpl setAllCallbacks: Display already initialized, skipping callback registration");
        }
    }

    void getPersistenceValue() override
    {
        DSLOG_INFO("dDisplayImpl getPersistenceValue - Display persistence loading");
        // Load any display-specific persistence values here
        // This would be similar to VideoPort persistence loading but for Display settings
    }

    // Implementation of Display Platform interface methods
    uint32_t GetConnectedVideoDisplay(const int32_t videoPortHandle, bool& isConnected) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" videoPortHandle=%d", videoPortHandle);
        
        pthread_mutex_lock(&dsDisplayLock);
        
        // Use dynamic library loading for dsIsDisplayConnected
        typedef dsError_t (*dsIsDisplayConnected_t)(intptr_t handle, bool *connected);
        static dsIsDisplayConnected_t func = 0;
        if (func == 0) {
            func = (dsIsDisplayConnected_t) resolve(RDK_DSHAL_NAME, "dsIsDisplayConnected");
        }
        
        if (func != 0) {
            dsError_t eError = func(static_cast<intptr_t>(videoPortHandle), &isConnected);
            if (eError == dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - isConnected=%s", isConnected ? "true" : "false");
            } else {
                DSLOG_ERR(" FAILED - dsIsDisplayConnected error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsIsDisplayConnected not available");
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

    uint32_t GetDisplaySurroundMode(const int32_t videoPortHandle, VideoPortSurroundMode& surroundMode) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" videoPortHandle=%d", videoPortHandle);
        
        pthread_mutex_lock(&dsDisplayLock);
        
        // Use dynamic library loading for dsGetDisplaySurroundMode
        typedef dsError_t (*dsGetDisplaySurroundMode_t)(intptr_t handle, int *surroundMode);
        static dsGetDisplaySurroundMode_t func = 0;
        if (func == 0) {
            func = (dsGetDisplaySurroundMode_t) resolve(RDK_DSHAL_NAME, "dsGetDisplaySurroundMode");
        }
        
        if (func != 0) {
            int dsSurroundMode = 0;
            dsError_t eError = func(static_cast<intptr_t>(videoPortHandle), &dsSurroundMode);
            if (eError == dsERR_NONE) {
                surroundMode = static_cast<VideoPortSurroundMode>(dsSurroundMode);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - surroundMode=%d", static_cast<int>(surroundMode));
            } else {
                DSLOG_ERR(" FAILED - dsGetDisplaySurroundMode error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsGetDisplaySurroundMode not available");
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

    uint32_t GetDisplayEDID(const int32_t videoPortHandle, uint8_t edidBytes[], const uint16_t edidBytesLength) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" videoPortHandle=%d, edidBytesLength=%d", videoPortHandle, edidBytesLength);
        
        if (!edidBytes || edidBytesLength <= 0) {
            DSLOG_ERR(" FAILED - Invalid parameters");
            return WPEFramework::Core::ERROR_BAD_REQUEST;
        }
        
        pthread_mutex_lock(&dsDisplayLock);
        
        // Use dynamic library loading for dsGetEDIDBytes  
        typedef dsError_t (*dsGetEDIDBytes_t)(intptr_t handle, unsigned char *edid, int *length);
        static dsGetEDIDBytes_t func = 0;
        if (func == 0) {
            func = (dsGetEDIDBytes_t) resolve(RDK_DSHAL_NAME, "dsGetEDIDBytes");
        }
        
        if (func != 0) {
            int actualLength = edidBytesLength;
            dsError_t eError = func(static_cast<intptr_t>(videoPortHandle), edidBytes, &actualLength);
            if (eError == dsERR_NONE && actualLength <= edidBytesLength) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - actualLength=%d", actualLength);
            } else {
                DSLOG_ERR(" FAILED - dsGetEDIDBytes error=%d, actualLength=%d", eError, actualLength);
            }
        } else {
            DSLOG_ERR(" FAILED - dsGetEDIDBytes not available");
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

    uint32_t GetDisplayEdidBytes(const int32_t handle, uint8_t edIdBytes[], const uint16_t edidLength) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, edidLength=%d", handle, edidLength);
        
        if (edIdBytes == nullptr || edidLength == 0) {
            DSLOG_ERR(" FAILED - Invalid parameters");
            return retCode;
        }

        pthread_mutex_lock(&dsDisplayLock);

        /* Mirror dsDisplay.c _dsGetEDIDBytes: serve from cache if available
         * (reset to false on dsDISPLAY_EVENT_DISCONNECTED). Checked under
         * dsDisplayLock since it is also written under the same lock below. */
        if (isEdidBytesCached && s_edidBytesCacheLength > 0 &&
            s_edidBytesCacheLength <= static_cast<int>(edidLength)) {
            memcpy(edIdBytes, s_edidBytesCache, s_edidBytesCacheLength);
            DSLOG_INFO(" returning cached EDID bytes, length=%d", s_edidBytesCacheLength);
            pthread_mutex_unlock(&dsDisplayLock);
            return WPEFramework::Core::ERROR_NONE;
        }

        // Use resolve method for dsGetEDIDBytes (matches dsDisplay.c pattern)
        typedef dsError_t (*dsGetEDIDBytes_t)(intptr_t handle, uint8_t *edidBytes, int *actualLength);
        static dsGetEDIDBytes_t func = 0;
        if (func == 0) {
            func = (dsGetEDIDBytes_t) resolve(RDK_DSHAL_NAME, "dsGetEDIDBytes");
        }
        
        if (func != 0) {
            int actualLength = 0;
            dsError_t eError = func(handle, edIdBytes, &actualLength);
            if (eError == dsERR_NONE && actualLength > 0 &&
                actualLength <= static_cast<int>(edidLength) &&
                actualLength <= static_cast<int>(sizeof(s_edidBytesCache))) {
                /* Populate cache — mirrors dsDisplay.c isEdidBytesCached = true */
                memcpy(s_edidBytesCache, edIdBytes, actualLength);
                s_edidBytesCacheLength = actualLength;
                isEdidBytesCached = true;
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - actualLength=%d (cached)", actualLength);
            } else {
                DSLOG_ERR(" FAILED - dsGetEDIDBytes error=%d, actualLength=%d", eError, actualLength);
            }
        } else {
            DSLOG_ERR(" FAILED - dsGetEDIDBytes not available");
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

    // New Display HAL methods implementation
    uint32_t GetDisplay(const int32_t type, const int32_t index, int32_t &handle) override 
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" type=%d, index=%d", type, index);

        // Validate input parameters
        if (type < 0 || index < 0) {
            DSLOG_ERR(" FAILED - Invalid parameters, type=%d, index=%d", type, index);
            return WPEFramework::Core::ERROR_BAD_REQUEST;
        }

        // Initialize handle to safe value
        handle = -1;

        // Add safety check for mutex lock
        int lock_result = pthread_mutex_lock(&dsDisplayLock);
        if (lock_result != 0) {
            DSLOG_ERR(" FAILED - Could not acquire mutex lock, error=%d", lock_result);
            return WPEFramework::Core::ERROR_GENERAL;
        }

        // Use direct call for dsGetDisplay (matches dsDisplay.c _dsGetDisplay pattern)
        intptr_t halHandle = 0;
        DSLOG_INFO(" Calling dsGetDisplay with type=%d, index=%d", type, index);
        
        dsError_t eError = dsGetDisplay(static_cast<dsVideoPortType_t>(type), index, &halHandle);
        
        if (eError == dsERR_NONE) {
            handle = static_cast<int32_t>(halHandle);
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - handle=%d", handle);
        } else {
            if (eError == dsERR_OPERATION_NOT_SUPPORTED)
                DSLOG_WARN(" not supported for portType=%d (error=%d)", type, eError);
            else
                DSLOG_ERR(" FAILED - dsGetDisplay error=%d", eError);
            handle = -1;
        }
        
        int unlock_result = pthread_mutex_unlock(&dsDisplayLock);
        if (unlock_result != 0) {
            DSLOG_ERR(" WARNING - Could not release mutex lock, error=%d", unlock_result);
        }
        
        return retCode;
    }

    uint32_t GetDisplayAspectRatio(const int32_t handle, DisplayVideoAspectRatio &aspectRatio) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        pthread_mutex_lock(&dsDisplayLock);
        
        // Use direct call for dsGetDisplayAspectRatio (matches dsDisplay.c _dsGetDisplayAspectRatio pattern)
        dsVideoAspectRatio_t halAspectRatio;
        dsError_t eError = dsGetDisplayAspectRatio(handle, &halAspectRatio);
        if (eError == dsERR_NONE) {
            // Convert DS HAL type to WPE Framework type
            aspectRatio = (halAspectRatio == dsVIDEO_ASPECT_RATIO_4x3) ? 
                         DisplayVideoAspectRatio::DS_DISPLAY_ASPECT_RATIO_4X3 : 
                         DisplayVideoAspectRatio::DS_DISPLAY_ASPECT_RATIO_16X9;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS - aspectRatio=%d", static_cast<int>(aspectRatio));
        } else {
            DSLOG_ERR(" FAILED - dsGetDisplayAspectRatio error=%d", eError);
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

    uint32_t GetDisplayEdid(const int32_t handle, WPEFramework::Exchange::IDeviceSettingsDisplay::DisplayEDID &edId, IDSVideoPortResolutionIterator*& supportedResolutionList) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        supportedResolutionList = nullptr;

        pthread_mutex_lock(&dsDisplayLock);

        /* Mirror dsDisplay.c _dsGetEDID: serve from cache when available.
         * Cache is reset to false on dsDISPLAY_EVENT_DISCONNECTED.
         * isEdidCached must be checked under dsDisplayLock since it is also
         * written under the same lock below. */
        if (isEdidCached) {
            edId.productCode            = s_edidStructCache.productCode;
            edId.serialNumber           = s_edidStructCache.serialNumber;
            edId.manufactureYear        = s_edidStructCache.manufactureYear;
            edId.manufactureWeek        = s_edidStructCache.manufactureWeek;
            edId.hdmiDeviceType         = s_edidStructCache.hdmiDeviceType;
            edId.isRepeater             = s_edidStructCache.isRepeater;
            edId.physicalAddressA       = s_edidStructCache.physicalAddressA;
            edId.physicalAddressB       = s_edidStructCache.physicalAddressB;
            edId.physicalAddressC       = s_edidStructCache.physicalAddressC;
            edId.physicalAddressD       = s_edidStructCache.physicalAddressD;
            edId.numOfSupportedResolution = s_edidStructCache.numOfSupportedResolution;
            edId.monitorName            = std::string(s_edidStructCache.monitorName);
            buildSupportedResolutionIterator(s_edidStructCache, supportedResolutionList);
            DSLOG_INFO(" returned %d cached supported resolutions", s_edidStructCache.numOfSupportedResolution);
            DSLOG_INFO(" returning cached EDID");
            pthread_mutex_unlock(&dsDisplayLock);
            return WPEFramework::Core::ERROR_NONE;
        }

        // Use direct call for dsGetEDID (matches dsDisplay.c _dsGetEDID pattern).
        // Read straight into s_edidStructCache instead of a stack-local dsDisplayEDID_t
        // (~13KB) to avoid an oversized stack frame; still guarded by dsDisplayLock.
        dsDisplayEDID_t& halEdid = s_edidStructCache;
        memset(&halEdid, 0, sizeof(halEdid));
        dsError_t eError = dsGetEDID(handle, &halEdid);
        if (eError == dsERR_NONE) {
            /* Mirror dsDisplay.c _dsGetEDID: filter the sink's advertised resolutions
             * down to those the platform can actually output, before caching/dumping. */
            filterEDIDResolution(handle, halEdid);

            /* halEdid already refers to s_edidStructCache — mirrors dsDisplay.c pattern */
            isEdidCached = true;
            dumpEDIDInformation(&halEdid);
            dumpHdmiEdidInfo(&halEdid);

            // Convert DS HAL type to WPE Framework type
            edId.productCode            = halEdid.productCode;
            edId.serialNumber           = halEdid.serialNumber;
            edId.manufactureYear        = halEdid.manufactureYear;
            edId.manufactureWeek        = halEdid.manufactureWeek;
            edId.hdmiDeviceType         = halEdid.hdmiDeviceType;
            edId.isRepeater             = halEdid.isRepeater;
            edId.physicalAddressA       = halEdid.physicalAddressA;
            edId.physicalAddressB       = halEdid.physicalAddressB;
            edId.physicalAddressC       = halEdid.physicalAddressC;
            edId.physicalAddressD       = halEdid.physicalAddressD;
            edId.numOfSupportedResolution = halEdid.numOfSupportedResolution;
            edId.monitorName            = std::string(halEdid.monitorName);
            buildSupportedResolutionIterator(halEdid, supportedResolutionList);
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" SUCCESS (cached for next call)");
        } else {
            DSLOG_ERR(" FAILED - dsGetEDID error=%d", eError);
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

    static void buildSupportedResolutionIterator(const dsDisplayEDID_t& halEdid, IDSVideoPortResolutionIterator*& supportedResolutionList)
    {
        supportedResolutionList = nullptr;

        if (halEdid.numOfSupportedResolution <= 0) {
            return;
        }

        std::vector<DisplayVideoPortResolution> resolutions;
        resolutions.reserve(static_cast<size_t>(halEdid.numOfSupportedResolution));

        // Bound by the actual array extent, not a macro product that may not match it.
        const int maxResolutions = static_cast<int>(sizeof(halEdid.suppResolutionList) / sizeof(halEdid.suppResolutionList[0]));
        const int resolutionCount = (halEdid.numOfSupportedResolution < maxResolutions)
            ? halEdid.numOfSupportedResolution
            : maxResolutions;

        for (int i = 0; i < resolutionCount; ++i) {
            const dsVideoPortResolution_t& halResolution = halEdid.suppResolutionList[i];
            DisplayVideoPortResolution resolution;
            resolution.name = std::string(halResolution.name);
            resolution.pixelResolution = static_cast<DisplayTVResolution>(halResolution.pixelResolution);
            resolution.aspectRatio = static_cast<DisplayVideoAspectRatio>(halResolution.aspectRatio);
            resolution.stereoScopicMode = static_cast<DisplayInVideoStereoScopicMode>(halResolution.stereoScopicMode);
            resolution.frameRate = static_cast<DisplayInVideoFrameRate>(halResolution.frameRate);
            resolution.interlaced = halResolution.interlaced;
            resolutions.push_back(std::move(resolution));
        }

        using ResolutionIterator = WPEFramework::RPC::IteratorType<IDSVideoPortResolutionIterator>;
        supportedResolutionList = WPEFramework::Core::Service<ResolutionIterator>::Create<IDSVideoPortResolutionIterator>(resolutions);
    }

    /* Mirror dsDisplay.c _GetDisplayPortType: find which configured video port a
     * display handle belongs to, by matching dsGetDisplay() handles for each port. */
    static dsVideoPortType_t GetDisplayPortType(const intptr_t handle)
    {
        std::vector<VideoPortTypeConfig> videoPortTypes;
        std::vector<VideoPortPortConfig> videoPorts;
        DeviceSettingsHAL::PopulateVideoPortConfig(videoPortTypes, videoPorts);

        for (const auto& port : videoPorts) {
            intptr_t halHandle = 0;
            if (dsGetDisplay(static_cast<dsVideoPortType_t>(port.videoPortType), port.videoPortIndex, &halHandle) == dsERR_NONE &&
                halHandle == handle) {
                return static_cast<dsVideoPortType_t>(port.videoPortType);
            }
        }
        DSLOG_INFO(" Requested Display handle=%d is not part of Platform Port Configuration", static_cast<int>(handle));
        return dsVIDEOPORT_TYPE_MAX;
    }

    /* Mirror dsDisplay.c filterEDIDResolution: for HDMI displays, narrow the sink's
     * advertised EDID resolution list down to resolutions the platform actually supports. */
    static void filterEDIDResolution(const intptr_t handle, dsDisplayEDID_t& edid)
    {
        if (GetDisplayPortType(handle) != dsVIDEOPORT_TYPE_HDMI) {
            DSLOG_INFO(" EDID for non-HDMI port; resolution list left unchanged");
            return;
        }
        DSLOG_INFO(" EDID for HDMI port; filtering against platform-supported resolutions");

        if (edid.numOfSupportedResolution <= 0) {
            DSLOG_INFO(" no HAL-reported resolutions to filter");
            return;
        }

        std::vector<VideoPortResolution> platformResolutions;
        DeviceSettingsHAL::PopulateVideoPortResolutionConfig(VideoPortType::DS_VIDEO_PORT_TYPE_HDMI, platformResolutions);

        // Bound by the actual array extent, not a macro product that may not match it.
        const int maxResolutions = static_cast<int>(sizeof(edid.suppResolutionList) / sizeof(edid.suppResolutionList[0]));
        const int originalCount = (edid.numOfSupportedResolution < maxResolutions)
            ? edid.numOfSupportedResolution
            : maxResolutions;
        const std::vector<dsVideoPortResolution_t> originalList(edid.suppResolutionList, edid.suppResolutionList + originalCount);

        DSLOG_INFO(" Total Resolution Count from HAL: %d", originalCount);
        int filteredCount = 0;
        for (const auto& platformRes : platformResolutions) {
            for (int j = 0; j < originalCount && filteredCount < maxResolutions; ++j) {
                if (platformRes.name == std::string(originalList[j].name)) {
                    edid.suppResolutionList[filteredCount] = originalList[j];
                    ++filteredCount;
                    DSLOG_INFO(" matched resolution: %s, count=%d", platformRes.name.c_str(), filteredCount);
                }
            }
        }
        edid.numOfSupportedResolution = filteredCount;
    }

    /* Mirror dsDisplay.c dumpEDIDInformation — logs EDID product/serial/year/
     * week/monitorName/deviceType/repeater, matching the IARM server output. */
    static void dumpEDIDInformation(dsDisplayEDID_t *edid)
    {
        if (!edid) return;
        DSLOG_INFO("[DsMgr]dumpEDIDInformation values:%x,%x,%d,%d,%s,%s,%x",
                edid->productCode, edid->serialNumber,
                edid->manufactureYear, edid->manufactureWeek,
                edid->monitorName,
                edid->hdmiDeviceType ? "HDMI" : "DVI",
                edid->isRepeater);
        DSLOG_INFO("[DsMgr]numOfSupportedResolution=%d", edid->numOfSupportedResolution);

        char telemetryValue[512] = {0};
        snprintf(telemetryValue, sizeof(telemetryValue),
                "[DsMgr]dumpEDIDInformation values Product Code:%x, Serial Number:%x, Manufacture Year:%d, Manufacture Week:%d, Monitor Name:%s, Device Type:%s, IsRepeater:%x",
                edid->productCode, edid->serialNumber, edid->manufactureYear, edid->manufactureWeek,
                edid->monitorName, edid->hdmiDeviceType ? "HDMI" : "DVI", edid->isRepeater);
        TELEMETRY_EVENT_STRING("SYST_INFO_TVData_split", telemetryValue);
    }

    /* Mirror iarmmgrs dsMgr.c dumpHdmiEdidInfo — emits monitor name / DVI-device
     * telemetry markers on top of the dsDisplay.c dump, from the same EDID read. */
    static void dumpHdmiEdidInfo(dsDisplayEDID_t *edid)
    {
        if (!edid || strlen(edid->monitorName) == 0) {
            DSLOG_INFO("Received EDID is NULL or monitorName is empty");
            return;
        }
        char eventMsg[256] = {0};
        snprintf(eventMsg, sizeof(eventMsg), "HDMI  Monitor Name is %s", edid->monitorName);
        TELEMETRY_EVENT_STRING("HDMI_INFO_MonitorName_split", eventMsg);
        if (!edid->hdmiDeviceType) {
            TELEMETRY_EVENT_INT("HDMI_INFO_DVIDevice", 1);
        }
    }

    uint32_t SetAllmEnabled(const int32_t handle, const bool enabled) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, enabled=%s", handle, enabled ? "true" : "false");
        
        pthread_mutex_lock(&dsDisplayLock);
        
        // Use resolve method for dsSetAllmEnabled (matches dsDisplay.c pattern)
        typedef dsError_t (*dsSetAllmEnabled_t)(intptr_t handle, bool enabled);
        typedef dsError_t (*dsGetAllmEnabled_t)(intptr_t handle, bool *enabled);
        static dsSetAllmEnabled_t func_dsSetAllmEnabled = 0;
        static dsGetAllmEnabled_t func_dsGetAllmEnabled = 0;
        if (func_dsGetAllmEnabled == 0 && func_dsSetAllmEnabled == 0) {
            func_dsGetAllmEnabled = (dsGetAllmEnabled_t) resolve(RDK_DSHAL_NAME, "dsGetAllmEnabled");
            func_dsSetAllmEnabled = (dsSetAllmEnabled_t) resolve(RDK_DSHAL_NAME, "dsSetAllmEnabled");
        }
        
        if (func_dsGetAllmEnabled != 0 && func_dsSetAllmEnabled != 0) {
            bool currentALLMState = false;
            dsError_t eError = func_dsGetAllmEnabled(handle, &currentALLMState);
            if (eError == dsERR_NONE) {
                if (currentALLMState == enabled) {
                    DSLOG_INFO(" ALLM mode already %s", enabled ? "Enabled" : "Disabled");
                    retCode = WPEFramework::Core::ERROR_NONE;
                } else {
                    DSLOG_INFO(" Current ALLM state %s, Requested to %s",
                           currentALLMState ? "Enabled" : "Disabled", enabled ? "Enabled" : "Disabled");
                    eError = func_dsSetAllmEnabled(handle, enabled);
                    if (eError == dsERR_NONE) {
                        retCode = WPEFramework::Core::ERROR_NONE;
                        DSLOG_INFO(" SUCCESS");
                    } else {
                        DSLOG_ERR(" FAILED - dsSetAllmEnabled error=%d", eError);
                    }
                }
            } else {
                DSLOG_ERR(" FAILED - dsGetAllmEnabled error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsSetAllmEnabled/dsGetAllmEnabled not available");
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

    uint32_t SetAVIContentType(const int32_t handle, const int32_t contentType) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, contentType=%d", handle, contentType);
        
        pthread_mutex_lock(&dsDisplayLock);
        
        // Use resolve method for dsSetAVIContentType (matches dsDisplay.c pattern)
        typedef dsError_t (*dsSetAVIContentType_t)(intptr_t handle, dsAviContentType_t contentType);
        typedef dsError_t (*dsGetAVIContentType_t)(intptr_t handle, dsAviContentType_t* contentType);
        static dsSetAVIContentType_t func_dsSetAVIContentType = 0;
        static dsGetAVIContentType_t func_dsGetAVIContentType = 0;
        if (func_dsGetAVIContentType == 0 && func_dsSetAVIContentType == 0) {
            func_dsSetAVIContentType = (dsSetAVIContentType_t) resolve(RDK_DSHAL_NAME, "dsSetAVIContentType");
            func_dsGetAVIContentType = (dsGetAVIContentType_t) resolve(RDK_DSHAL_NAME, "dsGetAVIContentType");
        }
        
        if (func_dsGetAVIContentType != 0 && func_dsSetAVIContentType != 0) {
            dsAviContentType_t currentContentType = dsAVICONTENT_TYPE_NOT_SIGNALLED;
            dsError_t eError = func_dsGetAVIContentType(handle, &currentContentType);
            if (eError == dsERR_NONE) {
                if (currentContentType == static_cast<dsAviContentType_t>(contentType)) {
                    DSLOG_INFO(" HDMI AVI content type already set to %d", contentType);
                    retCode = WPEFramework::Core::ERROR_NONE;
                } else {
                    DSLOG_INFO(" Current AVI content type %d, requested content type %d",
                           currentContentType, contentType);
                    eError = func_dsSetAVIContentType(handle, static_cast<dsAviContentType_t>(contentType));
                    if (eError == dsERR_NONE) {
                        retCode = WPEFramework::Core::ERROR_NONE;
                        DSLOG_INFO(" SUCCESS");
                    } else {
                        DSLOG_ERR(" FAILED - dsSetAVIContentType error=%d", eError);
                    }
                }
            } else {
                DSLOG_ERR(" FAILED - dsGetAVIContentType error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsSetAVIContentType/dsGetAVIContentType not available");
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

    uint32_t SetAVIScanInformation(const int32_t handle, const int32_t scanInfo) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, scanInfo=%d", handle, scanInfo);
        
        pthread_mutex_lock(&dsDisplayLock);
        
        // Use resolve method for dsSetAVIScanInformation (matches dsDisplay.c pattern)
        typedef dsError_t (*dsSetAVIScanInfo_t)(intptr_t handle, dsAVIScanInformation_t scanInfo);
        typedef dsError_t (*dsGetAVIScanInfo_t)(intptr_t handle, dsAVIScanInformation_t* scanInfo);
        static dsSetAVIScanInfo_t func_dsSetAVIScanInfo = 0;
        static dsGetAVIScanInfo_t func_dsGetAVIScanInfo = 0;
        if (func_dsGetAVIScanInfo == 0 && func_dsSetAVIScanInfo == 0) {
            func_dsSetAVIScanInfo = (dsSetAVIScanInfo_t) resolve(RDK_DSHAL_NAME, "dsSetAVIScanInformation");
            func_dsGetAVIScanInfo = (dsGetAVIScanInfo_t) resolve(RDK_DSHAL_NAME, "dsGetAVIScanInformation");
        }
        
        if (func_dsGetAVIScanInfo != 0 && func_dsSetAVIScanInfo != 0) {
            dsAVIScanInformation_t currentScanInfo = dsAVI_SCAN_TYPE_NO_DATA;
            dsError_t eError = func_dsGetAVIScanInfo(handle, &currentScanInfo);
            if (eError == dsERR_NONE) {
                if (currentScanInfo == static_cast<dsAVIScanInformation_t>(scanInfo)) {
                    DSLOG_INFO(" HDMI AVI scan Info already set to %d", scanInfo);
                    retCode = WPEFramework::Core::ERROR_NONE;
                } else {
                    DSLOG_INFO(" Current AVI scan Info %d, requested scan Info %d",
                           currentScanInfo, scanInfo);
                    eError = func_dsSetAVIScanInfo(handle, static_cast<dsAVIScanInformation_t>(scanInfo));
                    if (eError == dsERR_NONE) {
                        retCode = WPEFramework::Core::ERROR_NONE;
                        DSLOG_INFO(" SUCCESS");
                    } else {
                        DSLOG_ERR(" FAILED - dsSetAVIScanInformation error=%d", eError);
                    }
                }
            } else {
                DSLOG_ERR(" FAILED - dsGetAVIScanInformation error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsSetAVIScanInformation/dsGetAVIScanInformation not available");
        }
        
        pthread_mutex_unlock(&dsDisplayLock);
        return retCode;
    }

private:
    void registerDisplayEventCallbacks()
    {
        
        // Use direct calls matching dsDisplay.c _dsDisplayInit pattern
        intptr_t handle = 0;
        dsError_t eReturn = dsGetDisplay(dsVIDEOPORT_TYPE_HDMI, 0, &handle);
        if (dsERR_NONE != eReturn) {
            DSLOG_INFO(" dsGetDisplay for HDMI failed, trying INTERNAL");
            eReturn = dsGetDisplay(dsVIDEOPORT_TYPE_INTERNAL, 0, &handle);
            if (dsERR_NONE != eReturn) {
                DSLOG_ERR(" FAILED - dsGetDisplay for INTERNAL also failed, error=%d", eReturn);
                return;
            }
        }
        
        // Register display event callback using wrapper that adapts int to intptr_t
        dsError_t eError = dsRegisterDisplayEventCallback(handle, dsDisplayEventCallbackWrapper);
        if (eError == dsERR_NONE) {
            DSLOG_INFO(" SUCCESS - registered with handle=%d", static_cast<int>(handle));
        } else {
            DSLOG_ERR(" FAILED - error=%d", eError);
        }
    }

    // Wrapper callback that adapts int handle to intptr_t (for legacy devicesettings API compatibility)
    static void dsDisplayEventCallbackWrapper(int handle, dsDisplayEvent_t dsDisplayEvent, void* eventData)
    {
        // Cast int handle to intptr_t and call the implementation
        dsDisplayEventCallbackImpl(static_cast<intptr_t>(handle), dsDisplayEvent, eventData);
    }

    // Static callback function to handle display events from HAL
    static void dsDisplayEventCallbackImpl(intptr_t handle, dsDisplayEvent_t dsDisplayEvent, void* eventData)
    {
        DSLOG_INFO(" handle=%d, event=%d", static_cast<int>(handle), static_cast<int>(dsDisplayEvent));
        
        dDisplayImpl* instance = getInstance();
        if (!instance) {
            DSLOG_ERR(" No Display instance available");
            return;
        }

        uint8_t port = static_cast<uint8_t>(handle & 0xFF); // Extract port from handle

        switch (dsDisplayEvent) {
            case dsDISPLAY_RXSENSE_ON: // DS_DISPLAY_RXSENSE_ON equivalent
                if (g_DisplayRxSenseCallback) {
                    g_DisplayRxSenseCallback(port, true);
                }
                break;
                
            case dsDISPLAY_RXSENSE_OFF: // DS_DISPLAY_RXSENSE_OFF equivalent  
                TELEMETRY_EVENT_INT("HDMI_INFO_tv_off", 1);
                if (g_DisplayRxSenseCallback) {
                    g_DisplayRxSenseCallback(port, false);
                }
                break;
                
            case dsDISPLAY_HDCPPROTOCOL_CHANGE: // DS_DISPLAY_HDCPPROTOCOL_CHANGE equivalent
            {
                if (!eventData) {
                    DSLOG_ERR(" HDCP protocol version change has NULL event data");
                    return;
                }

                dsHdcpProtocolVersion_t protocolVersion =
                    *reinterpret_cast<dsHdcpProtocolVersion_t*>(eventData);
                DSLOG_INFO(" HDCP protocol version changed: version=%d", protocolVersion);
                _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPVERSION, protocolVersion);
                break;
            }
                
            case dsDISPLAY_EVENT_CONNECTED: // DS_DISPLAY_EVENT_CONNECTED equivalent
                // Legacy parity: reconcile persisted preferred color depth against
                // current sink capabilities before announcing connected state.
                dVideoPortImpl::ApplyPreferredColorDepthAfterHdmiReset();
                _dsSyncHdmiStatus(DS_HDMI_TAG_HOTPLUP, dsDISPLAY_EVENT_CONNECTED);
                if (g_DisplayHDMIHotPlugCallback) {
                    g_DisplayHDMIHotPlugCallback(port, true);
                }
                break;
                
            case dsDISPLAY_EVENT_DISCONNECTED: // DS_DISPLAY_EVENT_DISCONNECTED equivalent
                /* Mirror dsDisplay.c _dsDisplayEventCallback: reset EDID caches
                 * so next GetDisplayEdid/GetDisplayEdidBytes re-reads from HAL.
                 * Guarded by dsDisplayLock since the caches are also read/written
                 * under that lock in GetDisplayEdid/GetDisplayEdidBytes. */
                pthread_mutex_lock(&dsDisplayLock);
                isEdidCached = false;
                isEdidBytesCached = false;
                s_edidBytesCacheLength = 0;
                pthread_mutex_unlock(&dsDisplayLock);
                DSLOG_INFO(" DISCONNECTED — EDID caches invalidated");
                _dsSyncHdmiStatus(DS_HDMI_TAG_HOTPLUP, dsDISPLAY_EVENT_DISCONNECTED);
                if (g_DisplayHDMIHotPlugCallback) {
                    g_DisplayHDMIHotPlugCallback(port, false);
                }
                break;
                
            default:
                DSLOG_WARN(" Unknown event=%d", static_cast<int>(dsDisplayEvent));
                break;
        }
    }
};