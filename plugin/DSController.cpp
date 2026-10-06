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

#include "DSController.h"
#include "DSPwrEventListener.h"

#include <set>
#include <syscall.h>
#include <unistd.h>
#include <fcntl.h>

// C headers with built-in C++ protection
extern "C" {
#include "libIARM.h"
#include "libIBusDaemon.h"
#include "libIBus.h"
#include "iarmUtil.h"
#include "sysMgr.h"
#include "dsUtl.h"
#include "dsError.h"
#include "dsTypes.h"
#include "dsVideoPort.h"
#include "dsDisplay.h"
#include "dsAudio.h"
#include "rfcapi.h"
}

#include <glib.h>
// For glib APIs - conditional include
#ifdef GLIB_AVAILABLE
#include <glib.h>
#else
// Provide minimal glib-like definitions when glib is not available
typedef void* gpointer;
typedef int gboolean;
typedef unsigned int guint;
typedef struct _GMainLoop GMainLoop;

static inline GMainLoop* g_main_loop_new(void* context, gboolean is_running) { return nullptr; }
static inline void g_main_loop_run(GMainLoop* loop) {}
static inline void g_main_loop_quit(GMainLoop* loop) {}
static inline void g_main_loop_unref(GMainLoop* loop) {}
static inline gboolean g_main_loop_is_running(GMainLoop* loop) { return FALSE; }
static inline guint g_timeout_add_seconds(guint interval, gboolean (*function)(gpointer), gpointer data) { return 0; }
static inline gboolean g_source_remove(guint tag) { return FALSE; }
#endif

// DS HAL function declarations
/*extern "C" {
    bool dsGetHDMIDDCLineStatus(void);
}*/

using namespace std;

namespace WPEFramework {
namespace Plugin {

    DSController* DSController::_instance = nullptr;
    
    bool DSController::IsEUPlatform = false;
    char DSController::fallBackResolutionList[6][64];
    
    pthread_t DSController::_resolutionThreadID = 0;
    pthread_mutex_t DSController::_mutexLock;
    pthread_cond_t DSController::_mutexCond;
    guint DSController::_hotplugEventSrc = 0;
    volatile bool DSController::_dsMgr_thread_exit_flag = false;
    int DSController::_tuneReady = 0;
    int DSController::_initResolutionFlag = 0;
    int DSController::_resolutionRetryCount = 5;
    bool DSController::_hdcpAuthenticated = false;
    bool DSController::_ignoreEdid = false;
    bool DSController::_bootupFlagEnabled = true;
    dsDisplayEvent_t DSController::_displayEventStatus = dsDISPLAY_EVENT_MAX;
    
    // Platform configuration constants
    #define RES_MAX_LEN 64
    #define RES_MAX_COUNT 6
    #define DEFAULT_PROGRESSIVE_FPS "60"
    #define RESOLUTION_BASE_UHD     "2160p"
    #define RESOLUTION_BASE_FHD     "1080p"
    #define RESOLUTION_BASE_FHD_INT "1080i"
    #define RESOLUTION_BASE_HD      "720p"
    #define RESOLUTION_BASE_PAL     "576p"
    #define RESOLUTION_BASE_NTSC    "480p"
    #define EU_PROGRESSIVE_FPS  "50"
    #define EU_INTERLACED_FPS   "25"

    // Static Create function implementation
    DSController* DSController::Create(DeviceSettingsImp* deviceSettingsInstance) {
        return new DSController(deviceSettingsInstance);
    }

    DSController::DSController(DeviceSettingsImp* deviceSettingsInstance)
        : _deviceSettings(deviceSettingsInstance)
        , _pwrEventListener(nullptr)
        , _mainLoop(nullptr)
        , _easMode(0)
        , m_refCount(1)  // Initialize reference count
    {
        DSController::_instance = this;
        
        pthread_mutex_init(&_mutexLock, NULL);
        pthread_cond_init(&_mutexCond, NULL);
    }

    DSController::~DSController() {
        DSLOG_INFO("Destructor - Instance Address: %p", this);
        
        _dsMgr_thread_exit_flag = true;
        
        if (_mainLoop && g_main_loop_is_running(_mainLoop)) {
            g_main_loop_quit(_mainLoop);
        }
        
        pthread_mutex_lock(&_mutexLock);
        pthread_cond_signal(&_mutexCond);
        pthread_mutex_unlock(&_mutexLock);
        
        if (_resolutionThreadID != 0) {
            pthread_join(_resolutionThreadID, nullptr);
        }
        
        DeinitializeDeviceSettingsComponents();
        DeinitializePowerEventListener();
        
        pthread_mutex_destroy(&_mutexLock);
        pthread_cond_destroy(&_mutexCond);
        
        if (_mainLoop) {
            g_main_loop_unref(_mainLoop);
            _mainLoop = nullptr;
        }
        
    }

    DSController* DSController::instance(DSController* controller)
    {
        if (controller != nullptr) {
            _instance = controller;
        }
        return _instance;
    }

    // Migrated from DSMgr_Start
    uint32_t DSController::Start()
    {
        setupPlatformConfig();
        InitializeDeviceSettingsComponents();

        setvbuf(stdout, NULL, _IOLBF, 0);

        IARM_Bus_Init(IARM_BUS_DSMGR_PLUGIN_NAME);
        IARM_Bus_Connect();
        IARM_Bus_RegisterEvent(IARM_BUS_DSMGR_EVENT_MAX);

        Init();

        _initResolutionFlag = 1;

        int32_t hdmiHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_HDMI);
        if (hdmiHandle != 0 && _deviceSettings) {
            _deviceSettings->getIgnoreEDIDStatus(hdmiHandle, _ignoreEdid);
        }
        DSLOG_INFO("ResOverride DSController::Start _ignoreEdid: %d", _ignoreEdid);

