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
#include "dCompositeIn.h"
#include "dsCompositeIn.h"
#include "dsError.h"
#include "dsUtl.h"
#include "dsTypes.h"
#include "dsError.h"
#include "dsCompositeIn.h"
#include "dsDisplay.h"


#include <WPEFramework/interfaces/IDeviceSettingsCompositeIn.h>
#include "DeviceSettingsTypes.h"

#ifndef RDK_DSHAL_NAME
#warning   "RDK_DSHAL_NAME is not defined"
#define RDK_DSHAL_NAME "RDK_DSHAL_NAME is not defined"
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

static int compositeIn_isInitialized = 0;
static int compositeIn_isPlatInitialized = 0;
static pthread_mutex_t dsCompositeInLock = PTHREAD_MUTEX_INITIALIZER;

// Static global callback functions for CompositeIn events - using WPE Framework types
static std::function<void(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort, const bool)> g_CompositeInHotPlugCallback;
static std::function<void(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort, const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus)> g_CompositeInSignalStatusCallback;
static std::function<void(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort, const bool)> g_CompositeInStatusCallback;
static std::function<void(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort, const WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution)> g_CompositeInVideoModeUpdateCallback;

class dCompositeInImpl : public hal::dCompositeIn::IPlatform {

    // delete copy constructor and assignment operator
    dCompositeInImpl(const dCompositeInImpl&) = delete;
    dCompositeInImpl& operator=(const dCompositeInImpl&) = delete;

public:
    dCompositeInImpl()
    {
        DSLOG_INFO("Constructor");
        getInstance() = this; // Set static instance for callback access
        InitialiseHAL();
    }

    virtual ~dCompositeInImpl()
    {
        DSLOG_INFO("Destructor");
        DeInitialiseHAL();
        getInstance() = nullptr; // Clear static instance
    }

    // Resolve method for dynamic library loading - following dHdmiInImpl.h pattern
    static void* resolve(const std::string& libName, const std::string& symbolName) {
        return WPEFramework::Plugin::DeviceSettingsHALLoader::ResolveSymbol(libName, symbolName);
    }

    // Singleton getInstance method - following VideoPort pattern
    static dCompositeInImpl*& getInstance()
    {
        static dCompositeInImpl* instance = nullptr;
        return instance;
    }

    void InitialiseHAL() override
    {
        
        // Check TV profile - following dHdmiInImpl.h pattern
        profileType = searchRdkProfile();
        DSLOG_INFO("profileType %d", profileType);
        
        if (TV != profileType) {
            DSLOG_INFO(" Not TV profile - profileType=%d", static_cast<int>(profileType));
            return;
        }
        
        if (!compositeIn_isPlatInitialized) {
            DSLOG_INFO("<dsCompositeIn> - TV Profile");
            
            // Initialize DS HAL CompositeIn using resolve() method - matches dsCompositeIn.c pattern
            typedef dsError_t (*dsCompositeInInit_t)(void);
            static dsCompositeInInit_t initFunc = nullptr;
            
            if (initFunc == nullptr) {
                initFunc = (dsCompositeInInit_t) resolve(RDK_DSHAL_NAME, "dsCompositeInInit");
            }

            if (initFunc) {
                DSLOG_INFO("Invoking dsCompositeInInit()");
                dsError_t eError = initFunc();
                if (dsERR_NONE != eError) {
                    DSLOG_ERR(" dsCompositeInInit failed with error: %d", eError);
                    return;
                }
                DSLOG_INFO(" dsCompositeInInit succeeded");
            } else {
                DSLOG_ERR(" dsCompositeInInit function not available");
                return;
            }
            
            // Load persistence values after successful initialization
            getPersistenceValue();
            
            compositeIn_isPlatInitialized = 1;
            DSLOG_INFO("completed: compositeIn_isPlatInitialized=%d, compositeIn_isInitialized=%d",
                    compositeIn_isPlatInitialized, compositeIn_isInitialized);
        }
    }

