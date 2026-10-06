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
#include <algorithm>
#include <cctype>
#include <functional>
#include <mutex>
#include <iostream>
#include <cstring>
#include <dlfcn.h>

#include "dVideoDevice.h"
#include "dsVideoDevice.h"
#include "dsError.h"
#include "dsUtl.h"
#include "dsTypes.h"
#include "dsHdmiIn.h"



#include <WPEFramework/interfaces/IDeviceSettingsVideoDevice.h>
#include "DeviceSettingsTypes.h"

// Static global variables from dsVideoDevice.c conversion
static int videoDevice_isInitialized = 0;
static int videoDevice_isPlatInitialized = 0;
static dsVideoZoom_t srv_dfc = dsVIDEO_ZOOM_FULL;
static bool force_disable_hdr = true;

// Static global callbacks for VideoDevice events - following HdmiIn pattern
// Each callback owns its own mutex/condition-variable (GuardedCallback) so one
// callback type's invocation never blocks registration/invocation of another.
static GuardedCallback<void(const VideoDeviceZoom)> g_VideoDeviceZoomSettingsChangedCallback;
static GuardedCallback<void(const string)> g_VideoDeviceDisplayFrameratePreChangeCallback;
static GuardedCallback<void(const string)> g_VideoDeviceDisplayFrameratePostChangeCallback;

class dVideoDeviceImpl : public hal::dVideoDevice::IPlatform {

    // delete copy constructor and assignment operator
    dVideoDeviceImpl(const dVideoDeviceImpl&) = delete;
    dVideoDeviceImpl& operator=(const dVideoDeviceImpl&) = delete;

public:
    dVideoDeviceImpl()
    {
        DSLOG_INFO("Constructor");
        // Precheck: drop any callback left over from a prior (already destroyed) instance.
        // Reset() blocks until any invocation still in flight from that prior instance completes.
        g_VideoDeviceZoomSettingsChangedCallback.Reset();
        g_VideoDeviceDisplayFrameratePreChangeCallback.Reset();
        g_VideoDeviceDisplayFrameratePostChangeCallback.Reset();
        InitialiseHAL();
    }

    virtual ~dVideoDeviceImpl()
    {
        DSLOG_ERR("Destructor");
        // Terminate the HAL first so no further asynchronous callbacks can be
        // dispatched, then Reset() below blocks until any invocation already
        // in flight completes before this instance is destroyed.
        DeInitialiseHAL();
        g_VideoDeviceZoomSettingsChangedCallback.Reset();
        g_VideoDeviceDisplayFrameratePreChangeCallback.Reset();
        g_VideoDeviceDisplayFrameratePostChangeCallback.Reset();
    }

    // Singleton getInstance method - following HdmiIn pattern
    static dVideoDeviceImpl*& getInstance()
    {
        static dVideoDeviceImpl* instance = new dVideoDeviceImpl();
        return instance;
    }

    void InitialiseHAL()
    {
        // Note: videoDevice_isInitialized should only be set in setAllCallbacks after callback registration
        // Don't set it here as it prevents callback registration condition from working

        if (!videoDevice_isPlatInitialized) {
            DSLOG_INFO("<dsVideoDevice>");
            dsError_t eError = dsVideoDeviceInit();
            if (dsERR_NONE != eError) {
                DSLOG_ERR(" dsVideoDeviceInit failed with error: %d", eError);
                return;
            }
            DSLOG_INFO(" dsVideoDeviceInit succeeded");
            
            // Load persistence values after successful initialization - following dsVideoDevice.c pattern
            getPersistenceValue();
            
            videoDevice_isPlatInitialized = 1;
            DSLOG_INFO("completed: videoDevice_isPlatInitialized=%d, videoDevice_isInitialized=%d",
                    videoDevice_isPlatInitialized, videoDevice_isInitialized);
        }
    }

    void DeInitialiseHAL()
    {
        if (videoDevice_isPlatInitialized)
        {
            dsVideoDeviceTerm();
            videoDevice_isPlatInitialized = 0;
        }
        videoDevice_isInitialized = 0;
    }