        IARM_Bus_RegisterEventHandler(IARM_BUS_SYSMGR_NAME, IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE, _EventHandler);
        IARM_Bus_RegisterCall(IARM_BUS_COMMON_API_SysModeChange, _SysModeChange);

        // Initialize power event listener (migrated from dsMGR)
        // Note: service parameter will be passed separately via InitializePowerEventListener()
        _pwrEventListener = new DSPwrEventListener();
        DSLOG_INFO("DSPwrEventListener created: %p", _pwrEventListener);
        
        InitializeResolutionThread();
        
        _mainLoop = g_main_loop_new(NULL, FALSE);
        if(_mainLoop != NULL){
            g_timeout_add_seconds(300, HeartbeatMsg, _mainLoop); 
        } else {
            DSLOG_ERR("Fails to Create a main Loop for DS Manager");
        }
        
        FILE* fDSCtrptr = fopen("/opt/ddcDelay", "r");
        if (NULL != fDSCtrptr) {
            if (0 > fscanf(fDSCtrptr, "%d", &_resolutionRetryCount)) {
                DSLOG_ERR("Error: fscanf on ddcDelay failed");
            }
            fclose(fDSCtrptr);
        }
        
        /* Check TuneReady state and signal the resolution thread if already set.
         * Do NOT call SetVideoPortResolution() here — it involves a synchronous
         * GetDisplayEdid() (HDMI DDC read, 2-3s) that would block the WPEFramework
         * plugin activation thread, preventing dependent plugins from getting a
         * PluginInitializerService slot.
         *
         * The resolution thread (ResolutionThreadFunc) is already running and will
         * call SetVideoPortResolution() when it is woken by:
         *   - TuneReady IARM event  (IARM_BUS_SYSMGR_SYSSTATE_TUNEREADY)
         *   - HDMI hotplug          (OnDisplayHDMIHotPlug)
         *
         * If TuneReady is already set before we start, signal the resolution thread
         * now so it picks it up immediately without waiting for an event. */
        IARM_Bus_SYSMgr_GetSystemStates_Param_t tuneReadyParam;
        memset(&tuneReadyParam, 0, sizeof(tuneReadyParam));
        IARM_Bus_Call(IARM_BUS_SYSMGR_NAME, IARM_BUS_SYSMGR_API_GetSystemStates,
                     &tuneReadyParam, sizeof(tuneReadyParam));

        if (1 == tuneReadyParam.TuneReadyStatus.state) {
            DSLOG_INFO("TuneReady already set, signalling resolution thread");
            _tuneReady = 1;
            pthread_mutex_lock(&_mutexLock);
            _displayEventStatus = dsDISPLAY_EVENT_CONNECTED;
            pthread_cond_signal(&_mutexCond);
            pthread_mutex_unlock(&_mutexLock);
        }

        return Core::ERROR_NONE;
    }

    uint32_t DSController::Stop()
    {
        _dsMgr_thread_exit_flag = true;
        
        if(_mainLoop)
        {
            g_main_loop_quit(_mainLoop);
        }

        // TODO
        /*dsMgrDeinitPwrControllerEvt();
        PowerController_Term();*/
        
        Deinit();
        
        IARM_Bus_Disconnect();
        IARM_Bus_Term();
        
        return Core::ERROR_NONE;
    }
    
    void DSController::Loop()
    {
        if(_mainLoop)
        { 
            g_main_loop_run(_mainLoop);
        }
    }
    
    void DSController::InitializeResolutionThread()
    {
        pthread_mutex_init(&_mutexLock, NULL);
        if (pthread_cond_init(&_mutexCond, NULL) != 0) {
            DSLOG_ERR("Failed to create pthread_cond_init _mutexCond");
            return;
        }
        
        if (pthread_create(&_resolutionThreadID, NULL, ResolutionThreadFunc, NULL) != 0) {
            DSLOG_ERR("Failed pthread_create ResolutionThreadFunc");
            return;
        }
    }

    void DSController::InitializeDeviceSettingsComponents()
    {
        try {
            if (_deviceSettings) {
                _deviceSettings->Register("DSController", static_cast<IDisplayHDMIHotPlugNotification*>(this));
                _deviceSettings->Register("DSController", static_cast<IDisplayNotification*>(this));
                _deviceSettings->Register("DSController", static_cast<Exchange::IDeviceSettingsVideoPort::INotification*>(this));
            } else {
                DSLOG_ERR("Failed to get DeviceSettings implementation instance");
            }
        } catch (const std::exception& e) {
            DSLOG_ERR("Exception during DeviceSettings component initialization: %s", e.what());
        }
    }
    
    void DSController::DeinitializeDeviceSettingsComponents()
    {
        if (_deviceSettings) {
            _deviceSettings->Unregister(static_cast<IDisplayHDMIHotPlugNotification*>(this));
            _deviceSettings->Unregister(static_cast<IDisplayNotification*>(this));
            _deviceSettings->Unregister(static_cast<Exchange::IDeviceSettingsVideoPort::INotification*>(this));
        }
        
        _deviceSettings = nullptr;
    }

    void DSController::InitializePowerEventListener(PluginHost::IShell* service)
    {
        DSLOG_INFO("Service: %p", service);

        if (_pwrEventListener && service) {
            DSLOG_INFO("Initializing DSPwrEventListener with service");
            _pwrEventListener->Init(service);
        } else {
            DSLOG_ERR("Cannot initialize DSPwrEventListener - missing listener or service");
        }
    }
    