    void DeInitialiseHAL() override
    {
        
        if (TV != profileType) {
            DSLOG_INFO(" Not TV profile - profileType=%d", static_cast<int>(profileType));
            return;
        }
        
        if (compositeIn_isPlatInitialized) {
            compositeIn_isPlatInitialized--;
            if (!compositeIn_isPlatInitialized) {
                // Use resolve method for dsCompositeInTerm - matches dsCompositeIn.c _dsCompositeInTerm pattern
                typedef dsError_t (*dsCompositeInTerm_t)(void);
                static dsCompositeInTerm_t termFunc = nullptr;
                
                if (termFunc == nullptr) {
                    termFunc = (dsCompositeInTerm_t) resolve(RDK_DSHAL_NAME, "dsCompositeInTerm");
                }
                
                if (termFunc) {
                    DSLOG_INFO("Invoking dsCompositeInTerm()");
                    dsError_t eError = termFunc();
                    if (dsERR_NONE != eError) {
                        DSLOG_ERR(" dsCompositeInTerm failed with error: %d", eError);
                    }
                } else {
                    DSLOG_ERR(" dsCompositeInTerm function not available");
                }
            }
        }
        compositeIn_isInitialized = 0;
    }

    void setAllCallbacks(const CallbackBundle& bundle) override
    {
        DSLOG_INFO("dCompositeInImpl setAllCallbacks");
        
        if (!compositeIn_isInitialized) {
            // Set the global callback function pointers from CallbackBundle
            g_CompositeInHotPlugCallback = bundle.OnCompositeInHotPlug;
            g_CompositeInSignalStatusCallback = bundle.OnCompositeInSignalStatus;
            g_CompositeInStatusCallback = bundle.OnCompositeInStatus;
            g_CompositeInVideoModeUpdateCallback = bundle.OnCompositeInVideoModeUpdate;

            // Register HAL callbacks
            registerCompositeInEventCallbacks();
            
            compositeIn_isInitialized = 1;
            DSLOG_INFO("dCompositeInImpl setAllCallbacks: CompositeIn callbacks registered successfully");
        } else {
            DSLOG_INFO("dCompositeInImpl setAllCallbacks: CompositeIn already initialized, skipping callback registration");
        }
    }

    void getPersistenceValue() override
    {
        DSLOG_INFO("dCompositeInImpl getPersistenceValue - CompositeIn persistence loading");
        // Load any CompositeIn-specific persistence values here
        // This would be similar to VideoPort persistence loading but for CompositeIn settings
    }

    // Implementation of CompositeIn Platform interface methods
    uint32_t GetNrOfCompositeInputs(int32_t& nrCompositeInputs) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        
        pthread_mutex_lock(&dsCompositeInLock);
        
        // Use resolve method for dsCompositeInGetNumberOfInputs - matches dsCompositeIn.c pattern
        typedef dsError_t (*dsCompositeInGetNumberOfInputs_t)(uint8_t *nrCompositeInputs);
        static dsCompositeInGetNumberOfInputs_t func = 0;
        if (func == 0) {
            func = (dsCompositeInGetNumberOfInputs_t) resolve(RDK_DSHAL_NAME, "dsCompositeInGetNumberOfInputs");
        }
        
        if (func != 0) {
            uint8_t nrInputs = 0;
            dsError_t eError = func(&nrInputs);
            if (eError == dsERR_NONE) {
                nrCompositeInputs = static_cast<int32_t>(nrInputs);
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - nrCompositeInputs=%d", nrCompositeInputs);
            } else {
                DSLOG_ERR(" FAILED - dsCompositeInGetNumberOfInputs error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsCompositeInGetNumberOfInputs not available");
        }
        
        pthread_mutex_unlock(&dsCompositeInLock);
        return retCode;
    }

    uint32_t GetCompositeInStatus(CompositeInStatus& status) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        
        pthread_mutex_lock(&dsCompositeInLock);
        
        // Use resolve method for dsCompositeInGetStatus - matches dsCompositeIn.c pattern
        typedef dsError_t (*dsCompositeInGetStatus_t)(dsCompositeInStatus_t *inputStatus);
        static dsCompositeInGetStatus_t func = 0;
        if (func == 0) {
            func = (dsCompositeInGetStatus_t) resolve(RDK_DSHAL_NAME, "dsCompositeInGetStatus");
        }
        