    static void* resolve(const std::string& libName, const std::string& symbolName) {
        void* handle = dlopen(libName.c_str(), RTLD_LAZY);
        if (!handle) {
            DSLOG_ERR(" Failed to load library %s: %s", libName.c_str(), dlerror());
            return nullptr;
        }
        
        void* symbol = dlsym(handle, symbolName.c_str());
        if (!symbol) {
            DSLOG_ERR(" Failed to find symbol %s in %s: %s", symbolName.c_str(), libName.c_str(), dlerror());
            dlclose(handle);
            return nullptr;
        }
        
        DSLOG_INFO(" Successfully resolved %s from %s", symbolName.c_str(), libName.c_str());
        dlclose(handle);
        return symbol;
    }

    // Implementation of all VideoDevice Platform interface methods
    uint32_t GetVideoDeviceHandle(const int32_t index, int32_t& handle) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" index=%d", index);
        
        // Use intptr_t locally for HAL call - dsGetVideoDevice expects intptr_t*
        intptr_t halHandle = 0;
        dsError_t eError = dsGetVideoDevice(index, &halHandle);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
            handle = static_cast<int32_t>(halHandle);  // Cast result back to int32_t for interface
            DSLOG_INFO(" SUCCESS - handle=%d", handle);
        } else {
            DSLOG_ERR(" dsGetVideoDevice failed with error: %d", eError);
        }
        
        return retCode;
    }

    uint32_t SetVideoDeviceDFC(const int32_t handle, const VideoDeviceZoom zoomSetting) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, zoomSetting=%d", handle, static_cast<int>(zoomSetting));
        
        // Convert VideoDeviceZoom to dsVideoZoom_t
        dsVideoZoom_t dsZoom = convertVideoDeviceZoom(zoomSetting);
        
        try {
            if (dsZoom == dsVIDEO_ZOOM_NONE) {
                DSLOG_INFO("Call Zoom setting NONE");
                dsError_t eError = dsSetDFC(handle, dsZoom);
                if (eError == dsERR_NONE) {
                    srv_dfc = dsZoom;
                    retCode = WPEFramework::Core::ERROR_NONE;
                    device::HostPersistence::getInstance().persistHostProperty("VideoDevice.DFC", "None");
                    
                    // Trigger zoom settings changed callback
                    g_VideoDeviceZoomSettingsChangedCallback.Invoke(VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_NONE);
                    
                    DSLOG_INFO(" SUCCESS (NONE)");
                } else {
                    DSLOG_ERR(" dsSetDFC failed with error: %d", eError);
                }
            } else if (dsZoom == dsVIDEO_ZOOM_FULL) {
                DSLOG_INFO("Call Zoom setting FULL");
                dsError_t eError = dsSetDFC(handle, dsZoom);
                if (eError == dsERR_NONE) {
                    srv_dfc = dsZoom;
                    retCode = WPEFramework::Core::ERROR_NONE;
                    device::HostPersistence::getInstance().persistHostProperty("VideoDevice.DFC", "Full");
                    
                    // Trigger zoom settings changed callback
                    g_VideoDeviceZoomSettingsChangedCallback.Invoke(VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL);
                    
                    DSLOG_INFO(" SUCCESS (FULL)");
                } else {
                    DSLOG_ERR(" dsSetDFC failed with error: %d", eError);
                }
            } else if (dsZoom == dsVIDEO_ZOOM_16_9_ZOOM) {
                DSLOG_INFO("Call Zoom setting dsVIDEO_ZOOM_16_9_ZOOM");
                dsError_t eError = dsSetDFC(handle, dsZoom);
                if (eError == dsERR_NONE) {
                    srv_dfc = dsZoom;
                    retCode = WPEFramework::Core::ERROR_NONE;
                    device::HostPersistence::getInstance().persistHostProperty("VideoDevice.DFC", "Full");
                    
                    // Trigger zoom settings changed callback
                    g_VideoDeviceZoomSettingsChangedCallback.Invoke(VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_16_9_ZOOM);
                    
                    DSLOG_INFO(" SUCCESS (16_9_ZOOM)");
                } else {
                    DSLOG_ERR(" dsSetDFC failed with error: %d", eError);
                }
            } else {
                DSLOG_ERR("ERROR: unsupported Zoom setting %d", static_cast<int>(zoomSetting));
            }

            if (profileType == TV) {
                DSLOG_INFO("TV Profile - setting HDMI In zoom mode");
                dsHdmiInSelectZoomMode(srv_dfc);
            }
        } catch (...) {
            DSLOG_ERR("Error in Setting the Video Device DFC");
        }
        
        return retCode;
    }

    uint32_t GetVideoDeviceDFC(const int32_t handle, VideoDeviceZoom& zoomSetting) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        // Return the cached zoom setting
        zoomSetting = convertDSVideoZoom(srv_dfc);
        retCode = WPEFramework::Core::ERROR_NONE;
        DSLOG_INFO(" SUCCESS - zoomSetting=%d", static_cast<int>(zoomSetting));
        
        return retCode;
    }

    uint32_t GetHDRCapabilities(const int32_t handle, int32_t& capabilities) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetHDRCapabilitiesFunc_t)(intptr_t handle, int *capabilities);
        static dsGetHDRCapabilitiesFunc_t func = 0;
        if (func == 0) {
            func = (dsGetHDRCapabilitiesFunc_t)resolve(RDK_DSHAL_NAME, "dsGetHDRCapabilities");
            if (func) {
                DSLOG_INFO("dsGetHDRCapabilities() is defined and loaded");
            } else {
                DSLOG_INFO("dsGetHDRCapabilities() is not defined");
            }
        }
        
        if ((0 != func) && (false == force_disable_hdr)) {
            int dsCapabilities = 0;
            dsError_t eError = func(handle, &dsCapabilities);
            if (eError == dsERR_NONE) {
                capabilities = static_cast<int32_t>(dsCapabilities);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - capabilities=0x%x", capabilities);
            } else {
                DSLOG_ERR(" dsGetHDRCapabilities failed with error: %d", eError);
            }
        } else {
            capabilities = dsHDRSTANDARD_NONE;
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" HDR disabled or function not available - capabilities=0x%x", capabilities);
        }
        
        return retCode;
    }

    uint32_t GetSupportedVideoCodingFormats(const int32_t handle, int32_t& supportedFormats) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetSupportedVideoCodingFormatsFunc_t)(intptr_t handle, unsigned int *supported_formats);
        static dsGetSupportedVideoCodingFormatsFunc_t func = 0;
        if (func == 0) {
            func = (dsGetSupportedVideoCodingFormatsFunc_t)resolve(RDK_DSHAL_NAME, "dsGetSupportedVideoCodingFormats");
            if (func) {
                DSLOG_INFO("dsGetSupportedVideoCodingFormats() is defined and loaded");
            } else {
                DSLOG_INFO("dsGetSupportedVideoCodingFormats() is not defined");
            }
        }
        
        if (0 != func) {
            unsigned int dsFormats = 0;
            dsError_t eError = func(handle, &dsFormats);
            if (eError == dsERR_NONE) {
                supportedFormats = static_cast<int32_t>(dsFormats);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - supportedFormats=0x%x", supportedFormats);
            } else {
                DSLOG_ERR(" dsGetSupportedVideoCodingFormats failed with error: %d", eError);
            }
        } else {
            supportedFormats = 0x0; // Safe default: no formats supported
            retCode = WPEFramework::Core::ERROR_NONE;
            DSLOG_INFO(" Function not available - supportedFormats=0x%x", supportedFormats);
        }
        
        return retCode;
    }

    uint32_t GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, videoCodec=%d", handle, static_cast<int>(videoCodec));
        
        typedef dsError_t (*dsGetVideoCodecInfoFunc_t)(intptr_t handle, dsVideoCodingFormat_t codec, dsVideoCodecInfo_t * info);
        static dsGetVideoCodecInfoFunc_t func = 0;
        if (func == 0) {
            func = (dsGetVideoCodecInfoFunc_t)resolve(RDK_DSHAL_NAME, "dsGetVideoCodecInfo");
            if (func) {
                DSLOG_INFO("dsGetVideoCodecInfo() is defined and loaded");
            } else {
                DSLOG_INFO("dsGetVideoCodecInfo() is not defined");
            }
        }
        
        if (0 != func) {
            dsVideoCodingFormat_t dsFormat = convertVideoCodecToDSFormat(videoCodec);
            dsVideoCodecInfo_t info;
            dsError_t eError = func(handle, dsFormat, &info);
            if (eError == dsERR_NONE) {
                // Convert dsVideoCodecInfo_t to iterator
                codecInfo = createCodecInfoIterator(info);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - codecInfo created");
            } else {
                DSLOG_ERR(" dsGetVideoCodecInfo failed with error: %d", eError);
            }
        } else {
            retCode = WPEFramework::Core::ERROR_UNAVAILABLE;
            DSLOG_ERR(" Function not available");
        }
        
        return retCode;
    }

    uint32_t DisableHDR(const int32_t handle, const bool disable) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, disable=%s", handle, disable ? "true" : "false");
        
        typedef dsError_t (*dsDisableHDRSupportFunc_t)(intptr_t handle, bool enable);
        static dsDisableHDRSupportFunc_t func = 0;
        if (func == 0) {
            func = (dsDisableHDRSupportFunc_t)resolve(RDK_DSHAL_NAME, "dsForceDisableHDRSupport");
            if (func) {
                DSLOG_INFO("dsForceDisableHDRSupport() is defined and loaded");
            } else {
                DSLOG_INFO("dsForceDisableHDRSupport() is not defined");
            }
        }
        
        retCode = WPEFramework::Core::ERROR_NONE;
        
        if (0 != func) {
            dsError_t eError = func(handle, disable);
            if (eError != dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_GENERAL;
                DSLOG_ERR(" dsForceDisableHDRSupport failed with error: %d", eError);
            } else {
                DSLOG_INFO(" dsForceDisableHDRSupport succeeded - disable=%s", disable ? "true" : "false");
            }
        }
        
        force_disable_hdr = disable;
        if (force_disable_hdr) {
            device::HostPersistence::getInstance().persistHostProperty("VideoDevice.forceHDRDisabled", "true");
        } else {
            device::HostPersistence::getInstance().persistHostProperty("VideoDevice.forceHDRDisabled", "false");
        }
        
        return retCode;
    }

    uint32_t SetFRFMode(const int32_t handle, const int32_t frfmode) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, frfmode=%d", handle, frfmode);
        
        typedef dsError_t (*dsSetFRFModeFunc_t)(intptr_t handle, int frfmode);
        static dsSetFRFModeFunc_t func = 0;
        if (func == 0) {
            func = (dsSetFRFModeFunc_t)resolve(RDK_DSHAL_NAME, "dsSetFRFMode");
            if (func) {
                DSLOG_INFO("dsSetFRFMode is defined and loaded");
            } else {
                DSLOG_INFO("dsSetFRFMode is not defined");
            }
        }
        
        if (0 != func) {
            dsError_t eError = func(handle, frfmode);
            if (eError == dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" dsSetFRFMode failed with error: %d", eError);
            }
        }
        
        return retCode;
    }

    uint32_t GetFRFMode(const int32_t handle, int32_t& frfmode) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetFRFModeFunc_t)(intptr_t handle, int *frfmode);
        static dsGetFRFModeFunc_t func = 0;
        if (func == 0) {
            func = (dsGetFRFModeFunc_t)resolve(RDK_DSHAL_NAME, "dsGetFRFMode");
            if (func) {
                DSLOG_INFO("dsGetFRFMode() is defined and loaded");
            } else {
                DSLOG_INFO("dsGetFRFMode() is not defined");
            }
        }
        
        if (0 != func) {
            int dsFrfMode = 0;
            dsError_t eError = func(handle, &dsFrfMode);
            if (eError == dsERR_NONE) {
                frfmode = static_cast<int32_t>(dsFrfMode);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - frfmode=%d", frfmode);
            } else {
                DSLOG_ERR(" dsGetFRFMode failed with error: %d", eError);
            }
        }
        
        return retCode;
    }

    uint32_t GetCurrentDisplayFrameRate(const int32_t handle, string& framerate) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d", handle);
        
        typedef dsError_t (*dsGetCurrentDisframerateFunc_t)(intptr_t handle, char *framerate);
        static dsGetCurrentDisframerateFunc_t func = 0;
        if (func == 0) {
            func = (dsGetCurrentDisframerateFunc_t)resolve(RDK_DSHAL_NAME, "dsGetCurrentDisplayframerate");
            if (func) {
                DSLOG_INFO("dsGetCurrentDisframerate() is defined and loaded");
            } else {
                DSLOG_INFO("dsGetCurrentDisframerate() is not defined");
            }
        }
        
        if (0 != func) {
            char dsFramerate[32] = "";
            dsError_t eError = func(handle, dsFramerate);
            if (eError == dsERR_NONE) {
                framerate = string(dsFramerate);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - framerate=%s", framerate.c_str());
            } else {
                DSLOG_ERR(" dsGetCurrentDisplayframerate failed with error: %d", eError);
            }
        }
        
        return retCode;
    }

    uint32_t SetDisplayFrameRate(const int32_t handle, string framerate) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" handle=%d, framerate=%s", handle, framerate.c_str());
        
        typedef dsError_t (*dsSetDisplayframerateFunc_t)(intptr_t handle, char *frfmode);
        static dsSetDisplayframerateFunc_t func = 0;
        if (func == 0) {
            func = (dsSetDisplayframerateFunc_t)resolve(RDK_DSHAL_NAME, "dsSetDisplayframerate");
            if (func) {
                DSLOG_INFO("dsSetDisplayframerate() is defined and loaded");
            } else {
                DSLOG_INFO("dsSetDisplayframerate() is not defined");
            }
        }
        
        dsError_t result = dsERR_NONE;
        
        // Validate framerate parameter
        if (framerate.empty()) {
            result = dsERR_INVALID_PARAM;
            return WPEFramework::Core::ERROR_BAD_REQUEST;
        }
        
        // Send pre-change callback
        g_VideoDeviceDisplayFrameratePreChangeCallback.Invoke(framerate);
        
        if (0 != func) {
            char dsFramerate[32];
            strncpy(dsFramerate, framerate.c_str(), sizeof(dsFramerate) - 1);
            dsFramerate[sizeof(dsFramerate) - 1] = '\0';
            
            result = func(handle, dsFramerate);
            if (result == dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" dsSetDisplayframerate failed with error: %d", result);
            }
        }
        
        // Send post-change callback (skip on invalid param, matching dsVideoDevice.c broadcast guard)
        if (result != dsERR_INVALID_PARAM) {
            g_VideoDeviceDisplayFrameratePostChangeCallback.Invoke(std::move(framerate));
        }
        
        return retCode;
    }

    // VideoDevice Event Handling Infrastructure - following HdmiIn singleton pattern
    void setAllCallbacks(const CallbackBundle& bundle) override
    {
        ENTRY_LOG;
        DSLOG_INFO("Registering event callbacks with DS HAL");
        
        // Debug logging to diagnose condition failure
        DSLOG_INFO("VideoDevice callback registration check: videoDevice_isInitialized=%d, videoDevice_isPlatInitialized=%d",
                videoDevice_isInitialized, videoDevice_isPlatInitialized);
        
        if (videoDevice_isPlatInitialized && !videoDevice_isInitialized) {
            DSLOG_INFO("VideoDevice platform callback Initialization");
            
            // Register Zoom Settings Changed Callback
            if (bundle.OnZoomSettingsChanged) {
                DSLOG_INFO("VideoDevice Zoom Settings Changed Event Callback Registered");
                g_VideoDeviceZoomSettingsChangedCallback.Set(bundle.OnZoomSettingsChanged);
                // Zoom callbacks are triggered manually during DFC setting
            }
            
            // Register Display Framerate Pre-Change Callback - following dsVideoDevice.c pattern
            if (bundle.OnDisplayFrameratePreChange) {
                DSLOG_INFO("VideoDevice Display Framerate Pre-Change Event Callback Registered");
                g_VideoDeviceDisplayFrameratePreChangeCallback.Set(bundle.OnDisplayFrameratePreChange);
                
                // Register framerate pre-change callback with DS HAL - exact pattern from dsVideoDevice.c
                dsError_t eRet = VideoDeviceRegisterFrameratePreChangeCB(VideoDeviceFramerateStatusPreChangeCB);
                if (dsERR_NONE != eRet) {
                    DSLOG_ERR("VideoDeviceRegisterFrameratePreChangeCB failed with error: %d", eRet);
                } else {
                    DSLOG_INFO("Framerate pre-change callback registered successfully with DS HAL");
                }
            }
            
            // Register Display Framerate Post-Change Callback - following dsVideoDevice.c pattern
            if (bundle.OnDisplayFrameratePostChange) {
                DSLOG_INFO("VideoDevice Display Framerate Post-Change Event Callback Registered");
                g_VideoDeviceDisplayFrameratePostChangeCallback.Set(bundle.OnDisplayFrameratePostChange);
                
                // Register framerate post-change callback with DS HAL - exact pattern from dsVideoDevice.c
                dsError_t eRet = VideoDeviceRegisterFrameratePostChangeCB(VideoDeviceFramerateStatusPostChangeCB);
                if (dsERR_NONE != eRet) {
                    DSLOG_ERR("VideoDeviceRegisterFrameratePostChangeCB failed with error: %d", eRet);
                } else {
                    DSLOG_INFO("Framerate post-change callback registered successfully with DS HAL");
                }
            }
            
            videoDevice_isInitialized = 1;
            DSLOG_INFO("VideoDevice platform callback Initialization done");
        } else {
            if (!videoDevice_isPlatInitialized) {
                DSLOG_ERR("VideoDevice callback registration FAILED: Platform not initialized (videoDevice_isPlatInitialized=%d)",
                       videoDevice_isPlatInitialized);
            }
            if (videoDevice_isInitialized) {
                DSLOG_WARN("VideoDevice callback registration SKIPPED: Callbacks already initialized (videoDevice_isInitialized=%d)",
                        videoDevice_isInitialized);
            }
        }
        
        EXIT_LOG;
    }

    void getPersistenceValue()
    {
        ENTRY_LOG;
        DSLOG_INFO("Loading persistence settings");
        
        try {
            std::string _ZoomSettings("Full");
            /* Get the Zoom from Persistence */
            _ZoomSettings = device::HostPersistence::getInstance().getProperty("VideoDevice.DFC", _ZoomSettings);
            if (_ZoomSettings.compare("None") == 0) {
                srv_dfc = dsVIDEO_ZOOM_NONE;
            }
            DSLOG_INFO("Persistent VideoDevice DFC read: %s", _ZoomSettings.c_str());
            
            if (profileType == TV) {
                DSLOG_INFO("TV Profile - setting persistent zoom mode");
                dsHdmiInSelectZoomMode(srv_dfc);
            }
        } catch (...) {
            DSLOG_INFO("Exception in Getting the Zoom settings on Startup");
        }
        
        try {
            std::string _hdr_setting("false");
            _hdr_setting = device::HostPersistence::getInstance().getProperty("VideoDevice.forceHDRDisabled", _hdr_setting);
            if (_hdr_setting.compare("false") == 0) {
                force_disable_hdr = false;
            } else {
                force_disable_hdr = true;
                DSLOG_INFO("HDR support in disabled configuration");
            }
        } catch (...) {
            DSLOG_INFO("Exception in getting force-disable-HDR setting at start up");
        }
        
        EXIT_LOG;
    }

    // Static callback functions for DS HAL integration - following HdmiIn pattern
    static void VideoDeviceFramerateStatusPreChangeCB(unsigned int inputStatus)
    {
        DSLOG_INFO(" inputStatus=%u", inputStatus);
        
        // Call the stored global callback if available
        std::string framerate = std::to_string(inputStatus);
        g_VideoDeviceDisplayFrameratePreChangeCallback.Invoke(std::move(framerate));
    }

    static void VideoDeviceFramerateStatusPostChangeCB(unsigned int inputStatus)
    {
        DSLOG_INFO(" inputStatus=%u", inputStatus);
        
        // Call the stored global callback if available
        std::string framerate = std::to_string(inputStatus);
        g_VideoDeviceDisplayFrameratePostChangeCallback.Invoke(std::move(framerate));
    }

    // DS HAL Callback Registration Functions - following exact pattern from dsVideoDevice.c
    static dsError_t VideoDeviceRegisterFrameratePreChangeCB(dsRegisterFrameratePreChangeCB_t cbFunc)
    {
        dsError_t eRet = dsERR_GENERAL;
        DSLOG_INFO(" Registering framerate pre-change callback");
        
        typedef dsError_t (*_dsFramerateStatusPreChangeCB_t)(dsRegisterFrameratePreChangeCB_t CBFunc);
        static _dsFramerateStatusPreChangeCB_t frameratePreChangeCB = 0;
        
        if (frameratePreChangeCB == 0) {
            frameratePreChangeCB = (_dsFramerateStatusPreChangeCB_t) resolve(RDK_DSHAL_NAME, "dsRegisterFrameratePreChangeCB");
            if (frameratePreChangeCB == 0) {
                DSLOG_INFO("dsRegisterFrameratePreChangeCB is not defined");
            } else {
                DSLOG_INFO("dsRegisterFrameratePreChangeCB loaded");
            }
        }
        
        if (frameratePreChangeCB) {
            eRet = frameratePreChangeCB(cbFunc);
            if (dsERR_NONE == eRet) {
                DSLOG_INFO("Framerate pre-change callback registered successfully");
            } else {
                DSLOG_ERR("Failed to register framerate pre-change callback: %d", eRet);
            }
        }
        
        return eRet;
    }

    static dsError_t VideoDeviceRegisterFrameratePostChangeCB(dsRegisterFrameratePostChangeCB_t cbFunc)
    {
        dsError_t eRet = dsERR_GENERAL;
        DSLOG_INFO(" Registering framerate post-change callback");
        
        typedef dsError_t (*_dsFramerateStatusPostChangeCB_t)(dsRegisterFrameratePostChangeCB_t CBFunc);
        static _dsFramerateStatusPostChangeCB_t frameratePostChangeCB = 0;
        
        if (frameratePostChangeCB == 0) {
            frameratePostChangeCB = (_dsFramerateStatusPostChangeCB_t) resolve(RDK_DSHAL_NAME, "dsRegisterFrameratePostChangeCB");
            if (frameratePostChangeCB == 0) {
                DSLOG_INFO("dsRegisterFrameratePostChangeCB is not defined");
            } else {
                DSLOG_INFO("dsRegisterFrameratePostChangeCB loaded");
            }
        }
        
        if (frameratePostChangeCB) {
            eRet = frameratePostChangeCB(cbFunc);
            if (dsERR_NONE == eRet) {
                DSLOG_INFO("Framerate post-change callback registered successfully");
            } else {
                DSLOG_ERR("Failed to register framerate post-change callback: %d", eRet);
            }
        }
        
        return eRet;
    }