    void DSController::DeinitializePowerEventListener()
    {
        DSLOG_INFO(" called");
        
        if (_pwrEventListener) {
            DSLOG_INFO("Deinitializing and deleting DSPwrEventListener");
            _pwrEventListener->Deinit();
            delete _pwrEventListener;
            _pwrEventListener = nullptr;
        }
    }

    void DSController::Init()
    {
        DSLOG_INFO("Initializing Device Settings subsystems");
    }
    
    void DSController::Deinit()
    {
        DSLOG_INFO("Terminating Device Settings subsystems");
    }

// Helper methods using DeviceSettings components
    int32_t DSController::GetVideoPortHandle(dsVideoPortType_t port)
    {
        int32_t handle = 0;
        
        if (_deviceSettings) {
            VideoPortType vpType = static_cast<VideoPortType>(port);
            uint32_t result = _deviceSettings->GetVideoPort(vpType, 0, handle);
            if (result != Core::ERROR_NONE) {
                // INVALID_PARAM for unconfigured ports (e.g. COMPONENT on TV) is expected
                DSLOG_WARN("port type %d not available (result=%u)", port, result);
                handle = 0;
            }
        } else {
            DSLOG_ERR("DeviceSettings not initialized");
        }
        
        return handle;
    }
    
    bool DSController::IsHDMIConnected()
    {
        bool connected = false;
        
        if (_deviceSettings) {
            int32_t handle = GetVideoPortHandle(dsVIDEOPORT_TYPE_HDMI);
            if (handle != 0) {
                uint32_t result = _deviceSettings->IsVideoPortDisplayConnected(handle, connected);
                if (result != Core::ERROR_NONE) {
                    DSLOG_ERR("Failed to check connection status");
                    connected = false;
                }
            }
        } else {
            DSLOG_ERR("DeviceSettings not initialized");
        }
        
        return connected;
    }

    void* DSController::ResolutionThreadFunc(void *arg)
    {
        dsDisplayEvent_t edisplayEventStatusLocal = dsDISPLAY_EVENT_MAX;
        
        while (!_dsMgr_thread_exit_flag) {
            DSLOG_INFO("_DSMgrResnThreadFunc... wait for for HDMI or Tune Ready Events");
            
            pthread_mutex_lock(&_mutexLock);
            while (!_dsMgr_thread_exit_flag && _displayEventStatus == dsDISPLAY_EVENT_MAX) {
                pthread_cond_wait(&_mutexCond, &_mutexLock);
            }
            edisplayEventStatusLocal = _displayEventStatus;
            pthread_mutex_unlock(&_mutexLock);
            
            DSLOG_INFO("Setting Resolution On:: HDMI %s Event with TuneReady status = %d",
                   (edisplayEventStatusLocal == dsDISPLAY_EVENT_CONNECTED ? "Connect" : "Disconnect"),
                   _tuneReady);
            
            if (_hotplugEventSrc) {
                g_source_remove(_hotplugEventSrc);
                DSLOG_INFO("Cleared Hot Plug Event Time source %d", _hotplugEventSrc);
                _hotplugEventSrc = 0;
            }
            
            if ((1 == _tuneReady) && (dsDISPLAY_EVENT_CONNECTED == edisplayEventStatusLocal)) {
                if (_hdcpAuthenticated) {
                    if (_instance) {
                        _instance->SetVideoPortResolution();
                    }
                }
                if (_instance) {
                    _instance->SetAudioMode();
                }
            }
            else if ((1 == _tuneReady) && (dsDISPLAY_EVENT_DISCONNECTED == edisplayEventStatusLocal)) {
                _hdcpAuthenticated = false;
                if (_instance && _instance->isComponentPortPresent())
                {
                    _hotplugEventSrc = g_timeout_add_seconds((guint)5, SetResolutionHandler, _instance->_mainLoop);
                    DSLOG_INFO("Schedule a handler to set the resolution after 5 sec for %d time src..", _hotplugEventSrc);
                }
            }
            
            pthread_mutex_lock(&_mutexLock);
            _displayEventStatus = dsDISPLAY_EVENT_MAX;
            pthread_mutex_unlock(&_mutexLock);
        }
        
        return nullptr;
    }