        if (func != 0) {
            dsCompositeInStatus_t dsStatus;
            dsError_t eError = func(&dsStatus);
            if (eError == dsERR_NONE) {
                // Convert from DS types to WPE Framework types
                status.activePort = static_cast<CompositeInPort>(dsStatus.activePort);
                status.isPresented = dsStatus.isPresented;
                
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - activePort=%d, isPresented=%s",
                        static_cast<int>(status.activePort), status.isPresented ? "true" : "false");
            } else {
                DSLOG_ERR(" FAILED - dsCompositeInGetStatus error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsCompositeInGetStatus not available");
        }
        
        pthread_mutex_unlock(&dsCompositeInLock);
        return retCode;
    }

    uint32_t SelectCompositeInPort(const CompositeInPort port) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" port=%d", static_cast<int>(port));
        
        pthread_mutex_lock(&dsCompositeInLock);
        
        // Use resolve method for dsCompositeInSelectPort - matches dsCompositeIn.c pattern
        typedef dsError_t (*dsCompositeInSelectPort_t)(dsCompositeInPort_t port);
        static dsCompositeInSelectPort_t func = 0;
        if (func == 0) {
            func = (dsCompositeInSelectPort_t) resolve(RDK_DSHAL_NAME, "dsCompositeInSelectPort");
        }
        
        if (func != 0) {
            dsCompositeInPort_t dsPort = static_cast<dsCompositeInPort_t>(port);
            dsError_t eError = func(dsPort);
            if (eError == dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS - port=%d", static_cast<int>(port));
            } else {
                DSLOG_ERR(" FAILED - dsCompositeInSelectPort error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsCompositeInSelectPort not available");
        }
        
        pthread_mutex_unlock(&dsCompositeInLock);
        return retCode;
    }

    uint32_t ScaleCompositeInVideo(const CompositeInVideoRectangle videoRect) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" x=%d, y=%d, width=%d, height=%d",
                videoRect.x, videoRect.y, videoRect.width, videoRect.height);
        
        pthread_mutex_lock(&dsCompositeInLock);
        
        // Use resolve method for dsCompositeInScaleVideo - matches dsCompositeIn.c pattern
        typedef dsError_t (*dsCompositeInScaleVideo_t)(int x, int y, int width, int height);
        static dsCompositeInScaleVideo_t func = 0;
        if (func == 0) {
            func = (dsCompositeInScaleVideo_t) resolve(RDK_DSHAL_NAME, "dsCompositeInScaleVideo");
        }
        
        if (func != 0) {
            dsError_t eError = func(videoRect.x, videoRect.y, videoRect.width, videoRect.height);
            if (eError == dsERR_NONE) {
                retCode = WPEFramework::Core::ERROR_NONE;
                DSLOG_INFO(" SUCCESS");
            } else {
                DSLOG_ERR(" FAILED - dsCompositeInScaleVideo error=%d", eError);
            }
        } else {
            DSLOG_ERR(" FAILED - dsCompositeInScaleVideo not available");
        }
        
        pthread_mutex_unlock(&dsCompositeInLock);
        return retCode;
    }

    // Type conversion methods between DS HAL types and WPE Framework types
    static WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort convertToWPECompositeInPort(const CompositeInPort port)
    {
        return static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort>(port);
    }

    static CompositeInPort convertFromWPECompositeInPort(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort port)
    {
        return static_cast<CompositeInPort>(port);
    }

    static WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus convertToWPECompositeInSignalStatus(const CompositeInSignalStatus signalStatus)
    {
        return static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus>(signalStatus);
    }

    static CompositeInSignalStatus convertFromWPECompositeInSignalStatus(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus signalStatus)
    {
        return static_cast<CompositeInSignalStatus>(signalStatus);
    }

    static WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution convertToWPEDisplayVideoPortResolution(const DisplayVideoPortResolution resolution)
    {
        WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution wpeResolution;
        wpeResolution.name = resolution.name;
        wpeResolution.pixelResolution = static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayTVResolution>(resolution.pixelResolution);
        wpeResolution.aspectRatio = static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoAspectRatio>(resolution.aspectRatio);
        wpeResolution.frameRate = static_cast<WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayInVideoFrameRate>(resolution.frameRate);
        wpeResolution.interlaced = resolution.interlaced;
        return wpeResolution;
    }

    static DisplayVideoPortResolution convertFromWPEDisplayVideoPortResolution(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution resolution)
    {
        DisplayVideoPortResolution halResolution;
        halResolution.name = resolution.name;
        halResolution.pixelResolution = static_cast<DisplayTVResolution>(resolution.pixelResolution);
        halResolution.aspectRatio = static_cast<DisplayVideoAspectRatio>(resolution.aspectRatio); 
        halResolution.frameRate = static_cast<DisplayInVideoFrameRate>(resolution.frameRate);
        halResolution.interlaced = resolution.interlaced;
        return halResolution;
    }

    static WPEFramework::Exchange::IDeviceSettingsCompositeIn::VideoRectangle convertToWPEVideoRectangle(const CompositeInVideoRectangle rectangle)
    {
        WPEFramework::Exchange::IDeviceSettingsCompositeIn::VideoRectangle wpeRectangle;
        wpeRectangle.x = rectangle.x;
        wpeRectangle.y = rectangle.y;
        wpeRectangle.width = rectangle.width;
        wpeRectangle.height = rectangle.height;
        return wpeRectangle;
    }

    static CompositeInVideoRectangle convertFromWPEVideoRectangle(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::VideoRectangle rectangle)
    {
        CompositeInVideoRectangle halRectangle;
        halRectangle.x = rectangle.x;
        halRectangle.y = rectangle.y;
        halRectangle.width = rectangle.width;
        halRectangle.height = rectangle.height;
        return halRectangle;
    }