private:
    
    // Helper methods for DS VideoDevice HAL conversion
    dsVideoZoom_t convertVideoDeviceZoom(const VideoDeviceZoom zoom)
    {
        switch (zoom) {
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_NONE:
                return dsVIDEO_ZOOM_NONE;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL:
                return dsVIDEO_ZOOM_FULL;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_LB_16_9:
                return dsVIDEO_ZOOM_LB_16_9;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_LB_14_9:
                return dsVIDEO_ZOOM_LB_14_9;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_CCO:
                return dsVIDEO_ZOOM_CCO;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_PAN_SCAN:
                return dsVIDEO_ZOOM_PAN_SCAN;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_LB_2_21_1_ON_4_3:
                return dsVIDEO_ZOOM_LB_2_21_1_ON_4_3;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_LB_2_21_1_ON_16_9:
                return dsVIDEO_ZOOM_LB_2_21_1_ON_16_9;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_PLATFORM:
                return dsVIDEO_ZOOM_PLATFORM;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_16_9_ZOOM:
                return dsVIDEO_ZOOM_16_9_ZOOM;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_PILLARBOX_4_3:
                return dsVIDEO_ZOOM_PILLARBOX_4_3;
            case VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_WIDE_4_3:
                return dsVIDEO_ZOOM_WIDE_4_3;
            default:
                return dsVIDEO_ZOOM_FULL;
        }
    }

    VideoDeviceZoom convertDSVideoZoom(const dsVideoZoom_t dsZoom)
    {
        switch (dsZoom) {
            case dsVIDEO_ZOOM_NONE:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_NONE;
            case dsVIDEO_ZOOM_FULL:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL;
            case dsVIDEO_ZOOM_LB_16_9:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_LB_16_9;
            case dsVIDEO_ZOOM_LB_14_9:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_LB_14_9;
            case dsVIDEO_ZOOM_CCO:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_CCO;
            case dsVIDEO_ZOOM_PAN_SCAN:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_PAN_SCAN;
            case dsVIDEO_ZOOM_LB_2_21_1_ON_4_3:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_LB_2_21_1_ON_4_3;
            case dsVIDEO_ZOOM_LB_2_21_1_ON_16_9:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_LB_2_21_1_ON_16_9;
            case dsVIDEO_ZOOM_PLATFORM:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_PLATFORM;
            case dsVIDEO_ZOOM_16_9_ZOOM:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_16_9_ZOOM;
            case dsVIDEO_ZOOM_PILLARBOX_4_3:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_PILLARBOX_4_3;
            case dsVIDEO_ZOOM_WIDE_4_3:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_WIDE_4_3;
            default:
                return VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL;
        }
    }

    dsVideoCodingFormat_t convertVideoCodecToDSFormat(const VideoDeviceCodec codec)
    {
        switch (codec) {
            case VideoDeviceCodec::DS_VIDEO_CODEC_MPEGHPART2:
                return dsVIDEO_CODEC_MPEGHPART2;
            case VideoDeviceCodec::DS_VIDEO_CODEC_MPEG4PART10:
                return dsVIDEO_CODEC_MPEG4PART10;
            case VideoDeviceCodec::DS_VIDEO_CODEC_MPEG2:
                return dsVIDEO_CODEC_MPEG2;
            default:
                return dsVIDEO_CODEC_MPEGHPART2;
        }
    }

    IDeviceSettingsVideoCodecProfileSupportIterator* createCodecInfoIterator(const dsVideoCodecInfo_t& info)
    {
        std::vector<VideoDeviceCodecProfileSupport> profiles;
        const unsigned int count = std::min(info.num_entries, static_cast<unsigned int>(sizeof(info.entries) / sizeof(info.entries[0])));
        for (unsigned int i = 0; i < count; i++) {
            VideoDeviceCodecProfileSupport entry;
            entry.profile = static_cast<VideoDeviceCodecHEVCProfile>(info.entries[i].profile);
            entry.level = info.entries[i].level;
            profiles.push_back(entry);
        }

        DSLOG_INFO(" created iterator with %zu codec profile entries", profiles.size());
        using CodecProfileIterator = WPEFramework::RPC::IteratorType<IDeviceSettingsVideoCodecProfileSupportIterator>;
        return WPEFramework::Core::Service<CodecProfileIterator>::Create<IDeviceSettingsVideoCodecProfileSupportIterator>(profiles);
    }
};