    void DSController::SetVideoPortResolution()
    {
        DSLOG_INFO("Enter");
        
        int32_t hdmiHandle = 0;
        int32_t compHandle = 0;
        bool connected = false;
        
        hdmiHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_HDMI);
        if (hdmiHandle != 0) {
            usleep(100 * 1000);
            
            connected = IsHDMIConnected();
            if (_initResolutionFlag && connected) {
                #ifdef _INIT_RESN_SETTINGS
                int iCount = 0;
                while (iCount < _resolutionRetryCount) {
                    sleep(1);
                    if (dsGetHDMIDDCLineStatus()) {
                        break;
                    }
                    DSLOG_INFO("Waiting for HDMI DDC Line to be ready for resolution Change...");
                    iCount++;
                }
                #endif
            }
            
            if (connected) {
                DSLOG_INFO("Setting HDMI resolution..........");
                SetResolution(hdmiHandle, dsVIDEOPORT_TYPE_HDMI);
            } else {
                compHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_COMPONENT);
                
                if (0 != compHandle) {
                    DSLOG_INFO("Setting Component/Composite Resolution..........");
                    SetResolution(compHandle, dsVIDEOPORT_TYPE_COMPONENT);
                } else {
                    DSLOG_INFO("DSController: NULL Handle for component");
                    int32_t compositeHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_BB);
                    if (0 != compositeHandle) {
                        DSLOG_INFO("Setting BB Composite Resolution..........");
                        SetResolution(compositeHandle, dsVIDEOPORT_TYPE_BB);
                    } else {
                        DSLOG_INFO("DSController: NULL Handle for Composite");
                        int32_t rfHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_RF);
                        if (0 != rfHandle) {
                            DSLOG_INFO("Setting RF Resolution..........");
                            SetResolution(rfHandle, dsVIDEOPORT_TYPE_RF);
                        } else {
                            DSLOG_INFO("DSController: NULL Handle for RF");
                        }
                    }
                }
            }
        }
        
        DSLOG_INFO("Exit");
    }

    void DSController::SetResolution(int32_t handle, dsVideoPortType_t portType)
    {
        
        int32_t displayHandle = 0;
        int numResolutions = 0;
        bool isValidResolution = false;
        
        // Return if Handle is NULL
        if (handle == 0) {
            DSLOG_ERR("Got NULL Handle");
            return;
        }
        
        // Get the User Persisted Resolution Based on Handle
        VideoPortResolution presolution;
        if (_deviceSettings) {
            uint32_t result = _deviceSettings->GetVideoPortResolution(handle, presolution);
            if (result != Core::ERROR_NONE) {
                DSLOG_ERR("Failed to get persisted resolution");
                return;
            }
        }
        
        DSLOG_INFO("Got User Persisted Resolution - %s", presolution.name.c_str());
        
        if (portType == dsVIDEOPORT_TYPE_HDMI) {
            // Get The Display Handle
            if (_deviceSettings) {
                uint32_t result = _deviceSettings->GetDisplay(static_cast<DisplayPortType>(dsVIDEOPORT_TYPE_HDMI), 0, displayHandle);
                if (result == Core::ERROR_NONE && displayHandle != 0) {
                    // Get the EDID Display Handle
                    DisplayEDID edidData;
                    IDSVideoPortResolutionIterator* supportedResolutionList = nullptr;
                    
                    result = _deviceSettings->GetDisplayEdid(displayHandle, edidData, supportedResolutionList);
                    if (result == Core::ERROR_NONE) {
                        DumpHdmiEdidInfo(edidData);
                        numResolutions = edidData.numOfSupportedResolution;
                        DSLOG_INFO("numResolutions is %d", numResolutions);
                        
                        // If HDMI is connected and Low power Mode, TV might not transmit EDID information
                        // Change the Resolution in Next Hot plug. Do not set if TV is in DVI mode
                        if ((0 == numResolutions) || (!edidData.hdmiDeviceType)) {
                            DSLOG_ERR("Do not Set Resolution..The HDMI is not Ready !!");
                            DSLOG_ERR("numResolutions = %d edidData.hdmiDeviceType = %d !!", numResolutions, edidData.hdmiDeviceType);
                            return;
                        }

                        std::set<std::string> edidSupportedNames;
                        if (supportedResolutionList != nullptr) {
                            DisplayVideoPortResolution res;
                            while (supportedResolutionList->Next(res)) {
                                if (!res.name.empty()) {
                                    edidSupportedNames.insert(res.name);
                                }
                            }
                            supportedResolutionList->Release();
                            supportedResolutionList = nullptr;
                        }
                        DSLOG_INFO("EDID supported resolution count from iterator: %zu", edidSupportedNames.size());

                        auto isResInEdid = [&](const char* name) -> bool {
                            if (!name || name[0] == '\0') return false;
                            bool found = edidSupportedNames.count(std::string(name)) > 0;
                            if (found) DSLOG_INFO("Resolution supported in EDID: %s", name);
                            return found;
                        };

                        // First check if persisted resolution is directly supported
                        if (isResInEdid(presolution.name.c_str())) {
                            isValidResolution = true;
                            DSLOG_INFO("Persisted resolution %s is directly supported", presolution.name.c_str());
                        }
                        
                        // If resolution with 50Hz not supported, check for same resolution with 60Hz (EU fallback)
                        if (!isValidResolution && IsEUPlatform) {
                            char secResn[RES_MAX_LEN];
                            // Get secondary resolution based on presolution
                            if (getSecondaryResolution(const_cast<char*>(presolution.name.c_str()), secResn)) {
                                if (isResInEdid(secResn)) {
                                    DSLOG_INFO("Got Secondary Resolution - %s", secResn);
                                    isValidResolution = true;
                                    // Update presolution to use the secondary resolution
                                    presolution.name = std::string(secResn);
                                }
                            }
                        }
                        
                        // Fallback to next best resolution
                        if (!isValidResolution) {
                            int index = 0;
                            char baseResn[RES_MAX_LEN], fbResn[RES_MAX_LEN];
                            parseResolution(presolution.name.c_str(), baseResn);
                            int fNumResolutions = sizeof(fallBackResolutionList) / sizeof(fallBackResolutionList[0]);
                            
                            // Find index of base resolution in fallback list
                            for (int i = 0; i < fNumResolutions; i++) {
                                if (strcmp(fallBackResolutionList[i], baseResn) == 0) {
                                    index = i;
                                    break;
                                }
                            }
                            
                            // Try each fallback resolution in order
                            for (int i = index + 1; i < fNumResolutions; i++) {
                                if (IsEUPlatform) {
                                    getFallBackResolution(fallBackResolutionList[i], fbResn, 1); // EU fps
                                    DSLOG_INFO("Check next resolution: %s", fbResn);
                                    if (isResInEdid(fbResn)) {
                                        isValidResolution = true;
                                    }
                                }
                                if (!isValidResolution) {
                                    getFallBackResolution(fallBackResolutionList[i], fbResn, 0); // default fps
                                    DSLOG_INFO("Check next resolution: %s", fbResn);
                                    if (isResInEdid(fbResn)) {
                                        isValidResolution = true;
                                    }
                                }
                                if (isValidResolution) {
                                    DSLOG_INFO("Got Next Best Resolution - %s", fbResn);
                                    // Update presolution to use the fallback resolution
                                    presolution.name = std::string(fbResn);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        } else if (portType == dsVIDEOPORT_TYPE_COMPONENT || portType == dsVIDEOPORT_TYPE_BB || portType == dsVIDEOPORT_TYPE_RF) {
            // Set the Component / Composite Resolution
            DSLOG_INFO("Setting resolution for non-HDMI port type: %d", portType);
            isValidResolution = true; // Assume valid for component/composite
        }
        
        // Set The Video Port Resolution if valid
        if (isValidResolution && _deviceSettings) {
            uint32_t result = _deviceSettings->SetVideoPortResolution(handle, presolution, false, false);
            if (result != Core::ERROR_NONE) {
                DSLOG_ERR("Failed to set resolution");
            } else {
                DSLOG_INFO("Setting resolution to: %s", presolution.name.c_str());
            }
        } else {
            DSLOG_ERR("Failed to find any valid resolution!");
        }
        
    }

    void DSController::SetAudioMode()
    {
        
        if (_easMode == 1) { // IARM_BUS_SYS_MODE_EAS
            DSLOG_INFO("EAS In progress..Do not Modify Audio");
            return;
        }
        
        if (!_deviceSettings) {
            DSLOG_ERR("DeviceSettings not initialized");
            return;
        }
        
        // Get supported audio port types - for now use common types
        AudioPortType supportedPortTypes[] = {AudioPortType::AUDIO_PORT_TYPE_SPDIF, AudioPortType::AUDIO_PORT_TYPE_HDMI, AudioPortType::AUDIO_PORT_TYPE_SPEAKER};
        int numPorts = sizeof(supportedPortTypes) / sizeof(supportedPortTypes[0]);
        
        for (int i = 0; i < numPorts; i++) {
            int32_t handle = 0;
            uint32_t result = _deviceSettings->GetAudioPort(supportedPortTypes[i], 0, handle);
            if (result != Core::ERROR_NONE || handle == 0) {
                continue;
            }
            
            AudioStereoMode currentMode;
            result = _deviceSettings->GetStereoMode(handle, currentMode, false);
            if (result != Core::ERROR_NONE) {
                continue;
            }
            
            if (supportedPortTypes[i] == AudioPortType::AUDIO_PORT_TYPE_HDMI) {
                // Check if HDMI is connected
                int32_t vHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_HDMI);
                bool connected = false;
                bool isSurround = false;
                
                if (vHandle != 0 && _deviceSettings) {
                    _deviceSettings->IsVideoPortDisplayConnected(vHandle, connected);
                }
                
                if (!connected) {
                    DSLOG_INFO("HDMI Not Connected ..Do not Set Audio on HDMI !!!");
                    continue;
                }
                
                int32_t autoMode = 0;
                result = _deviceSettings->GetStereoAuto(handle, autoMode);
                if (result == Core::ERROR_NONE && autoMode) {
                    // If auto, then force surround
                    currentMode = AudioStereoMode::AUDIO_STEREO_SURROUND;
                }
                
                // Assume surround is supported
                isSurround = true;
                
                if (!isSurround) {
                    // If Surround not supported, then force Stereo
                    currentMode = AudioStereoMode::AUDIO_STEREO_STEREO;
                    DSLOG_INFO("Surround mode not Supported on HDMI ..Set Stereo");
                }
            }
            
            DSLOG_INFO("Audio mode for audio port %d is : %d", static_cast<int>(supportedPortTypes[i]), static_cast<int>(currentMode));
            _deviceSettings->SetStereoMode(handle, currentMode, false);
        }
        
    }

    void DSController::SetEASAudioMode()
    {
        
        if (_easMode != 1) { // IARM_BUS_SYS_MODE_EAS
            DSLOG_INFO("EAS Not In progress..Do not Modify Audio");
            return;
        }
        
        if (!_deviceSettings) {
            DSLOG_ERR("DeviceSettings not initialized");
            return;
        }
        
        // Get supported audio port types - for now use common types
        AudioPortType supportedPortTypes[] = {AudioPortType::AUDIO_PORT_TYPE_SPDIF, AudioPortType::AUDIO_PORT_TYPE_HDMI, AudioPortType::AUDIO_PORT_TYPE_SPEAKER};
        int numPorts = sizeof(supportedPortTypes) / sizeof(supportedPortTypes[0]);
        
        for (int i = 0; i < numPorts; i++) {
            int32_t handle = 0;
            uint32_t result = _deviceSettings->GetAudioPort(supportedPortTypes[i], 0, handle);
            if (result != Core::ERROR_NONE || handle == 0) {
                continue;
            }
            
            AudioStereoMode currentMode;
            result = _deviceSettings->GetStereoMode(handle, currentMode, false);
            if (result != Core::ERROR_NONE) {
                continue;
            }
            
            if (currentMode == AudioStereoMode::AUDIO_STEREO_PASSTHROUGH) {
                // In EAS, fallback to Stereo
                currentMode = AudioStereoMode::AUDIO_STEREO_STEREO;
            }
            
            DSLOG_INFO("EAS Audio mode for audio port %d is : %d", static_cast<int>(supportedPortTypes[i]), static_cast<int>(currentMode));
            _deviceSettings->SetStereoMode(handle, currentMode, false);
        }
        
    }

    void DSController::SetBackgroundColor(dsVideoBackgroundColor_t color)
    {
        
        // Get the HDMI Video Port Handle
        int32_t hdmiHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_HDMI);
        
        if (hdmiHandle != 0 && _deviceSettings) {
            VideoBackgroundColor bgColor = static_cast<VideoBackgroundColor>(color);
            uint32_t result = _deviceSettings->SetBackgroundColor(hdmiHandle, bgColor);
            if (result != Core::ERROR_NONE) {
                DSLOG_ERR("Failed to set background color");
            }
        }
        
    }

    void DSController::DumpHdmiEdidInfo(const DisplayEDID& edidData)
    {
        DSLOG_INFO("Connected HDMI Display Device Info");

        if (!edidData.monitorName.empty())
            DSLOG_INFO("HDMI Monitor Name is %s", edidData.monitorName.c_str());
        DSLOG_INFO("HDMI Manufacturing ID is %d", edidData.serialNumber);
        DSLOG_INFO("HDMI Product Code is %d", edidData.productCode);
        DSLOG_INFO("HDMI Device Type is %s", edidData.hdmiDeviceType ? "HDMI" : "DVI");
        DSLOG_INFO("HDMI Sink Device %s a Repeater", edidData.isRepeater ? "is" : "is not");
        DSLOG_INFO("HDMI Physical Address is %d:%d:%d:%d",
                edidData.physicalAddressA, edidData.physicalAddressB,
                edidData.physicalAddressC, edidData.physicalAddressD);
        
    }

    void DSController::ScheduleEdidDump()
    {
        // Schedule EDID dump after 1 second using GLib
        g_timeout_add_seconds((guint)1, DumpEdidOnChecksumDiff, NULL);
    }
    
    bool DSController::isEUPlatform()
    {
        char line[256];
        bool isEUflag = false;
        const char* devPropPath = "/etc/device.properties";
        char deviceProp[15] = "FRIENDLY_ID";
        const char* USRegion = " US";
        
        FILE *file = fopen(devPropPath, "r");
        if (file == NULL) {
            DSLOG_ERR("Unable to open file %s", devPropPath);
            return false;
        }
        
        while (fgets(line, sizeof(line), file)) {
            if (strstr(line, deviceProp) != NULL) {
                if (strstr(line, USRegion) != NULL) {
                    DSLOG_INFO("Detected US region: %s, isEUflag:%d", line, isEUflag);
                } else { // EU - UK/IT/DE
                    isEUflag = true;
                    DSLOG_INFO("Detected EU region: %s, isEUflag:%d", line, isEUflag);
                }
                break;
            }
        }
        fclose(file);
        return isEUflag;
    }
    
    void DSController::setupPlatformConfig()
    {
        const char* resList[] = {"2160p","1080p","1080i","720p","576p","480p"};
        int count = 0, n = sizeof(resList) / sizeof(resList[0]);
        
        IsEUPlatform = isEUPlatform();
        
        for (int i = 0; i < n; i++) {
            // Include 576p for EU only
            if ((strstr(resList[i], "576p") != NULL) && !IsEUPlatform) {
                continue;
            }
            if (count < RES_MAX_COUNT) {
                snprintf(fallBackResolutionList[count], RES_MAX_LEN, "%s", resList[i]);
                DSLOG_INFO("Fallback resolution[%d]: %s", count, fallBackResolutionList[count]);
                count++;
            } else {
                break;
            }
        }
    }
    
    bool DSController::getSecondaryResolution(char* res, char *secRes)
    {
        bool ret = true;
        
        if (strstr(res, RESOLUTION_BASE_HD) != NULL) {
            snprintf(secRes, RES_MAX_LEN, "%s", RESOLUTION_BASE_HD); // 720p
        } else if (strstr(res, RESOLUTION_BASE_FHD) != NULL) {
            snprintf(secRes, RES_MAX_LEN, "%s%s", RESOLUTION_BASE_FHD, DEFAULT_PROGRESSIVE_FPS); // 1080p60
        } else if (strstr(res, RESOLUTION_BASE_FHD_INT) != NULL) {
            snprintf(secRes, RES_MAX_LEN, "%s", RESOLUTION_BASE_FHD_INT); // 1080i
        } else if (strstr(res, RESOLUTION_BASE_UHD) != NULL) {
            snprintf(secRes, RES_MAX_LEN, "%s%s", RESOLUTION_BASE_UHD, DEFAULT_PROGRESSIVE_FPS); // 2160p60
        } else {
            ret = false; // For other resolutions 480p 576p
        }
        
        DSLOG_INFO("Secondary resolution for %s: %s (ret=%d)", res, secRes, ret);
        return ret;
    }
    
    void DSController::parseResolution(const char* pResn, char* bResn)
    {
        char tmpResn[RES_MAX_LEN];
        int len = 0;
        
        snprintf(tmpResn, sizeof(tmpResn), "%s", pResn);
        char *token = strtok(tmpResn, "ip");
        strncpy(bResn, token, RES_MAX_LEN);
        len = strlen(bResn);
        
        if (strchr(pResn, 'i') != NULL) {
            snprintf(bResn + len, RES_MAX_LEN - len, "%s", "i");  // Append 'i'
        } else if (strchr(pResn, 'p') != NULL) {
            snprintf(bResn + len, RES_MAX_LEN - len, "%s", "p");  // Append 'p'
        }
        
        DSLOG_INFO("Parsed resolution from %s to %s", pResn, bResn);
    }
    
    void DSController::getFallBackResolution(char* Resn, char *fbResn, int flag)
    {
        char tmpResn[RES_MAX_LEN];
        snprintf(tmpResn, RES_MAX_LEN, "%s", Resn);
        int len = strlen(tmpResn);
        
        if (flag) { // EU
            if ((strcmp(Resn, RESOLUTION_BASE_UHD) == 0) || 
                (strcmp(Resn, RESOLUTION_BASE_FHD) == 0) || 
                (strcmp(Resn, RESOLUTION_BASE_HD) == 0)) {
                snprintf(tmpResn + len, sizeof(tmpResn) - len, "%s", EU_PROGRESSIVE_FPS);  // 2160p50, 1080p50, 720p50
            } else if (strcmp(Resn, RESOLUTION_BASE_FHD_INT) == 0) {
                snprintf(tmpResn + len, sizeof(tmpResn) - len, "%s", EU_INTERLACED_FPS);  // 1080i25
            } else {
                // do nothing for 576p, 480p
            }
        } else { // US
            if ((strcmp(Resn, RESOLUTION_BASE_UHD) == 0) || 
                (strcmp(Resn, RESOLUTION_BASE_FHD) == 0)) {
                snprintf(tmpResn + len, sizeof(tmpResn) - len, "%s", DEFAULT_PROGRESSIVE_FPS);  // 2160p60, 1080p60
            }
        }
        
        snprintf(fbResn, RES_MAX_LEN, "%s", tmpResn);
        DSLOG_INFO("Fallback resolution for %s (EU=%d): %s", Resn, flag, fbResn);
    }
    
    bool DSController::isResolutionSupported(dsDisplayEDID_t *edidData, int numResolutions, 
                                     int pNumResolutions, char *Resn, int* index)
    {
        bool supported = false;
        dsVideoPortResolution_t *setResn = NULL;
        
        for (int i = numResolutions - 1; i >= 0; i--) {
            setResn = &(edidData->suppResolutionList[i]);
            if (strcmp(setResn->name, Resn) == 0) {
                // Check if platform supports this resolution
                // Note: kResolutions would need to be defined or passed as parameter
                // For now, we'll mark as supported if found in EDID
                DSLOG_INFO("Resolution supported in EDID: %s", Resn);
                supported = true;
                *index = i;
                break;
            }
        }
        
        return supported;
    }

    // Static callback functions
    gboolean DSController::HeartbeatMsg(gpointer data)
    {
        DSLOG_INFO("I-ARM BUS DS Mgr: HeartBeat ping.");
        return TRUE;
    }
    
    gboolean DSController::SetResolutionHandler(gpointer data)
    {
        DSLOG_INFO("Set Video Resolution after delayed time ..");
        if (_instance) {
            _instance->SetVideoPortResolution();
            _instance->_hotplugEventSrc = 0;
        }
        return FALSE;
    }
    
    gboolean DSController::DumpEdidOnChecksumDiff(gpointer data)
    {
        DSLOG_INFO("dumpEdidOnChecksumDiff HDMI-EDID Dump>>>>>>>>>>>>>>");
        
        if (_instance && _instance->_deviceSettings) {
            int32_t displayHandle = 0;
            uint32_t result = _instance->_deviceSettings->GetDisplay(static_cast<DisplayPortType>(dsVIDEOPORT_TYPE_HDMI), 0, displayHandle);
            
            if (result == Core::ERROR_NONE && displayHandle != 0) {
                static int cached_EDID_checksum = 0;
                int current_EDID_checksum = 0;
                
                uint8_t edidBytes[512];
                uint16_t length = sizeof(edidBytes);
                
                result = _instance->_deviceSettings->GetDisplayEdidBytes(displayHandle, edidBytes, length);
                if (result == Core::ERROR_NONE && length > 0 && length <= 512) {
                    for (int i = 0; i < (length / 128); i++)
                        current_EDID_checksum += edidBytes[(i+1)*128 - 1];
                    
                    if ((cached_EDID_checksum == 0) || (current_EDID_checksum != cached_EDID_checksum)) {
                        cached_EDID_checksum = current_EDID_checksum;
                        DSLOG_INFO("HDMI-EDID Dump detected changes");
                    }
                }
            }
        }
        
        return false;
    }

    void DSController::EventHandler(const char *owner, int eventId, void *data, size_t len)
    {
        
        // Handle only Sys Manager Events
        if (strcmp(owner, IARM_BUS_SYSMGR_NAME) == 0) {
            // Only handle state events
            if (eventId != IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE) return;
            
            IARM_Bus_SYSMgr_EventData_t* sysEventData = (IARM_Bus_SYSMgr_EventData_t*)data;
            IARM_Bus_SYSMgr_SystemState_t stateId = sysEventData->data.systemStates.stateId;
            int state = sysEventData->data.systemStates.state;
            DSLOG_INFO("invoked for stateid %d of state %d", stateId, state);

            switch (stateId) {
                case IARM_BUS_SYSMGR_SYSSTATE_TUNEREADY:
                    DSLOG_INFO("Tune Ready Events in DS Manager");
                    
                    if (0 == _tuneReady) {
                        _tuneReady = 1;
                        
                        // Set audio mode from persistent
                        SetAudioMode();
                        
                        // Un-block the Resolution Settings Thread
                        pthread_mutex_lock(&_mutexLock);
                        pthread_cond_signal(&_mutexCond);
                        pthread_mutex_unlock(&_mutexLock);
                    }
                    break;
                default:
                    break;
            }
        }
        
    }

    void DSController::SysModeChange(void *arg)
    {
        
        IARM_Bus_CommonAPI_SysModeChange_Param_t* param = (IARM_Bus_CommonAPI_SysModeChange_Param_t*)arg;
        int isNextEAS = 0; // IARM_BUS_SYS_MODE_NORMAL
        
        DSLOG_INFO("Recvd Sysmode Change::New mode --> %d, Old mode --> %d", param->newMode, param->oldMode);
        
        if ((param->newMode == IARM_BUS_SYS_MODE_EAS) ||
            (param->newMode == IARM_BUS_SYS_MODE_NORMAL)) {
            isNextEAS = param->newMode;
        } else {
            // Do not process any other mode change as of now for DS Manager
            return;
        }
        
        if ((_easMode == IARM_BUS_SYS_MODE_EAS) && (isNextEAS == IARM_BUS_SYS_MODE_NORMAL)) {
            _easMode = IARM_BUS_SYS_MODE_NORMAL;
            SetAudioMode();
        } else if ((_easMode == IARM_BUS_SYS_MODE_NORMAL) && (isNextEAS == IARM_BUS_SYS_MODE_EAS)) {
            // Change the Audio Mode to Stereo if Current Audio Setting is Passthrough
            _easMode = IARM_BUS_SYS_MODE_EAS;
            SetEASAudioMode();
        } else {
            // no op for no mode change
        }
        
    }
    
    // Static IARM event handlers
    void DSController::_EventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
    {
        if (_instance) {
            _instance->EventHandler(owner, eventId, data, len);
        }
    }
    
    IARM_Result_t DSController::_SysModeChange(void *arg)
    {
        if (_instance) {
            _instance->SysModeChange(arg);
        }
        return IARM_RESULT_SUCCESS;
    }
    
    // Display::INotification implementation - Only for HDMI hotplug events
    void DSController::OnDisplayRxSense(const DisplayEvent displayEvent) {
        DSLOG_INFO("displayEvent = %d", static_cast<int>(displayEvent));
    }
    
    void DSController::OnDisplayHDCPStatus(const int32_t hdcpStatus) {
        DSLOG_INFO("hdcpStatus = %d", hdcpStatus);
        HandleHDCPStatus(hdcpStatus);
    }

    void DSController::OnHDCPStatusChange(const VideoPortHdcpStatus hdcpStatus) {
        DSLOG_INFO("hdcpStatus = %d", static_cast<int>(hdcpStatus));
        HandleHDCPStatus(static_cast<int32_t>(hdcpStatus));
    }

    void DSController::HandleHDCPStatus(int32_t hdcpStatus) {
        IARM_Bus_SYSMgr_EventData_t HDCPeventData;
        HDCPeventData.data.systemStates.stateId = IARM_BUS_SYSMGR_SYSSTATE_HDCP_ENABLED;
        HDCPeventData.data.systemStates.state = 1;

        if (hdcpStatus == dsHDCP_STATUS_AUTHENTICATED) {
            DSLOG_INFO("Changed status to HDCP Authentication Pass !!!!!!!!");
            _hdcpAuthenticated = true;
            DSLOG_INFO("HDCP success - Cleared hotplug_event_src Time source %d and set resolution immediately", _hotplugEventSrc);

            if (_hotplugEventSrc) {
                g_source_remove(_hotplugEventSrc);
                _hotplugEventSrc = 0;
            }

            SetBackgroundColor(dsVIDEO_BGCOLOR_NONE);
            if ((!_ignoreEdid) || _bootupFlagEnabled) {
                SetVideoPortResolution();
                if (_bootupFlagEnabled)
                    _bootupFlagEnabled = false;
            }
            ScheduleEdidDump();
        } else if (hdcpStatus == dsHDCP_STATUS_AUTHENTICATIONFAILURE) {
            DSLOG_ERR("Changed status to HDCP Authentication Fail !!!!!!!!");
            HDCPeventData.data.systemStates.state = 0;
            SetBackgroundColor(dsVIDEO_BGCOLOR_BLUE);
            _hdcpAuthenticated = false;
            if (!_ignoreEdid) {
                SetVideoPortResolution();
            }
            ScheduleEdidDump();
        }
        DSLOG_INFO("Broadcasting IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE event for HDCP status: %d", HDCPeventData.data.systemStates.state);
        IARM_Bus_BroadcastEvent(IARM_BUS_SYSMGR_NAME, (IARM_EventId_t)IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE,
                                (void*)&HDCPeventData, sizeof(HDCPeventData));
    }

    void DSController::OnDisplayHDMIHotPlug(const DisplayEvent displayEvent) {
        DSLOG_INFO("displayEvent = %d", static_cast<int>(displayEvent));

        SetBackgroundColor(dsVIDEO_BGCOLOR_NONE);

        pthread_mutex_lock(&_mutexLock);
        _displayEventStatus = (displayEvent == DisplayEvent::DS_DISPLAY_EVENT_CONNECTED) ?
                              dsDISPLAY_EVENT_CONNECTED : dsDISPLAY_EVENT_DISCONNECTED;
        pthread_cond_signal(&_mutexCond);
        pthread_mutex_unlock(&_mutexLock);
    }

    // Helper methods implementation
    bool DSController::isComponentPortPresent()
    {
        int32_t compHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_COMPONENT);
        bool present = (compHandle != 0);
        
        if (!present) {
            // Also check for BB composite as fallback
            int32_t compositeHandle = GetVideoPortHandle(dsVIDEOPORT_TYPE_BB);
            present = (compositeHandle != 0);
        }
        
        DSLOG_INFO("%s", present ? "true" : "false");
        return present;
    }

} // namespace Plugin
} // namespace WPEFramework