private:
    void registerCompositeInEventCallbacks()
    {
        
        // Register CompositeIn event callbacks using resolve method - matches dsCompositeIn.c pattern
        typedef dsError_t (*dsCompositeInRegisterConnectCB_t)(dsCompositeInConnectCB_t callback);
        typedef dsError_t (*dsCompositeInRegisterSignalChangeCB_t)(dsCompositeInSignalChangeCB_t callback);  
        typedef dsError_t (*dsCompositeInRegisterStatusChangeCB_t)(dsCompositeInStatusChangeCB_t callback);
        typedef dsError_t (*dsCompositeInRegisterVideoModeUpdateCB_t)(dsCompositeInVideoModeUpdateCB_t callback);
        
        static dsCompositeInRegisterConnectCB_t funcConnect = 0;
        static dsCompositeInRegisterSignalChangeCB_t funcSignal = 0;
        static dsCompositeInRegisterStatusChangeCB_t funcStatus = 0;
        static dsCompositeInRegisterVideoModeUpdateCB_t funcVideoMode = 0;
        
        if (funcConnect == 0) {
            funcConnect = (dsCompositeInRegisterConnectCB_t) resolve(RDK_DSHAL_NAME, "dsCompositeInRegisterConnectCB");
            funcSignal = (dsCompositeInRegisterSignalChangeCB_t) resolve(RDK_DSHAL_NAME, "dsCompositeInRegisterSignalChangeCB");
            funcStatus = (dsCompositeInRegisterStatusChangeCB_t) resolve(RDK_DSHAL_NAME, "dsCompositeInRegisterStatusChangeCB");
            funcVideoMode = (dsCompositeInRegisterVideoModeUpdateCB_t) resolve(RDK_DSHAL_NAME, "dsCompositeInRegisterVideoModeUpdateCB");
        }
        
        // dsCompositeIn.c registers each callback independently — a platform missing one
        // callback symbol must not prevent registering the others.
        if (funcConnect) {
            funcConnect(dsCompositeInConnectCallback);
            DSLOG_INFO(" dsCompositeInRegisterConnectCB SUCCESS");
        } else {
            DSLOG_ERR(" dsCompositeInRegisterConnectCB not available");
        }

        if (funcSignal) {
            funcSignal(dsCompositeInSignalChangeCallback);
            DSLOG_INFO(" dsCompositeInRegisterSignalChangeCB SUCCESS");
        } else {
            DSLOG_ERR(" dsCompositeInRegisterSignalChangeCB not available");
        }

        if (funcStatus) {
            funcStatus(dsCompositeInStatusChangeCallback);
            DSLOG_INFO(" dsCompositeInRegisterStatusChangeCB SUCCESS");
        } else {
            DSLOG_ERR(" dsCompositeInRegisterStatusChangeCB not available");
        }

        if (funcVideoMode) {
            funcVideoMode(dsCompositeInVideoModeUpdateCallback);
            DSLOG_INFO(" dsCompositeInRegisterVideoModeUpdateCB SUCCESS");
        } else {
            DSLOG_ERR(" dsCompositeInRegisterVideoModeUpdateCB not available");
        }
    }

    // Static callback functions to handle CompositeIn events from HAL
    static void dsCompositeInConnectCallback(dsCompositeInPort_t port, bool isPortConnected)
    {
        DSLOG_INFO(" port=%d, isPortConnected=%s", static_cast<int>(port), isPortConnected ? "true" : "false");
        
        if (g_CompositeInHotPlugCallback) {
            // Convert DS HAL type directly to WPE Framework type
            WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort wpePort = convertToWPECompositeInPort(static_cast<CompositeInPort>(port));
            g_CompositeInHotPlugCallback(wpePort, isPortConnected);
        }
    }

    static void dsCompositeInSignalChangeCallback(dsCompositeInPort_t port, dsCompInSignalStatus_t sigStatus)
    {
        DSLOG_INFO(" port=%d, sigStatus=%d", static_cast<int>(port), static_cast<int>(sigStatus));
        
        if (g_CompositeInSignalStatusCallback) {
            // Convert DS HAL types directly to WPE Framework types
            WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort wpePort = convertToWPECompositeInPort(static_cast<CompositeInPort>(port));
            WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus wpeSignalStatus = convertToWPECompositeInSignalStatus(static_cast<CompositeInSignalStatus>(sigStatus));
            g_CompositeInSignalStatusCallback(wpePort, wpeSignalStatus);
        }
    }

    static void dsCompositeInStatusChangeCallback(dsCompositeInStatus_t inputStatus)
    {
        DSLOG_INFO(" activePort=%d, isPresented=%s",
                static_cast<int>(inputStatus.activePort), inputStatus.isPresented ? "true" : "false");
        
        if (g_CompositeInStatusCallback) {
            // Convert DS HAL type directly to WPE Framework type
            WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort wpePort = convertToWPECompositeInPort(static_cast<CompositeInPort>(inputStatus.activePort));
            g_CompositeInStatusCallback(wpePort, inputStatus.isPresented);
        }
    }

    static void dsCompositeInVideoModeUpdateCallback(dsCompositeInPort_t port, dsVideoPortResolution_t videoResolution)
    {
        DSLOG_INFO(" port=%d", static_cast<int>(port));
        DSLOG_INFO("Video Mode: %s pixelResolution %d aspectRatio %d stereoScopicMode %d frameRate %d",
                videoResolution.name, videoResolution.pixelResolution, videoResolution.aspectRatio, 
                videoResolution.stereoScopicMode, videoResolution.frameRate);
        
        if (g_CompositeInVideoModeUpdateCallback) {
            // Convert DS HAL types to WPE Framework types
            WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort wpePort = convertToWPECompositeInPort(static_cast<CompositeInPort>(port));
            
            // Convert DS HAL dsVideoPortResolution_t to DisplayVideoPortResolution
            DisplayVideoPortResolution halResolution;
            halResolution.name = std::string(videoResolution.name);
            halResolution.pixelResolution = static_cast<DisplayTVResolution>(videoResolution.pixelResolution);
            halResolution.aspectRatio = static_cast<DisplayVideoAspectRatio>(videoResolution.aspectRatio);
            halResolution.stereoScopicMode = static_cast<DisplayInVideoStereoScopicMode>(videoResolution.stereoScopicMode);
            halResolution.frameRate = static_cast<DisplayInVideoFrameRate>(videoResolution.frameRate);
            halResolution.interlaced = videoResolution.interlaced;
            
            WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution wpeResolution = convertToWPEDisplayVideoPortResolution(halResolution);
            g_CompositeInVideoModeUpdateCallback(wpePort, wpeResolution);
        }
    }
};