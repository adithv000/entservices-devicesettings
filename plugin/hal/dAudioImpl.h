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

#include "dAudio.h"
#include "DeviceSettingsTypes.h"

#include <interfaces/IDeviceSettingsAudio.h>

#include "dsAudio.h"
#include "dsError.h"
#include "dsTypes.h"
#include "dsUtl.h"
#include "DeviceSettingsTelemetry.h"
#include <core/core.h>
#include <com/com.h>

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <dlfcn.h>
#include <cstdio>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

// Fallback for DS HAL header sets predating the application audio config ABI; layout is frozen upstream.
#ifndef DS_MAX_APPLICATION_AUDIO_CONFIG_NAME_LEN
#define DS_MAX_APPLICATION_AUDIO_CONFIG_NAME_LEN 64
#define DS_MAX_APPLICATION_AUDIO_CONFIGS 64

typedef struct _dsApplicationAudioConfig_t {
    char configName[DS_MAX_APPLICATION_AUDIO_CONFIG_NAME_LEN];
} dsApplicationAudioConfig_t;

typedef struct _dsApplicationAudioConfigList_t {
    uint32_t size;
    uint32_t totalCount;
    uint32_t returnedCount;
    dsApplicationAudioConfig_t config[DS_MAX_APPLICATION_AUDIO_CONFIGS];
} dsApplicationAudioConfigList_t;
#endif

// Static global callback functions following HdmiIn pattern
static std::function<void(const AudioPortType, const uint32_t, const bool)> g_AudioOutHotPlugCallback;
static std::function<void(const AudioFormat)> g_AudioFormatUpdateCallback;
static std::function<void(const DolbyAtmosCapability, const bool)> g_DolbyAtmosCapabilitiesChangedCallback;
static std::function<void(const bool)> g_AssociatedAudioMixingChangedCallback;
static std::function<void(const int32_t)> g_AudioFaderControlChangedCallback;
static std::function<void(const std::string&)> g_AudioPrimaryLanguageChangedCallback;
static std::function<void(const std::string&)> g_AudioSecondaryLanguageChangedCallback;
static std::function<void(const AudioPortState)> g_AudioPortStateChangedCallback;
static std::function<void(const float)> g_AudioLevelChangedCallback;
static std::function<void(const AudioPortType, const AudioStereoMode)> g_AudioModeChangedCallback;

#ifdef IGNORE_EDID_LOGIC
static bool g_AudioHdmiAuto = false;
static bool g_AudioSpdifAuto = false;
static bool g_AudioHdmiArcAuto = false;
static dsAudioStereoMode_t g_HdmiAudioMode = dsAUDIO_STEREO_STEREO;
static dsAudioStereoMode_t g_SpdifAudioMode = dsAUDIO_STEREO_STEREO;
static dsAudioStereoMode_t g_HdmiArcAudioMode = dsAUDIO_STEREO_STEREO;
#else
static bool g_AudioHdmiAuto = true;
static bool g_AudioSpdifAuto = true;
static bool g_AudioHdmiArcAuto = true;
static dsAudioStereoMode_t g_HdmiAudioMode = dsAUDIO_STEREO_SURROUND;
static dsAudioStereoMode_t g_SpdifAudioMode = dsAUDIO_STEREO_SURROUND;
static dsAudioStereoMode_t g_HdmiArcAudioMode = dsAUDIO_STEREO_SURROUND;
#endif
static bool g_AudioSpeakerAuto = true;
static dsAudioStereoMode_t g_SpeakerAudioMode = dsAUDIO_STEREO_SURROUND;

// Legacy dsAudio.c parity: cache latest level and coalesce persistence writes.
static std::atomic<float> g_LastVolumeLevel(0.0f);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
static std::atomic<float> g_audioLevelCacheSpdif(0.0f);
static std::atomic<float> g_audioLevelCacheHdmi(0.0f);
static std::atomic<float> g_audioLevelCacheSpeaker(0.0f);
static std::atomic<float> g_audioLevelCacheHeadphone(0.0f);
static std::atomic<bool> g_audioLevelPersistThreadAlive(false);
static std::atomic<bool> g_audioLevelPersistPending(false);
static std::thread g_audioLevelPersistThread;
static std::mutex g_audioLevelPersistMutex;
static std::condition_variable g_audioLevelPersistCv;
#endif

/* LE (Loudness Equivalent) enable state — mirrors m_LEEnabled in dsAudio.c.
 * Loaded from persistence at init, updated on each EnableAudioLEConfig call. */
static bool m_LEEnabled = false;

using namespace WPEFramework::Exchange;

class dAudioImpl : public hal::dAudio::IPlatform {

private:
    // delete copy constructor and assignment operator
    dAudioImpl(const dAudioImpl&) = delete;
    dAudioImpl& operator=(const dAudioImpl&) = delete;
    
    bool _isInitialized;

    // Audio ducking state management
    bool _isDuckingInProgress;
    int32_t _volumeDuckingLevel;
    bool _muteStatus;
    
    // Audio port state tracking
    bool _audioPortEnabled[dsAUDIOPORT_TYPE_MAX];

#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
    static void persistAudioLevelIfChanged(const char* key, float& previousLevel, const float currentLevel)
    {
        if (currentLevel != previousLevel) {
            DSLOG_INFO("Persist coalesced audio level: %s=%f", key, currentLevel);
            device::HostPersistence::getInstance().persistHostProperty(key, std::to_string(currentLevel));
            previousLevel = currentLevel;
        }
    }

    static void runAudioLevelPersistThread()
    {
        float prevSpdif = g_audioLevelCacheSpdif.load();
        float prevHdmi = g_audioLevelCacheHdmi.load();
        float prevSpeaker = g_audioLevelCacheSpeaker.load();
        float prevHeadphone = g_audioLevelCacheHeadphone.load();

        DSLOG_INFO("Audio level persistence coalescing thread started");
        while (g_audioLevelPersistThreadAlive.load()) {
            std::unique_lock<std::mutex> lk(g_audioLevelPersistMutex);
            g_audioLevelPersistCv.wait(lk, [] {
                return g_audioLevelPersistPending.load() || !g_audioLevelPersistThreadAlive.load();
            });

            if (!g_audioLevelPersistThreadAlive.load()) {
                break;
            }

            g_audioLevelPersistPending.store(false);
            lk.unlock();

            // Legacy delay before persisting latest coalesced values.
            std::this_thread::sleep_for(std::chrono::seconds(3));

            try {
                persistAudioLevelIfChanged("SPDIF0.audio.Level", prevSpdif, g_audioLevelCacheSpdif.load());
                persistAudioLevelIfChanged("HDMI0.audio.Level", prevHdmi, g_audioLevelCacheHdmi.load());
                persistAudioLevelIfChanged("SPEAKER0.audio.Level", prevSpeaker, g_audioLevelCacheSpeaker.load());
                persistAudioLevelIfChanged("HEADPHONE0.audio.Level", prevHeadphone, g_audioLevelCacheHeadphone.load());
            } catch (...) {
                DSLOG_ERR("Exception while persisting coalesced audio levels");
            }
        }
        DSLOG_INFO("Audio level persistence coalescing thread stopped");
    }

    static void startAudioLevelPersistThread()
    {
        if (g_audioLevelPersistThreadAlive.load()) {
            return;
        }
        g_audioLevelPersistThreadAlive.store(true);
        g_audioLevelPersistPending.store(false);
        g_audioLevelPersistThread = std::thread(runAudioLevelPersistThread);
    }

    static void stopAudioLevelPersistThread()
    {
        if (!g_audioLevelPersistThreadAlive.load()) {
            return;
        }

        g_audioLevelPersistThreadAlive.store(false);
        g_audioLevelPersistPending.store(true);
        g_audioLevelPersistCv.notify_one();

        if (g_audioLevelPersistThread.joinable()) {
            g_audioLevelPersistThread.join();
        }
    }

    static void cacheAudioLevelForPersist(const dsAudioPortType_t portType, const float level)
    {
        switch (portType) {
            case dsAUDIOPORT_TYPE_SPDIF:
                g_audioLevelCacheSpdif.store(level);
                break;
            case dsAUDIOPORT_TYPE_HDMI:
                g_audioLevelCacheHdmi.store(level);
                break;
            case dsAUDIOPORT_TYPE_SPEAKER:
                g_audioLevelCacheSpeaker.store(level);
                break;
            case dsAUDIOPORT_TYPE_HEADPHONE:
                g_audioLevelCacheHeadphone.store(level);
                break;
            default:
                return;
        }

        g_audioLevelPersistPending.store(true);
        g_audioLevelPersistCv.notify_one();
    }
#endif
    
    // Helper method implementations for enabling audio port
    dsAudioPortType_t getAudioPortType(intptr_t handle)
    {
        intptr_t halHandle = 0;
        
        // Simplified approach - check common port types
        const dsAudioPortType_t portTypes[] = {
            dsAUDIOPORT_TYPE_HDMI,
            dsAUDIOPORT_TYPE_SPDIF, 
            dsAUDIOPORT_TYPE_SPEAKER,
            dsAUDIOPORT_TYPE_HDMI_ARC,
            dsAUDIOPORT_TYPE_HEADPHONE
        };
        
        for (int i = 0; i < 5; i++) {
            if (dsGetAudioPort(portTypes[i], 0, &halHandle) == dsERR_NONE) {
                if (handle == halHandle) {
                    return portTypes[i];
                }
            }
        }
        
        DSLOG_WARN("The requested audio port is not part of platform port configuration");
        return dsAUDIOPORT_TYPE_MAX;
    }
    
    uint32_t setAudioDuckingAudioLevel(intptr_t handle)
    {
        float volume = 0;
        
        if (_isDuckingInProgress) {
            volume = _volumeDuckingLevel;
        } else {
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
            volume = g_audioLevelCacheSpeaker.load();
#else
            volume = g_LastVolumeLevel.load();
#endif
            DSLOG_INFO("Restoring speaker audio level: %f", volume);
        }
        
        // Use resolve function for dsSetAudioLevel
        typedef dsError_t (*dsSetAudioLevel_t)(intptr_t handle, float level);
        static dsSetAudioLevel_t dsSetAudioLevelFunc = 0;
        if (dsSetAudioLevelFunc == 0) {
            dsSetAudioLevelFunc = (dsSetAudioLevel_t)resolve(RDK_DSHAL_NAME, "dsSetAudioLevel");
            if (dsSetAudioLevelFunc == 0) {
                DSLOG_ERR("dsSetAudioLevel is not defined");
                return WPEFramework::Core::ERROR_GENERAL;
            }
        }
        
        dsError_t ret = dsERR_GENERAL;
        if (0 != dsSetAudioLevelFunc) {
            ret = dsSetAudioLevelFunc(handle, volume);
        }
        
        if (ret != dsERR_NONE) {
            DSLOG_ERR("dsSetAudioLevel failed with error: %d", ret);
            return WPEFramework::Core::ERROR_GENERAL;
        }
        
        return WPEFramework::Core::ERROR_NONE;
    }

    bool isAudioOutputConnectedForInitialization(const intptr_t handle)
    {
        typedef dsError_t (*dsAudioOutIsConnected_t)(intptr_t handle, bool* isConnected);
        static dsAudioOutIsConnected_t dsAudioOutIsConnectedFunc = 0;
        bool isConnected = true;

        if (dsAudioOutIsConnectedFunc == 0) {
            dsAudioOutIsConnectedFunc = (dsAudioOutIsConnected_t)resolve(RDK_DSHAL_NAME, "dsAudioOutIsConnected");
            if (dsAudioOutIsConnectedFunc == 0) {
                DSLOG_WARN("dsAudioOutIsConnected is not defined; assuming audio output is connected");
                return isConnected;
            }
        }

        if (dsAudioOutIsConnectedFunc(handle, &isConnected) != dsERR_NONE) {
            DSLOG_WARN("dsAudioOutIsConnected failed; assuming audio output is connected");
            return true;
        }

        return isConnected;
    }
    
    uint32_t getAudioDelayInternal(dsAudioPortType_t portType)
    {
        std::string audioDelayMs = "0";
        uint32_t returnAudioDelayMs = 0;
        
        switch(portType) {
            case dsAUDIOPORT_TYPE_SPDIF:
                {
                   try {
                        audioDelayMs = device::HostPersistence::getInstance().getProperty("SPDIF0.audio.Delay");
                    }
                    catch(...) {
                            try {
                                DSLOG_INFO("SPDIF0.audio.Delay not found in persistence store. Try system default");
                                audioDelayMs = device::HostPersistence::getInstance().getDefaultProperty("SPDIF0.audio.Delay");
                            }
                            catch(...) {
                                audioDelayMs = "0";
                            }
                    }
                }
                break;
            case dsAUDIOPORT_TYPE_HDMI:
                {
                   try {
                        audioDelayMs = device::HostPersistence::getInstance().getProperty("HDMI0.audio.Delay");
                    }
                    catch(...) {
                            try {
                                DSLOG_INFO("HDMI0.audio.Delay not found in persistence store. Try system default");
                                audioDelayMs = device::HostPersistence::getInstance().getDefaultProperty("HDMI0.audio.Delay");
                            }
                            catch(...) {
                                audioDelayMs = "0";
                            }
                    }
                }
                break;
            case dsAUDIOPORT_TYPE_SPEAKER:
                {
                   try {
                        audioDelayMs = device::HostPersistence::getInstance().getProperty("SPEAKER0.audio.Delay");
                    }
                    catch(...) {
                            try {
                                DSLOG_INFO("SPEAKER0.audio.Delay not found in persistence store. Try system default");
                                audioDelayMs = device::HostPersistence::getInstance().getDefaultProperty("SPEAKER0.audio.Delay");
                            }
                            catch(...) {
                                audioDelayMs = "0";
                            }
                    }
                }
                break;
            case dsAUDIOPORT_TYPE_HDMI_ARC:
                {
                   try {
                        audioDelayMs = device::HostPersistence::getInstance().getProperty("HDMI_ARC0.audio.Delay");
                    }
                    catch(...) {
                            try {
                                DSLOG_INFO("HDMI_ARC0.audio.Delay not found in persistence store. Try system default");
                                audioDelayMs = device::HostPersistence::getInstance().getDefaultProperty("HDMI_ARC0.audio.Delay");
                            }
                            catch(...) {
                                audioDelayMs = "0";
                            }
                    }
                }
                break;
            default:
                DSLOG_INFO("Port type: UNKNOWN, persist audio delay: %s : NOT SET", audioDelayMs.c_str());
                break;
        }
        
        try {
            returnAudioDelayMs = std::stoul(audioDelayMs);
            DSLOG_INFO("Audio delay value returnAudioDelayMs: %d", returnAudioDelayMs);
        }
        catch(...) {
            DSLOG_INFO("Exception in getting the audio delay from persistence storage, returning default value 0");
            returnAudioDelayMs = 0;
        }
        
        return returnAudioDelayMs;
    }

    bool setAudioDelayInternal(intptr_t handle, uint32_t audioDelay)
    {
        try {
            // Use resolve function for dsSetAudioDelay
            typedef dsError_t (*dsSetAudioDelay_t)(intptr_t handle, uint32_t audioDelay);
            static dsSetAudioDelay_t dsSetAudioDelayFunc = 0;
            if (dsSetAudioDelayFunc == 0) {
                dsSetAudioDelayFunc = (dsSetAudioDelay_t)resolve(RDK_DSHAL_NAME, "dsSetAudioDelay");
                if (dsSetAudioDelayFunc == 0) {
                    DSLOG_ERR("dsSetAudioDelay is not defined");
                    return false;
                }
            }

            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetAudioDelayFunc) {
                ret = dsSetAudioDelayFunc(handle, audioDelay);
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("Audio delay set successfully: handle=%ld, delay=%u", (long)handle, audioDelay);
                return true;
            } else {
                if (ret == dsERR_OPERATION_NOT_SUPPORTED)
                    DSLOG_WARN("dsSetAudioDelay not supported for this port (error=%d)", ret);
                else
                    DSLOG_ERR("dsSetAudioDelay failed with error: %d", ret);
                return false;
            }
        } catch (...) {
            DSLOG_ERR("Exception in setAudioDelayInternal");
            return false;
        }
    }
    
    // HAL callback registration functions (internal)
    dsError_t registerHALCallbacks()
    {
        ENTRY_LOG;
        dsError_t ret = dsERR_NONE;
        
        try {
            // Register audio output port connect callback
            ret = dsAudioOutRegisterConnectCB(audioOutPortConnectCallback);
            if (ret != dsERR_NONE) {
                DSLOG_WARN("dsAudioOutRegisterConnectCB failed with error: %d", ret);
            } else {
                DSLOG_INFO("Audio output port connect callback registered successfully");
            }
            
            // Register audio format update callback  
            ret = dsAudioFormatUpdateRegisterCB(audioFormatUpdateCallback);
            if (ret != dsERR_NONE) {
                DSLOG_WARN("dsAudioFormatUpdateRegisterCB failed with error: %d", ret);
            } else {
                DSLOG_INFO("Audio format update callback registered successfully");
            }
            
            // Register atmos capability change callback
            ret = dsAudioAtmosCapsChangeRegisterCB(audioAtmosCapsChangeCallback);
            if (ret != dsERR_NONE) {
                DSLOG_WARN("dsAudioAtmosCapsChangeRegisterCB failed with error: %d", ret);
            } else {
                DSLOG_INFO("Audio atmos caps change callback registered successfully");
            }
            
        } catch (...) {
            DSLOG_ERR("Exception in registerHALCallbacks");
            ret = dsERR_GENERAL;
        }
        
        EXIT_LOG;
        return ret;
    }

public:
    dAudioImpl() : _isInitialized(false), _isDuckingInProgress(false), _volumeDuckingLevel(0), _muteStatus(false)
    {
        for (int i = 0; i < dsAUDIOPORT_TYPE_MAX; i++) {
            _audioPortEnabled[i] = false;
        }
        InitialiseHAL();
    }

    /** Called from DeviceSettingsImp::Configure() — deferred HAL initialisation.
     *  Mirrors old dsMgr pattern: load all persistence once, then init hardware. */
    void InitialiseHAL()
    {
        if (_isInitialized) return;
        ENTRY_LOG;
        DSLOG_INFO("<dsAudio>");
        try {
            // Root cause fix #2: load ALL persistence into memory in ONE file read
            // before audioConfigInit() makes 30-40 getProperty() calls.
            // Mirrors dsMgr_init(): HostPersistence::getInstance().load() called once
            // so all subsequent getProperty() are fast in-memory map lookups.
            device::HostPersistence::getInstance().load();

            dsError_t ret = dsAudioPortInit();
            if (ret != dsERR_NONE) {
                DSLOG_ERR("dsAudioPortInit failed with error: %d", ret);
            } else {
                _isInitialized = true;
                DSLOG_INFO("Audio platform initialized successfully");
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                startAudioLevelPersistThread();
#endif
                initializeAudioSettings();
                audioConfigInit();
                registerHALCallbacks();
                notifyAudioPortStateChanged(AudioPortState::AUDIO_PORT_STATE_INITIALIZED);
            }
        } catch (...) {
            DSLOG_ERR("Exception during Audio platform initialization");
        }
        EXIT_LOG;
    }

    virtual ~dAudioImpl()
    {
        ENTRY_LOG;

#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
        stopAudioLevelPersistThread();
#endif

        if (_isInitialized) {
            try {
                dsError_t ret = dsAudioPortTerm();
                if (ret != dsERR_NONE) {
                    DSLOG_ERR("dsAudioPortTerm failed with error: %d", ret);
                }
            } catch (...) {
                DSLOG_ERR("Exception during Audio platform termination");
            }
            _isInitialized = false;
        }
        EXIT_LOG;
    }

    // Type conversion methods
    dsAudioPortType_t convertToDS(const AudioPortType type)
    {
        switch (type) {
            case AudioPortType::AUDIO_PORT_TYPE_LR: return dsAUDIOPORT_TYPE_ID_LR;
            case AudioPortType::AUDIO_PORT_TYPE_HDMI: return dsAUDIOPORT_TYPE_HDMI;
            case AudioPortType::AUDIO_PORT_TYPE_SPDIF: return dsAUDIOPORT_TYPE_SPDIF;
            case AudioPortType::AUDIO_PORT_TYPE_SPEAKER: return dsAUDIOPORT_TYPE_SPEAKER;
            case AudioPortType::AUDIO_PORT_TYPE_HDMIARC: return dsAUDIOPORT_TYPE_HDMI_ARC;
            case AudioPortType::AUDIO_PORT_TYPE_HEADPHONE: return dsAUDIOPORT_TYPE_HEADPHONE;
            default: return dsAUDIOPORT_TYPE_MAX;
        }
    }

    dsAudioStereoMode_t convertToDS(const AudioStereoMode mode)
    {
        switch (mode) {
            case AudioStereoMode::AUDIO_STEREO_UNKNOWN: return dsAUDIO_STEREO_UNKNOWN;
            case AudioStereoMode::AUDIO_STEREO_MONO: return dsAUDIO_STEREO_MONO;
            case AudioStereoMode::AUDIO_STEREO_STEREO: return dsAUDIO_STEREO_STEREO;
            case AudioStereoMode::AUDIO_STEREO_SURROUND: return dsAUDIO_STEREO_SURROUND;
            case AudioStereoMode::AUDIO_STEREO_PASSTHROUGH: return dsAUDIO_STEREO_PASSTHRU;
            case AudioStereoMode::AUDIO_STEREO_DD: return dsAUDIO_STEREO_DD;
            case AudioStereoMode::AUDIO_STEREO_DDPLUS: return dsAUDIO_STEREO_DDPLUS;
            default: return dsAUDIO_STEREO_UNKNOWN;
        }
    }

    AudioStereoMode convertFromDS(const dsAudioStereoMode_t dsMode)
    {
        switch (dsMode) {
            case dsAUDIO_STEREO_UNKNOWN: return AudioStereoMode::AUDIO_STEREO_UNKNOWN;
            case dsAUDIO_STEREO_MONO: return AudioStereoMode::AUDIO_STEREO_MONO;
            case dsAUDIO_STEREO_STEREO: return AudioStereoMode::AUDIO_STEREO_STEREO;
            case dsAUDIO_STEREO_SURROUND: return AudioStereoMode::AUDIO_STEREO_SURROUND;
            case dsAUDIO_STEREO_PASSTHRU: return AudioStereoMode::AUDIO_STEREO_PASSTHROUGH;
            case dsAUDIO_STEREO_DD: return AudioStereoMode::AUDIO_STEREO_DD;
            case dsAUDIO_STEREO_DDPLUS: return AudioStereoMode::AUDIO_STEREO_DDPLUS;
            default: return AudioStereoMode::AUDIO_STEREO_UNKNOWN;
        }
    }

    dsAudioStereoMode_t getConfiguredStereoMode(const dsAudioPortType_t portType) const
    {
        switch (portType) {
            case dsAUDIOPORT_TYPE_HDMI: return g_HdmiAudioMode;
            case dsAUDIOPORT_TYPE_SPDIF: return g_SpdifAudioMode;
            case dsAUDIOPORT_TYPE_HDMI_ARC: return g_HdmiArcAudioMode;
            case dsAUDIOPORT_TYPE_SPEAKER: return g_SpeakerAudioMode;
            default: return dsAUDIO_STEREO_STEREO; /* Default to stereo like DS_IARM */
        }
    }

    bool isStereoAutoEnabled(const dsAudioPortType_t portType) const
    {
        switch (portType) {
            case dsAUDIOPORT_TYPE_HDMI: return g_AudioHdmiAuto;
            case dsAUDIOPORT_TYPE_SPDIF: return g_AudioSpdifAuto;
            case dsAUDIOPORT_TYPE_HDMI_ARC: return g_AudioHdmiArcAuto;
            case dsAUDIOPORT_TYPE_SPEAKER: return g_AudioSpeakerAuto;
            default: return false;
        }
    }

    void setConfiguredStereoMode(const dsAudioPortType_t portType, const dsAudioStereoMode_t mode)
    {
        switch (portType) {
            case dsAUDIOPORT_TYPE_HDMI: g_HdmiAudioMode = mode; break;
            case dsAUDIOPORT_TYPE_SPDIF: g_SpdifAudioMode = mode; break;
            case dsAUDIOPORT_TYPE_HDMI_ARC: g_HdmiArcAudioMode = mode; break;
            case dsAUDIOPORT_TYPE_SPEAKER: g_SpeakerAudioMode = mode; break;
            default: break;
        }
    }

    void setStereoAutoState(const dsAudioPortType_t portType, const bool enabled)
    {
        switch (portType) {
            case dsAUDIOPORT_TYPE_HDMI: g_AudioHdmiAuto = enabled; break;
            case dsAUDIOPORT_TYPE_SPDIF: g_AudioSpdifAuto = enabled; break;
            case dsAUDIOPORT_TYPE_HDMI_ARC: g_AudioHdmiArcAuto = enabled; break;
            case dsAUDIOPORT_TYPE_SPEAKER: g_AudioSpeakerAuto = enabled; break;
            default: break;
        }
    }

    bool getPersistedStereoMode(const dsAudioPortType_t portType, dsAudioStereoMode_t& mode) const
    {
        const char* property = nullptr;
        switch (portType) {
            case dsAUDIOPORT_TYPE_HDMI:     property = "HDMI0.AudioMode"; break;
            case dsAUDIOPORT_TYPE_SPDIF:    property = "SPDIF0.AudioMode"; break;
            case dsAUDIOPORT_TYPE_HDMI_ARC: property = "HDMI_ARC0.AudioMode"; break;
            case dsAUDIOPORT_TYPE_SPEAKER:  property = "SPEAKER0.AudioMode"; break;
            default: return false;
        }

        std::string value;
        try {
            value = device::HostPersistence::getInstance().getProperty(property);
        } catch (...) {
            try {
                value = device::HostPersistence::getInstance().getDefaultProperty(property);
            } catch (...) {
                return false;
            }
        }

        // dsAudio.c _GetAudioModeFromPersistent: HDMI reads always report the persisted mode via telemetry.
        if (portType == dsAUDIOPORT_TYPE_HDMI) {
            char telemetryValue[128] = {0};
            snprintf(telemetryValue, sizeof(telemetryValue), "The HDMI Audio Mode Setting From Persistent is %s", value.c_str());
            TELEMETRY_EVENT_STRING("hdmiAudioMode_split", telemetryValue);
        }

        if (value == "SURROUND") {
            mode = dsAUDIO_STEREO_SURROUND;
        } else if (value == "PASSTHRU") {
            mode = dsAUDIO_STEREO_PASSTHRU;
        } else if (value == "DOLBYDIGITAL") {
            mode = dsAUDIO_STEREO_DD;
        } else if (value == "DOLBYDIGITALPLUS") {
            mode = dsAUDIO_STEREO_DDPLUS;
        } else {
            mode = dsAUDIO_STEREO_STEREO;
        }
        return true;
    }

    bool getPersistedStereoAuto(const dsAudioPortType_t portType, int32_t& autoMode) const
    {
        const char* property = nullptr;
        switch (portType) {
            case dsAUDIOPORT_TYPE_HDMI:     property = "HDMI0.AudioMode.AUTO"; break;
            case dsAUDIOPORT_TYPE_SPDIF:    property = "SPDIF0.AudioMode.AUTO"; break;
            case dsAUDIOPORT_TYPE_HDMI_ARC: property = "HDMI_ARC0.AudioMode.AUTO"; break;
            case dsAUDIOPORT_TYPE_SPEAKER:  property = "SPEAKER0.AudioMode.AUTO"; break;
            default: return false;
        }

        std::string value;
        try {
            value = device::HostPersistence::getInstance().getProperty(property);
        } catch (...) {
            try {
                value = device::HostPersistence::getInstance().getDefaultProperty(property);
            } catch (...) {
                return false;
            }
        }
        autoMode = (value == "TRUE") ? 1 : 0;
        return true;
    }

    // IPlatform interface implementation
    uint32_t GetAudioPort(const AudioPortType type, const int32_t index, int32_t &handle) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            dsAudioPortType_t dsType = convertToDS(type);
            intptr_t dsHandle;
            
            dsError_t ret = dsGetAudioPort(dsType, index, &dsHandle);
            
            if (ret == dsERR_NONE) {
                handle = static_cast<int32_t>(dsHandle);
                DSLOG_INFO("success: type=%d, index=%d, handle=%d", type, index, handle);
            } else {
                if (ret == dsERR_OPERATION_NOT_SUPPORTED)
                    DSLOG_WARN(" port type=%d not supported on this platform (error=%d)", type, ret);
                else
                    DSLOG_ERR("dsGetAudioPort failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioPort");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioCapabilities(const int32_t handle, int32_t &capabilities) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            int dsCapabilities;
            
            // Use resolve function for dsGetAudioCapabilities
            typedef dsError_t (*dsGetAudioCapabilities_t)(intptr_t handle, int* capabilities);
            static dsGetAudioCapabilities_t dsGetAudioCapabilitiesFunc = 0;
            if (dsGetAudioCapabilitiesFunc == 0) {
                dsGetAudioCapabilitiesFunc = (dsGetAudioCapabilities_t)resolve(RDK_DSHAL_NAME, "dsGetAudioCapabilities");
                if (dsGetAudioCapabilitiesFunc == 0) {
                    DSLOG_ERR("dsGetAudioCapabilities is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetAudioCapabilitiesFunc) {
                ret = dsGetAudioCapabilitiesFunc(dsHandle, &dsCapabilities);
            }
            
            if (ret == dsERR_NONE) {
                capabilities = dsCapabilities;
                DSLOG_INFO("success: handle=%d, capabilities=%d", handle, capabilities);
            } else {
                DSLOG_ERR("dsGetAudioCapabilities failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioCapabilities");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioMS12Capabilities(const int32_t handle, int32_t &capabilities) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            int dsCapabilities;
            dsError_t ret = dsGetMS12Capabilities(dsHandle, &dsCapabilities);
            if (ret == dsERR_NONE) {
                capabilities = dsCapabilities;
                DSLOG_INFO("success: handle=%d, capabilities=%d", handle, capabilities);
            } else {
                DSLOG_ERR("dsGetMS12Capabilities failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioMS12Capabilities");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioFormat(const int32_t handle, AudioFormat &audioFormat) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            dsAudioFormat_t dsFormat;
            
            // Use resolve function for dsGetAudioFormat
            typedef dsError_t (*dsGetAudioFormat_t)(intptr_t handle, dsAudioFormat_t* format);
            static dsGetAudioFormat_t dsGetAudioFormatFunc = 0;
            if (dsGetAudioFormatFunc == 0) {
                dsGetAudioFormatFunc = (dsGetAudioFormat_t)resolve(RDK_DSHAL_NAME, "dsGetAudioFormat");
                if (dsGetAudioFormatFunc == 0) {
                    DSLOG_ERR("dsGetAudioFormat is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetAudioFormatFunc) {
                ret = dsGetAudioFormatFunc(dsHandle, &dsFormat);
            }
            
            if (ret == dsERR_NONE) {
                audioFormat = static_cast<AudioFormat>(dsFormat);
                DSLOG_INFO("success: handle=%d, format=%d", handle, audioFormat);
            } else {
                DSLOG_ERR("dsGetAudioFormat failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioFormat");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioEncoding(const int32_t handle, AudioEncoding &encoding) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            // No dsGetAudioEncoding HAL API exists; encoding is derived from stereo mode (mirrors dsAudio.c _dsGetEncoding)
            dsAudioStereoMode_t stereoMode = dsAUDIO_STEREO_UNKNOWN;
            dsError_t ret = dsGetStereoMode(static_cast<intptr_t>(handle), &stereoMode);
            if (ret != dsERR_NONE) {
                DSLOG_ERR(" dsGetStereoMode failed: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
            switch (stereoMode) {
                case dsAUDIO_STEREO_STEREO:
                    encoding = AudioEncoding::AUDIO_ENCODING_PCM;
                    break;
                case dsAUDIO_STEREO_DD:
                    encoding = AudioEncoding::AUDIO_ENCODING_AC3;
                    break;
                case dsAUDIO_STEREO_DDPLUS:
                    encoding = AudioEncoding::AUDIO_ENCODING_EAC3;
                    break;
                case dsAUDIO_STEREO_SURROUND:
                case dsAUDIO_STEREO_PASSTHRU:
                    encoding = AudioEncoding::AUDIO_ENCODING_DISPLAY;
                    break;
                case dsAUDIO_STEREO_UNKNOWN:
                default:
                    encoding = AudioEncoding::AUDIO_ENCODING_NONE;
                    break;
            }
            DSLOG_INFO(" handle=%d stereoMode=%d encoding=%d", handle, stereoMode, static_cast<int>(encoding));
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioEncoding");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetSupportedCompressions(const int32_t handle, IDeviceSettingsAudioCompressionIterator*& compressions) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        // Derive supported compressions from dsGetAudioCapabilities — no lib32-devicesettings dependency.
        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);

            // Resolve dsGetAudioCapabilities via dlopen (same pattern as all other HAL calls).
            typedef dsError_t (*dsGetAudioCapabilities_t)(intptr_t handle, int* capabilities);
            static dsGetAudioCapabilities_t dsGetAudioCapabilitiesFunc = 0;
            if (dsGetAudioCapabilitiesFunc == 0) {
                dsGetAudioCapabilitiesFunc = (dsGetAudioCapabilities_t)resolve(RDK_DSHAL_NAME, "dsGetAudioCapabilities");
            }

            int caps = 0;
            if (dsGetAudioCapabilitiesFunc != 0) {
                dsGetAudioCapabilitiesFunc(dsHandle, &caps);
            }

            // Build compression list based on capabilities bitmask.
            // dsAUDIOSUPPORT_DD / DDPLUS indicate heavy/medium compression support.
            std::vector<AudioCompression> compressionList;
            compressionList.push_back(AudioCompression::AUDIO_COMPRESSION_NONE);
            compressionList.push_back(AudioCompression::AUDIO_COMPRESSION_LIGHT);
            if (caps & dsAUDIOSUPPORT_DD) {
                compressionList.push_back(AudioCompression::AUDIO_COMPRESSION_MEDIUM);
            }
            if (caps & dsAUDIOSUPPORT_DDPLUS) {
                compressionList.push_back(AudioCompression::AUDIO_COMPRESSION_HEAVY);
            }

            using CompressionIterator = WPEFramework::RPC::IteratorType<IDeviceSettingsAudioCompressionIterator>;
            compressions = WPEFramework::Core::Service<CompressionIterator>::Create<IDeviceSettingsAudioCompressionIterator>(compressionList);

            DSLOG_INFO("success: handle=%d, count=%zu, caps=0x%x",
                    handle, compressionList.size(), caps);
        } catch (...) {
            DSLOG_ERR("Exception in GetSupportedCompressions");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioCompression(const int32_t handle, AudioCompression &compression) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            int dsCompression;
            
            // Use resolve function for dsGetAudioCompression
            typedef dsError_t (*dsGetAudioCompression_t)(intptr_t handle, int* compression);
            static dsGetAudioCompression_t dsGetAudioCompressionFunc = 0;
            if (dsGetAudioCompressionFunc == 0) {
                dsGetAudioCompressionFunc = (dsGetAudioCompression_t)resolve(RDK_DSHAL_NAME, "dsGetAudioCompression");
                if (dsGetAudioCompressionFunc == 0) {
                    DSLOG_ERR("dsGetAudioCompression is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetAudioCompressionFunc) {
                ret = dsGetAudioCompressionFunc(dsHandle, &dsCompression);
            }
            
            if (ret == dsERR_NONE) {
                compression = static_cast<AudioCompression>(dsCompression);
                DSLOG_INFO("success: handle=%d, compression=%d", handle, static_cast<int>(compression));
            } else {
                DSLOG_ERR("dsGetAudioCompression failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioCompression");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioCompression(const int32_t handle, const AudioCompression compression) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetAudioCompression
            typedef dsError_t (*dsSetAudioCompression_t)(intptr_t handle, int compression);
            static dsSetAudioCompression_t dsSetAudioCompressionFunc = 0;
            if (dsSetAudioCompressionFunc == 0) {
                dsSetAudioCompressionFunc = (dsSetAudioCompression_t)resolve(RDK_DSHAL_NAME, "dsSetAudioCompression");
                if (dsSetAudioCompressionFunc == 0) {
                    DSLOG_ERR("dsSetAudioCompression is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetAudioCompressionFunc) {
                ret = dsSetAudioCompressionFunc(dsHandle, static_cast<int>(compression));
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, compression=%d", handle, static_cast<int>(compression));
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.Compression", std::to_string(static_cast<int>(compression)));
#endif
            } else {
                DSLOG_ERR("dsSetAudioCompression failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioCompression");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioLevel(const int32_t handle, const float audioLevel) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetAudioLevel
            typedef dsError_t (*dsSetAudioLevel_t)(intptr_t handle, float level);
            static dsSetAudioLevel_t dsSetAudioLevelFunc = 0;
            if (dsSetAudioLevelFunc == 0) {
                dsSetAudioLevelFunc = (dsSetAudioLevel_t)resolve(RDK_DSHAL_NAME, "dsSetAudioLevel");
                if (dsSetAudioLevelFunc == 0) {
                    DSLOG_ERR("dsSetAudioLevel is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetAudioLevelFunc) {
                // dsAudio.c: for SPEAKER port, if ducking is in progress, apply
                // ducking level instead of the requested level (or skip if ducking is active).
                dsAudioPortType_t portType = getAudioPortType(dsHandle);
                if (portType == dsAUDIOPORT_TYPE_SPEAKER) {
                    float currentLevel = 0;
                    dsGetAudioLevel(dsHandle, &currentLevel);
                    if (_isDuckingInProgress && currentLevel != static_cast<float>(_volumeDuckingLevel)) {
                        // Ducking active and current level diverged — re-apply ducking level
                        DSLOG_INFO(" ducking in progress, applying ducking level %d instead of %f",
                                _volumeDuckingLevel, audioLevel);
                        ret = dsSetAudioLevelFunc(dsHandle, static_cast<float>(_volumeDuckingLevel));
                    } else if (_isDuckingInProgress) {
                        // Already at ducking level — skip (dsAudio.c: returns SUCCESS without calling HAL)
                        DSLOG_INFO(" ducking in progress, skipping level change for SPEAKER");
                        ret = dsERR_NONE;
                    } else {
                        ret = dsSetAudioLevelFunc(dsHandle, audioLevel);
                    }
                } else {
                    ret = dsSetAudioLevelFunc(dsHandle, audioLevel);
                }
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, level=%f", handle, audioLevel);
                g_LastVolumeLevel.store(audioLevel);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                dsAudioPortType_t _portType = getAudioPortType(dsHandle);
                if (g_audioLevelPersistThreadAlive.load()) {
                    cacheAudioLevelForPersist(_portType, audioLevel);
                } else {
                    std::string _audioLevel = std::to_string(audioLevel);
                    switch (_portType) {
                        case dsAUDIOPORT_TYPE_SPDIF:     device::HostPersistence::getInstance().persistHostProperty("SPDIF0.audio.Level", _audioLevel); break;
                        case dsAUDIOPORT_TYPE_HDMI:      device::HostPersistence::getInstance().persistHostProperty("HDMI0.audio.Level", _audioLevel); break;
                        case dsAUDIOPORT_TYPE_SPEAKER:   device::HostPersistence::getInstance().persistHostProperty("SPEAKER0.audio.Level", _audioLevel); break;
                        case dsAUDIOPORT_TYPE_HEADPHONE: device::HostPersistence::getInstance().persistHostProperty("HEADPHONE0.audio.Level", _audioLevel); break;
                        default: break;
                    }
                }
#endif
                // Notify about audio level change
                notifyAudioLevelChanged(static_cast<int32_t>(audioLevel));
            } else {
                DSLOG_ERR("dsSetAudioLevel failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioLevel");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioLevel(const int32_t handle, float &audioLevel) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            float dsLevel;

            // Use resolve function for dsGetAudioLevel
            typedef dsError_t (*dsGetAudioLevel_t)(intptr_t handle, float* level);
            static dsGetAudioLevel_t dsGetAudioLevelFunc = 0;
            if (dsGetAudioLevelFunc == 0) {
                dsGetAudioLevelFunc = (dsGetAudioLevel_t)resolve(RDK_DSHAL_NAME, "dsGetAudioLevel");
                if (dsGetAudioLevelFunc == 0) {
                    DSLOG_ERR("dsGetAudioLevel is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetAudioLevelFunc) {
                ret = dsGetAudioLevelFunc(dsHandle, &dsLevel);
            }

            if (ret == dsERR_NONE) {
                audioLevel = dsLevel;
                g_LastVolumeLevel.store(audioLevel);
                DSLOG_INFO("success: handle=%d, level=%f", handle, audioLevel);
            } else {
                DSLOG_ERR("dsGetAudioLevel failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioLevel");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioGain(const int32_t handle, const float gainLevel) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetAudioGain
            typedef dsError_t (*dsSetAudioGain_t)(intptr_t handle, float gainLevel);
            static dsSetAudioGain_t dsSetAudioGainFunc = 0;
            if (dsSetAudioGainFunc == 0) {
                dsSetAudioGainFunc = (dsSetAudioGain_t)resolve(RDK_DSHAL_NAME, "dsSetAudioGain");
                if (dsSetAudioGainFunc == 0) {
                    DSLOG_ERR("dsSetAudioGain is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetAudioGainFunc) {
                ret = dsSetAudioGainFunc(dsHandle, gainLevel);
            }
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, gain=%f", handle, gainLevel);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                std::string _gain = std::to_string(gainLevel);
                dsAudioPortType_t _portType = getAudioPortType(dsHandle);
                switch (_portType) {
                    case dsAUDIOPORT_TYPE_SPDIF:   device::HostPersistence::getInstance().persistHostProperty("SPDIF0.audio.Gain",   _gain); break;
                    case dsAUDIOPORT_TYPE_HDMI:    device::HostPersistence::getInstance().persistHostProperty("HDMI0.audio.Gain",    _gain); break;
                    case dsAUDIOPORT_TYPE_SPEAKER: device::HostPersistence::getInstance().persistHostProperty("SPEAKER0.audio.Gain", _gain); break;
                    default: break;
                }
#endif
            } else {
                DSLOG_ERR("dsSetAudioGain failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioGain");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioGain(const int32_t handle, float &gainLevel) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            float dsGain;
            
            // Use resolve function for dsGetAudioGain
            typedef dsError_t (*dsGetAudioGain_t)(intptr_t handle, float* gain);
            static dsGetAudioGain_t dsGetAudioGainFunc = 0;
            if (dsGetAudioGainFunc == 0) {
                dsGetAudioGainFunc = (dsGetAudioGain_t)resolve(RDK_DSHAL_NAME, "dsGetAudioGain");
                if (dsGetAudioGainFunc == 0) {
                    DSLOG_ERR("dsGetAudioGain is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetAudioGainFunc) {
                ret = dsGetAudioGainFunc(dsHandle, &dsGain);
            }
            
            if (ret == dsERR_NONE) {
                gainLevel = dsGain;
                DSLOG_INFO("success: handle=%d, gain=%f", handle, gainLevel);
            } else {
                DSLOG_ERR("dsGetAudioGain failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioGain");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioMute(const int32_t handle, const bool mute) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);

            // dsAudio.c: when unmuting SPEAKER port, restore ducking level first
            dsAudioPortType_t portType = getAudioPortType(dsHandle);
            if (!mute && portType == dsAUDIOPORT_TYPE_SPEAKER) {
                if (setAudioDuckingAudioLevel(dsHandle) != WPEFramework::Core::ERROR_NONE) {
                    DSLOG_ERR(" failed to restore audio ducking level for Speaker port");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

            dsError_t ret = dsSetAudioMute(dsHandle, mute);
            if (ret == dsERR_NONE) {
                _muteStatus = mute;
                DSLOG_INFO("success: handle=%d, mute=%d", handle, mute);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                std::string _mute = mute ? "TRUE" : "FALSE";
                dsAudioPortType_t _portType = getAudioPortType(dsHandle);
                switch (_portType) {
                    case dsAUDIOPORT_TYPE_SPDIF:     device::HostPersistence::getInstance().persistHostProperty("SPDIF0.audio.mute",     _mute); break;
                    case dsAUDIOPORT_TYPE_HDMI:      device::HostPersistence::getInstance().persistHostProperty("HDMI0.audio.mute",      _mute); break;
                    case dsAUDIOPORT_TYPE_SPEAKER:   device::HostPersistence::getInstance().persistHostProperty("SPEAKER0.audio.mute",   _mute); break;
                    case dsAUDIOPORT_TYPE_HEADPHONE: device::HostPersistence::getInstance().persistHostProperty("HEADPHONE0.audio.mute", _mute); break;
                    case dsAUDIOPORT_TYPE_HDMI_ARC:  device::HostPersistence::getInstance().persistHostProperty("HDMI_ARC0.audio.mute",  _mute); break;
                    default: break;
                }
#endif
            } else {
                DSLOG_ERR("dsSetAudioMute failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioMute");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t IsAudioMuted(const int32_t handle, bool &muted) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        
        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            bool dsMuted;
            
            // Use resolve function for dsIsAudioMute
            typedef dsError_t (*dsIsAudioMute_t)(intptr_t handle, bool* muted);
            static dsIsAudioMute_t dsIsAudioMuteFunc = 0;
            if (dsIsAudioMuteFunc == 0) {
                dsIsAudioMuteFunc = (dsIsAudioMute_t)resolve(RDK_DSHAL_NAME, "dsIsAudioMute");
                if (dsIsAudioMuteFunc == 0) {
                    DSLOG_ERR("dsIsAudioMute is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsIsAudioMuteFunc) {
                ret = dsIsAudioMuteFunc(dsHandle, &dsMuted);
            }
            
            if (ret == dsERR_NONE) {
                muted = dsMuted;
                DSLOG_INFO("success: handle=%d, muted=%d", handle, muted);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                std::string muteKey;
                switch (getAudioPortType(dsHandle)) {
                    case dsAUDIOPORT_TYPE_SPDIF:     muteKey = "SPDIF0.audio.mute";     break;
                    case dsAUDIOPORT_TYPE_HDMI:      muteKey = "HDMI0.audio.mute";      break;
                    case dsAUDIOPORT_TYPE_SPEAKER:   muteKey = "SPEAKER0.audio.mute";   break;
                    case dsAUDIOPORT_TYPE_HEADPHONE: muteKey = "HEADPHONE0.audio.mute"; break;
                    case dsAUDIOPORT_TYPE_HDMI_ARC:  muteKey = "HDMI_ARC0.audio.mute";  break;
                    default: break;
                }

                if (!muteKey.empty()) {
                    try {
                        muted = (device::HostPersistence::getInstance().getProperty(muteKey) == "TRUE")
                            || muted;
                    } catch (...) {
                        DSLOG_WARN("failed to read persisted mute state for %s", muteKey.c_str());
                    }
                }
#endif
            } else {
                DSLOG_ERR("dsIsAudioMute failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in IsAudioMuted");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioDucking(const int32_t handle, const AudioDuckingType duckingType, const AudioDuckingAction duckingAction, const uint8_t level) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            int32_t volume = 0;
            bool portEnabled = false;
            
            DSLOG_INFO(" action=%d, type=%d, level=%d", static_cast<int>(duckingAction), static_cast<int>(duckingType), level);

            // Check if audio port is enabled
            dsError_t ret = dsIsAudioPortEnabled(dsHandle, &portEnabled);
            if (ret != dsERR_NONE) {
                DSLOG_WARN("dsIsAudioPortEnabled failed with error: %d", ret);
            }

            const float lastVolumeLevel = g_LastVolumeLevel.load();
            DSLOG_INFO("Cached audio level: %f", lastVolumeLevel);

            // Calculate ducking volume based on action and type
            if (duckingAction == AudioDuckingAction::AUDIO_DUCKINGACTION_START) {
                _isDuckingInProgress = true;
                if (duckingType == AudioDuckingType::AUDIO_DUCKINGTYPE_RELATIVE) {
                    volume = (lastVolumeLevel * level) / 100;
                } else {
                    if (level > lastVolumeLevel) {
                        volume = lastVolumeLevel;
                    } else {
                        volume = level;
                    }
                }
            } else {
                _isDuckingInProgress = false;
                volume = static_cast<int32_t>(lastVolumeLevel);
            }

            // If muted or port disabled, store volume but don't apply
            if (_muteStatus || !portEnabled) {
                DSLOG_WARN("Mute on or port disabled, ignoring ducking request");
                _volumeDuckingLevel = volume;
                EXIT_LOG;
                return WPEFramework::Core::ERROR_NONE;
            }

            DSLOG_INFO("Adjusted volume: %d, previous ducking level: %d", volume, _volumeDuckingLevel);

            // Apply volume to HAL layer and send event if changed
            if (volume != _volumeDuckingLevel) {
                // Use resolve function for dsSetAudioLevel
                typedef dsError_t (*dsSetAudioLevel_t)(intptr_t handle, float level);
                static dsSetAudioLevel_t dsSetAudioLevelFunc = 0;
                if (dsSetAudioLevelFunc == 0) {
                    dsSetAudioLevelFunc = (dsSetAudioLevel_t)resolve(RDK_DSHAL_NAME, "dsSetAudioLevel");
                    if (dsSetAudioLevelFunc == 0) {
                        DSLOG_ERR("dsSetAudioLevel is not defined");
                        return WPEFramework::Core::ERROR_GENERAL;
                    }
                }
                
                dsError_t ret = dsERR_GENERAL;
                if (0 != dsSetAudioLevelFunc) {
                    ret = dsSetAudioLevelFunc(dsHandle, volume);
                }
                
                if (ret == dsERR_NONE) {
                    _volumeDuckingLevel = volume;
                    DSLOG_INFO("applied successfully: handle=%d, volume=%d", handle, volume);
                    
                    // Send audio level change event through callback if available
                    if (g_AudioLevelChangedCallback) {
                        g_AudioLevelChangedCallback(static_cast<float>(volume));
                    }
                } else {
                    DSLOG_ERR("dsSetAudioLevel failed with error: %d", ret);
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            DSLOG_INFO("success: handle=%d, type=%d, action=%d, level=%d, final_volume=%d",
                   handle, static_cast<int>(duckingType), static_cast<int>(duckingAction), level, volume);
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioDucking");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetStereoMode(const int32_t handle, AudioStereoMode &mode, const bool persist) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        
        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            const dsAudioPortType_t portType = getAudioPortType(dsHandle);
            dsAudioStereoMode_t dsMode = dsAUDIO_STEREO_UNKNOWN;
            dsError_t ret = dsERR_NONE;

            if (persist) {
                if (!getPersistedStereoMode(portType, dsMode)) {
                    DSLOG_ERR("No persisted stereo mode for handle=%d", handle);
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            } else if (isStereoAutoEnabled(portType) || portType == dsAUDIOPORT_TYPE_SPEAKER) {
                ret = dsGetStereoMode(dsHandle, &dsMode);
            } else {
                dsMode = getConfiguredStereoMode(portType);
            }
            
            if (ret == dsERR_NONE) {
                mode = convertFromDS(dsMode);
                DSLOG_INFO("success: handle=%d, mode=%d, persist=%s", handle, static_cast<int>(mode), persist ? "true" : "false");
            } else {
                DSLOG_ERR("dsGetStereoMode failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetStereoMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetStereoMode(const int32_t handle, const AudioStereoMode mode, const bool persist) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            dsAudioStereoMode_t dsMode = convertToDS(mode);
            const dsAudioPortType_t dsPortType = getAudioPortType(dsHandle);

            if (dsPortType == dsAUDIOPORT_TYPE_MAX) {
                DSLOG_ERR("Unable to determine audio port type for handle=%d", handle);
                return WPEFramework::Core::ERROR_BAD_REQUEST;
            }

            dsError_t ret = dsSetStereoMode(dsHandle, dsMode);

            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, mode=%d, persist=%s", handle, static_cast<int>(mode), persist ? "true" : "false");

                // dsAudio.c _dsSetStereoMode: fires once per call when persisting a user-requested PASSTHRU mode.
                if (mode == AudioStereoMode::AUDIO_STEREO_PASSTHROUGH && persist) {
                    TELEMETRY_EVENT_INT("SYS_INFO_Userpassthruenable", 1);
                }

                AudioPortType portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER; // Default
                
                // Convert dsAudioPortType_t to AudioPortType and handle persistence
                std::string modeString;
                switch (mode) {
                    case AudioStereoMode::AUDIO_STEREO_STEREO:
                        modeString = "STEREO";
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER;
                        break;
                    case AudioStereoMode::AUDIO_STEREO_SURROUND:
                        modeString = "SURROUND";
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER;
                        break;
                    case AudioStereoMode::AUDIO_STEREO_PASSTHROUGH:
                        modeString = "PASSTHRU";
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER;
                        break;
                    case AudioStereoMode::AUDIO_STEREO_DD:
                        modeString = "DOLBYDIGITAL";
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER;
                        break;
                    case AudioStereoMode::AUDIO_STEREO_DDPLUS:
                        modeString = "DOLBYDIGITALPLUS";
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER;
                        break;
                    default:
                        modeString = "STEREO";
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER;
                        break;
                }

                // Convert dsAudioPortType_t to AudioPortType for notification
                switch (dsPortType) {
                    case dsAUDIOPORT_TYPE_HDMI:
                        portType = AudioPortType::AUDIO_PORT_TYPE_HDMI;
                        break;
                    case dsAUDIOPORT_TYPE_SPDIF:
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPDIF;
                        break;
                    case dsAUDIOPORT_TYPE_SPEAKER:
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER;
                        break;
                    case dsAUDIOPORT_TYPE_HDMI_ARC:
                        portType = AudioPortType::AUDIO_PORT_TYPE_HDMIARC;
                        break;
                    default:
                        portType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER;
                        break;
                }

                // Handle persistence based on port type and mode
                if (persist) {
                    try {
                        DSLOG_INFO("Setting Audio Mode %s with persistent value: %s", modeString.c_str(), persist ? "true" : "false");
                        
                        switch (dsPortType) {
                            case dsAUDIOPORT_TYPE_HDMI:
                                device::HostPersistence::getInstance().persistHostProperty("HDMI0.AudioMode", modeString.c_str());
                                break;
                            case dsAUDIOPORT_TYPE_SPDIF:
                                device::HostPersistence::getInstance().persistHostProperty("SPDIF0.AudioMode", modeString.c_str());
                                break;
                            case dsAUDIOPORT_TYPE_HDMI_ARC:
                                device::HostPersistence::getInstance().persistHostProperty("HDMI_ARC0.AudioMode", modeString.c_str());
                                break;
                            case dsAUDIOPORT_TYPE_SPEAKER:
                                device::HostPersistence::getInstance().persistHostProperty("SPEAKER0.AudioMode", modeString.c_str());
                                break;
                            default:
                                DSLOG_WARN("Unknown port type %d, skipping persistence", dsPortType);
                                break;
                        }
                    } catch (...) {
                        DSLOG_ERR("Error in persisting audio mode setting");
                    }
                }

                setConfiguredStereoMode(dsPortType, dsMode);

                // Notify about audio mode change
                notifyAudioModeChanged(portType, mode);
            } else {
                if (ret == dsERR_OPERATION_NOT_SUPPORTED)
                    DSLOG_WARN("dsSetStereoMode not supported on this port (error=%d)", ret);
                else
                    DSLOG_ERR("dsSetStereoMode failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetStereoMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAssociatedAudioMixing(const int32_t handle, const bool mixing) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetAssociatedAudioMixing
            typedef dsError_t (*dsSetAssociatedAudioMixing_t)(intptr_t handle, bool mixing);
            static dsSetAssociatedAudioMixing_t dsSetAssociatedAudioMixingFunc = 0;
            if (dsSetAssociatedAudioMixingFunc == 0) {
                dsSetAssociatedAudioMixingFunc = (dsSetAssociatedAudioMixing_t)resolve(RDK_DSHAL_NAME, "dsSetAssociatedAudioMixing");
                if (dsSetAssociatedAudioMixingFunc == 0) {
                    DSLOG_ERR("dsSetAssociatedAudioMixing is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetAssociatedAudioMixingFunc) {
                ret = dsSetAssociatedAudioMixingFunc(dsHandle, mixing);
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, mixing=%s", handle, mixing ? "true" : "false");
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.AssociatedAudioMixing", mixing ? "Enabled" : "Disabled");
#endif
                // Notify about associated audio mixing change
                notifyAssociatedAudioMixingChanged(mixing);
            } else {
                DSLOG_ERR("dsSetAssociatedAudioMixing failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAssociatedAudioMixing");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAssociatedAudioMixing(const int32_t handle, bool &mixing) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            bool dsMixing;
            
            // Use resolve function for dsGetAssociatedAudioMixing
            typedef dsError_t (*dsGetAssociatedAudioMixing_t)(intptr_t handle, bool* mixing);
            static dsGetAssociatedAudioMixing_t dsGetAssociatedAudioMixingFunc = 0;
            if (dsGetAssociatedAudioMixingFunc == 0) {
                dsGetAssociatedAudioMixingFunc = (dsGetAssociatedAudioMixing_t)resolve(RDK_DSHAL_NAME, "dsGetAssociatedAudioMixing");
                if (dsGetAssociatedAudioMixingFunc == 0) {
                    DSLOG_ERR("dsGetAssociatedAudioMixing is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetAssociatedAudioMixingFunc) {
                ret = dsGetAssociatedAudioMixingFunc(dsHandle, &dsMixing);
            }
            
            if (ret == dsERR_NONE) {
                mixing = dsMixing;
                DSLOG_INFO("success: handle=%d, mixing=%s", handle, mixing ? "true" : "false");
            } else {
                DSLOG_ERR("dsGetAssociatedAudioMixing failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAssociatedAudioMixing");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioFaderControl(const int32_t handle, const int32_t mixerBalance) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetFaderControl
            typedef dsError_t (*dsSetFaderControl_t)(intptr_t handle, int balance);
            static dsSetFaderControl_t dsSetFaderControlFunc = 0;
            if (dsSetFaderControlFunc == 0) {
                dsSetFaderControlFunc = (dsSetFaderControl_t)resolve(RDK_DSHAL_NAME, "dsSetFaderControl");
                if (dsSetFaderControlFunc == 0) {
                    DSLOG_ERR("dsSetFaderControl is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetFaderControlFunc) {
                ret = dsSetFaderControlFunc(dsHandle, mixerBalance);
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, balance=%d", handle, mixerBalance);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.FaderControl", std::to_string(mixerBalance));
#endif
                // Notify about fader control change
                notifyAudioFaderControlChanged(mixerBalance);
            } else {
                DSLOG_ERR("dsSetFaderControl failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioFaderControl");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioFaderControl(const int32_t handle, int32_t &mixerBalance) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            int dsBalance;
            
            // Use resolve function for dsGetFaderControl
            typedef dsError_t (*dsGetFaderControl_t)(intptr_t handle, int* balance);
            static dsGetFaderControl_t dsGetFaderControlFunc = 0;
            if (dsGetFaderControlFunc == 0) {
                dsGetFaderControlFunc = (dsGetFaderControl_t)resolve(RDK_DSHAL_NAME, "dsGetFaderControl");
                if (dsGetFaderControlFunc == 0) {
                    DSLOG_ERR("dsGetFaderControl is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetFaderControlFunc) {
                ret = dsGetFaderControlFunc(dsHandle, &dsBalance);
            }
            
            if (ret == dsERR_NONE) {
                mixerBalance = dsBalance;
                DSLOG_INFO("success: handle=%d, balance=%d", handle, mixerBalance);
            } else {
                DSLOG_ERR("dsGetFaderControl failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioFaderControl");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioPrimaryLanguage(const int32_t handle, const std::string& primaryAudioLanguage) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetPrimaryLanguage
            typedef dsError_t (*dsSetPrimaryLanguage_t)(intptr_t handle, const char* language);
            static dsSetPrimaryLanguage_t dsSetPrimaryLanguageFunc = 0;
            if (dsSetPrimaryLanguageFunc == 0) {
                dsSetPrimaryLanguageFunc = (dsSetPrimaryLanguage_t)resolve(RDK_DSHAL_NAME, "dsSetPrimaryLanguage");
                if (dsSetPrimaryLanguageFunc == 0) {
                    DSLOG_ERR("dsSetPrimaryLanguage is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetPrimaryLanguageFunc) {
                ret = dsSetPrimaryLanguageFunc(dsHandle, primaryAudioLanguage.c_str());
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, language=%s", handle, primaryAudioLanguage.c_str());
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.PrimaryLanguage", primaryAudioLanguage);
#endif
                // Notify about primary language change
                notifyAudioPrimaryLanguageChanged(primaryAudioLanguage);
            } else {
                DSLOG_ERR("dsSetPrimaryLanguage failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioPrimaryLanguage");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioPrimaryLanguage(const int32_t handle, std::string &primaryAudioLanguage) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            char langStr[32] = {0};
            
            // Use resolve function for dsGetPrimaryLanguage
            typedef dsError_t (*dsGetPrimaryLanguage_t)(intptr_t handle, char* language);
            static dsGetPrimaryLanguage_t dsGetPrimaryLanguageFunc = 0;
            if (dsGetPrimaryLanguageFunc == 0) {
                dsGetPrimaryLanguageFunc = (dsGetPrimaryLanguage_t)resolve(RDK_DSHAL_NAME, "dsGetPrimaryLanguage");
                if (dsGetPrimaryLanguageFunc == 0) {
                    DSLOG_ERR("dsGetPrimaryLanguage is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetPrimaryLanguageFunc) {
                ret = dsGetPrimaryLanguageFunc(dsHandle, langStr);
            }
            
            if (ret == dsERR_NONE) {
                primaryAudioLanguage = std::string(langStr);
                DSLOG_INFO("success: handle=%d, language=%s", handle, primaryAudioLanguage.c_str());
            } else {
                DSLOG_ERR("dsGetPrimaryLanguage failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioPrimaryLanguage");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioSecondaryLanguage(const int32_t handle, const std::string& secondaryAudioLanguage) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetSecondaryLanguage
            typedef dsError_t (*dsSetSecondaryLanguage_t)(intptr_t handle, const char* language);
            static dsSetSecondaryLanguage_t dsSetSecondaryLanguageFunc = 0;
            if (dsSetSecondaryLanguageFunc == 0) {
                dsSetSecondaryLanguageFunc = (dsSetSecondaryLanguage_t)resolve(RDK_DSHAL_NAME, "dsSetSecondaryLanguage");
                if (dsSetSecondaryLanguageFunc == 0) {
                    DSLOG_ERR("dsSetSecondaryLanguage is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetSecondaryLanguageFunc) {
                ret = dsSetSecondaryLanguageFunc(dsHandle, secondaryAudioLanguage.c_str());
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, language=%s", handle, secondaryAudioLanguage.c_str());
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.SecondaryLanguage", secondaryAudioLanguage);
#endif
                // Notify about secondary language change
                notifyAudioSecondaryLanguageChanged(secondaryAudioLanguage);
            } else {
                DSLOG_ERR("dsSetSecondaryLanguage failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioSecondaryLanguage");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioSecondaryLanguage(const int32_t handle, std::string &secondaryAudioLanguage) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            char langStr[32] = {0};
            
            // Use resolve function for dsGetSecondaryLanguage
            typedef dsError_t (*dsGetSecondaryLanguage_t)(intptr_t handle, char* language);
            static dsGetSecondaryLanguage_t dsGetSecondaryLanguageFunc = 0;
            if (dsGetSecondaryLanguageFunc == 0) {
                dsGetSecondaryLanguageFunc = (dsGetSecondaryLanguage_t)resolve(RDK_DSHAL_NAME, "dsGetSecondaryLanguage");
                if (dsGetSecondaryLanguageFunc == 0) {
                    DSLOG_ERR("dsGetSecondaryLanguage is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetSecondaryLanguageFunc) {
                ret = dsGetSecondaryLanguageFunc(dsHandle, langStr);
            }
            
            if (ret == dsERR_NONE) {
                secondaryAudioLanguage = std::string(langStr);
                DSLOG_INFO("success: handle=%d, language=%s", handle, secondaryAudioLanguage.c_str());
            } else {
                DSLOG_ERR("dsGetSecondaryLanguage failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioSecondaryLanguage");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t IsAudioOutputConnected(const int32_t handle, bool &isConnected) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            bool dsConnected;
            
            // dsAudio.c uses dsAudioOutIsConnected (not dsIsAudioPortEnabled)
            typedef dsError_t (*dsAudioOutIsConnected_t)(intptr_t handle, bool* isConnected);
            static dsAudioOutIsConnected_t dsAudioOutIsConnectedFunc = 0;
            if (dsAudioOutIsConnectedFunc == 0) {
                dsAudioOutIsConnectedFunc = (dsAudioOutIsConnected_t)resolve(RDK_DSHAL_NAME, "dsAudioOutIsConnected");
                if (dsAudioOutIsConnectedFunc == 0) {
                    DSLOG_ERR("dsAudioOutIsConnected is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsAudioOutIsConnectedFunc) {
                ret = dsAudioOutIsConnectedFunc(dsHandle, &dsConnected);
            }
            
            if (ret == dsERR_NONE) {
                isConnected = dsConnected;
                DSLOG_INFO("success: handle=%d, connected=%s", handle, isConnected ? "true" : "false");
            } else {
                DSLOG_ERR("dsAudioOutIsConnected failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in IsAudioOutputConnected");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioSinkDeviceAtmosCapability(const int32_t handle, DolbyAtmosCapability &atmosCapability) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // dsAtmosCapability_t should be dsATMOSCapability_t
            dsATMOSCapability_t dsCapability;
            
            // Use resolve function for dsGetSinkDeviceAtmosCapability
            typedef dsError_t (*dsGetSinkDeviceAtmosCapability_t)(intptr_t handle, dsATMOSCapability_t* capability);
            static dsGetSinkDeviceAtmosCapability_t dsGetSinkDeviceAtmosCapabilityFunc = 0;
            if (dsGetSinkDeviceAtmosCapabilityFunc == 0) {
                dsGetSinkDeviceAtmosCapabilityFunc = (dsGetSinkDeviceAtmosCapability_t)resolve(RDK_DSHAL_NAME, "dsGetSinkDeviceAtmosCapability");
                if (dsGetSinkDeviceAtmosCapabilityFunc == 0) {
                    DSLOG_ERR("dsGetSinkDeviceAtmosCapability is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetSinkDeviceAtmosCapabilityFunc) {
                ret = dsGetSinkDeviceAtmosCapabilityFunc(dsHandle, &dsCapability);
            }
            
            if (ret == dsERR_NONE) {
                atmosCapability = static_cast<DolbyAtmosCapability>(dsCapability);
                DSLOG_INFO("success: handle=%d, capability=%d", handle, static_cast<int>(atmosCapability));
            } else {
                DSLOG_ERR("dsGetSinkDeviceAtmosCapability failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioSinkDeviceAtmosCapability");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioAtmosOutputMode(const int32_t handle, const bool enable) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetAudioAtmosOutputMode
            typedef dsError_t (*dsSetAudioAtmosOutputMode_t)(intptr_t handle, bool enable);
            static dsSetAudioAtmosOutputMode_t dsSetAudioAtmosOutputModeFunc = 0;
            if (dsSetAudioAtmosOutputModeFunc == 0) {
                dsSetAudioAtmosOutputModeFunc = (dsSetAudioAtmosOutputMode_t)resolve(RDK_DSHAL_NAME, "dsSetAudioAtmosOutputMode");
                if (dsSetAudioAtmosOutputModeFunc == 0) {
                    DSLOG_ERR("dsSetAudioAtmosOutputMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetAudioAtmosOutputModeFunc) {
                ret = dsSetAudioAtmosOutputModeFunc(dsHandle, enable);
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, enable=%s", handle, enable ? "true" : "false");
            } else {
                DSLOG_ERR("dsSetAudioAtmosOutputMode failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioAtmosOutputMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    // Missing IDeviceSettingsAudio interface methods implementation

    uint32_t IsAudioPortEnabled(const int32_t handle, bool &enabled) override {
        ENTRY_LOG;
        try {
            bool portEnabled = false;
            
            // Use resolve function for dsIsAudioPortEnabled
            typedef dsError_t (*dsIsAudioPortEnabled_t)(intptr_t handle, bool* enabled);
            static dsIsAudioPortEnabled_t dsIsAudioPortEnabledFunc = 0;
            if (dsIsAudioPortEnabledFunc == 0) {
                dsIsAudioPortEnabledFunc = (dsIsAudioPortEnabled_t)resolve(RDK_DSHAL_NAME, "dsIsAudioPortEnabled");
                if (dsIsAudioPortEnabledFunc == 0) {
                    DSLOG_ERR("dsIsAudioPortEnabled is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsIsAudioPortEnabledFunc) {
                dsResult = dsIsAudioPortEnabledFunc(static_cast<intptr_t>(handle), &portEnabled);
            }
            
            if (dsResult == dsERR_NONE) {
                enabled = portEnabled;
                DSLOG_INFO("success: handle=%d, enabled=%s", handle, enabled ? "true" : "false");
            } else {
                DSLOG_ERR("dsIsAudioPortEnabled failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in IsAudioPortEnabled");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t EnableAudioPort(const int32_t handle, const bool enable) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            dsAudioPortType_t portType = getAudioPortType(dsHandle);

            // Special handling for SPEAKER port - manage audio ducking level
            if (portType == dsAUDIOPORT_TYPE_SPEAKER) {
                bool muted = false;
                dsError_t ret = dsIsAudioMute(dsHandle, &muted);
                if (ret != dsERR_NONE) {
                    DSLOG_WARN("Failed to get the mute status of Speaker port");
                }

                if (enable && !muted) {
                    if (setAudioDuckingAudioLevel(dsHandle) != WPEFramework::Core::ERROR_NONE) {
                        DSLOG_ERR("Failed to set audio ducking level for Speaker port");
                        return WPEFramework::Core::ERROR_GENERAL;
                    }
                } else {
                    DSLOG_INFO("Not setting audio ducking level as mute status is %s", muted ? "true" : "false");
                }
            }

            // Enable/disable the audio port
            // Use resolve function for dsEnableAudioPort
            typedef dsError_t (*dsEnableAudioPort_t)(intptr_t handle, bool enable);
            static dsEnableAudioPort_t dsEnableAudioPortFunc = 0;
            if (dsEnableAudioPortFunc == 0) {
                dsEnableAudioPortFunc = (dsEnableAudioPort_t)resolve(RDK_DSHAL_NAME, "dsEnableAudioPort");
                if (dsEnableAudioPortFunc == 0) {
                    DSLOG_ERR("dsEnableAudioPort is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsEnableAudioPortFunc) {
                dsResult = dsEnableAudioPortFunc(dsHandle, enable);
            }
            if (dsResult != dsERR_NONE) {
                DSLOG_ERR("dsEnableAudioPort failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }

#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
            if (enable && portType != dsAUDIOPORT_TYPE_SPEAKER) {
                float restoredVolume = 0.0f;
                bool hasCachedVolume = true;

                switch (portType) {
                    case dsAUDIOPORT_TYPE_SPDIF:
                        restoredVolume = g_audioLevelCacheSpdif.load();
                        break;
                    case dsAUDIOPORT_TYPE_HDMI:
                        restoredVolume = g_audioLevelCacheHdmi.load();
                        break;
                    case dsAUDIOPORT_TYPE_HEADPHONE:
                        restoredVolume = g_audioLevelCacheHeadphone.load();
                        break;
                    default:
                        hasCachedVolume = false;
                        break;
                }

                if (hasCachedVolume) {
                    typedef dsError_t (*dsSetAudioLevel_t)(intptr_t handle, float level);
                    static dsSetAudioLevel_t dsSetAudioLevelFunc = 0;
                    if (dsSetAudioLevelFunc == 0) {
                        dsSetAudioLevelFunc = (dsSetAudioLevel_t)resolve(RDK_DSHAL_NAME, "dsSetAudioLevel");
                    }

                    if (dsSetAudioLevelFunc != 0) {
                        dsResult = dsSetAudioLevelFunc(dsHandle, restoredVolume);
                        if (dsResult == dsERR_NONE) {
                            DSLOG_INFO("Restored audio level %f for enabled port type %d", restoredVolume, portType);
                        } else {
                            DSLOG_WARN("Failed to restore audio level for enabled port type %d: %d", portType, dsResult);
                        }
                    }
                }
            }
#endif

            // Verify that the port was actually enabled/disabled
            bool portEnabled = false;
            dsResult = dsIsAudioPortEnabled(dsHandle, &portEnabled);
            if (dsResult == dsERR_NONE) {
                if (portEnabled != enable) {
                    DSLOG_ERR("Audio port enable verification failed. Expected: %s, Actual: %s",
                           enable ? "enabled" : "disabled", portEnabled ? "enabled" : "disabled");
                    return WPEFramework::Core::ERROR_GENERAL;
                } else {
                    DSLOG_INFO("Audio port enable verification passed: %s", enable ? "enabled" : "disabled");

                    // Update port state tracking
                    if (portType < dsAUDIOPORT_TYPE_MAX) {
                        _audioPortEnabled[portType] = enable;
                        DSLOG_INFO("Port type %d enabled status: %s", portType, enable ? "true" : "false");

                        // Set audio delay when enabling port
                        if (enable) {
                            uint32_t audioDelay = getAudioDelayInternal(portType);
                            bool delaySet = setAudioDelayInternal(dsHandle, audioDelay);
                            DSLOG_INFO("Updated audio delay for port enable - port type: %d, delay: %u, success: %s",
                                   portType, audioDelay, delaySet ? "true" : "false");
                        }
                    }
                }
            } else {
                DSLOG_WARN("Audio port status verification failed - dsIsAudioPortEnabled call failed with error: %d", dsResult);
            }

            DSLOG_INFO("success: handle=%d, enable=%s", handle, enable ? "true" : "false");

        } catch (...) {
            DSLOG_ERR("Exception in EnableAudioPort");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetSupportedARCTypes(const int32_t handle, int32_t &types) override {
        ENTRY_LOG;
        try {
            int arcTypes = 0;
            
            // Use resolve function for dsGetSupportedARCTypes
            typedef dsError_t (*dsGetSupportedARCTypes_t)(intptr_t handle, int* types);
            static dsGetSupportedARCTypes_t dsGetSupportedARCTypesFunc = 0;
            if (dsGetSupportedARCTypesFunc == 0) {
                dsGetSupportedARCTypesFunc = (dsGetSupportedARCTypes_t)resolve(RDK_DSHAL_NAME, "dsGetSupportedARCTypes");
                if (dsGetSupportedARCTypesFunc == 0) {
                    DSLOG_ERR("dsGetSupportedARCTypes is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetSupportedARCTypesFunc) {
                dsResult = dsGetSupportedARCTypesFunc(static_cast<intptr_t>(handle), &arcTypes);
            }
            
            if (dsResult == dsERR_NONE) {
                types = arcTypes;
            } else {
                DSLOG_ERR("dsGetSupportedARCTypes failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetSupportedARCTypes");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetSAD(const int32_t handle, const uint8_t sadList[], const uint8_t count) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // dsAudio.c uses dsAudioSetSAD (not dsSetSAD)
            typedef dsError_t (*dsAudioSetSAD_t)(intptr_t handle, dsAudioSADList_t sad_list);
            static dsAudioSetSAD_t dsAudioSetSADFunc = 0;
            if (dsAudioSetSADFunc == 0) {
                dsAudioSetSADFunc = (dsAudioSetSAD_t)resolve(RDK_DSHAL_NAME, "dsAudioSetSAD");
                if(dsAudioSetSADFunc == 0) {
                    DSLOG_ERR("dsAudioSetSAD is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

            dsAudioSADList_t sadList_hal;
            memcpy(sadList_hal.sad, sadList, count < 15 ? count : 15);
            sadList_hal.count = count;
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsAudioSetSADFunc) {
                ret = dsAudioSetSADFunc(dsHandle, sadList_hal);
            }

            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, count=%d", handle, count);
            } else {
                DSLOG_ERR("dsSetSAD failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetSAD");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t EnableARC(const int32_t handle, const WPEFramework::Exchange::IDeviceSettingsAudio::AudioARCStatus arcStatus) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            dsAudioARCStatus_t dsARCStatus;
            dsARCStatus.type = static_cast<dsAudioARCTypes_t>(arcStatus.arcType);
            dsARCStatus.status = arcStatus.status;
            
            // dsAudio.c uses dsAudioEnableARC (not dsEnableARC)
            typedef dsError_t (*dsAudioEnableARC_t)(intptr_t handle, dsAudioARCStatus_t arcStatus);
            static dsAudioEnableARC_t dsAudioEnableARCFunc = 0;
            if (dsAudioEnableARCFunc == 0) {
                dsAudioEnableARCFunc = (dsAudioEnableARC_t)resolve(RDK_DSHAL_NAME, "dsAudioEnableARC");
                if(dsAudioEnableARCFunc == 0) {
                    DSLOG_ERR("dsAudioEnableARC is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsAudioEnableARCFunc) {
                ret = dsAudioEnableARCFunc(dsHandle, dsARCStatus);
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, arcStatus type=%d status=%d", handle, static_cast<int>(arcStatus.arcType), static_cast<int>(arcStatus.status));
            } else {
                DSLOG_ERR("dsEnableARC failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in EnableARC");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioEnablePersist(const int32_t handle, bool &enabled, string &portName) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            // By default all ports are enabled
            enabled = true;

            std::string isEnabledAudioPortKey("audio.");
            isEnabledAudioPortKey.append(portName);
            isEnabledAudioPortKey.append(".isEnabled");
            std::string _AudioPortEnable("TRUE");

            try {
                _AudioPortEnable = device::HostPersistence::getInstance().getProperty(isEnabledAudioPortKey);
            }
            catch(...) {
                try {
                    DSLOG_INFO(" %s port enable settings not found in persistence store. Try system default", isEnabledAudioPortKey.c_str());
                    _AudioPortEnable = device::HostPersistence::getInstance().getDefaultProperty(isEnabledAudioPortKey);
                }
                catch(...) {
                    // By default enable all the ports
                    _AudioPortEnable = "TRUE";
                }
            }

            if ("FALSE" == _AudioPortEnable) { 
                DSLOG_INFO(" persist dsEnableAudioPort value: %s", _AudioPortEnable.c_str());
                enabled = false;
            }
            else {
                DSLOG_INFO(" persist dsEnableAudioPort value: %s", _AudioPortEnable.c_str());
                enabled = true;
            }

            DSLOG_INFO("success: handle=%d, portName=%s, enabled=%s, key=%s, value=%s",
                   handle, portName.c_str(), enabled ? "TRUE" : "FALSE", isEnabledAudioPortKey.c_str(), _AudioPortEnable.c_str());
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioEnablePersist");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioEnablePersist(const int32_t handle, const bool enable, const string portName) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            std::string isEnabledAudioPortKey("audio.");
            isEnabledAudioPortKey.append(portName);
            isEnabledAudioPortKey.append(".isEnabled");

            std::string enableValue = enable ? "TRUE" : "FALSE";
            device::HostPersistence::getInstance().persistHostProperty(isEnabledAudioPortKey.c_str(), enableValue.c_str());

            DSLOG_INFO("success: handle=%d, portName=%s, enable=%s, key=%s",
                   handle, portName.c_str(), enableValue.c_str(), isEnabledAudioPortKey.c_str());
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioEnablePersist");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t IsAudioMSDecoded(const int32_t handle, bool &hasms11Decode) override {
        ENTRY_LOG;
        try {
            bool ms11Decoded = false;
            
            // Use resolve function for dsIsAudioMSDecode
            typedef dsError_t (*dsIsAudioMSDecode_t)(intptr_t handle, bool* decoded);
            static dsIsAudioMSDecode_t dsIsAudioMSDecodeFunc = 0;
            if (dsIsAudioMSDecodeFunc == 0) {
                dsIsAudioMSDecodeFunc = (dsIsAudioMSDecode_t)resolve(RDK_DSHAL_NAME, "dsIsAudioMSDecode");
                if (dsIsAudioMSDecodeFunc == 0) {
                    DSLOG_ERR("dsIsAudioMSDecode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsIsAudioMSDecodeFunc) {
                dsResult = dsIsAudioMSDecodeFunc(static_cast<intptr_t>(handle), &ms11Decoded);
            }
            
            if (dsResult == dsERR_NONE) {
                hasms11Decode = ms11Decoded;
            } else {
                DSLOG_ERR("dsIsAudioMSDecode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in IsAudioMSDecoded");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t IsAudioMS12Decoded(const int32_t handle, bool &hasms12Decode) override {
        ENTRY_LOG;
        try {
            bool ms12Decoded = false;
            dsError_t dsResult = dsIsAudioMS12Decode(static_cast<intptr_t>(handle), &ms12Decoded);
            if (dsResult == dsERR_NONE) {
                hasms12Decode = ms12Decoded;
            } else {
                DSLOG_ERR("dsIsAudioMS12Decode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in IsAudioMS12Decoded");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioLEConfig(const int32_t handle, bool &enabled) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            bool leEnabled;
            // Use resolve function for dsGetLEConfig
            typedef dsError_t (*dsGetLEConfig_t)(intptr_t handle, bool* enabled);
            static dsGetLEConfig_t dsGetLEConfigFunc = 0;
            if (dsGetLEConfigFunc == 0) {
                dsGetLEConfigFunc = (dsGetLEConfig_t)resolve(RDK_DSHAL_NAME, "dsGetLEConfig");
                if (dsGetLEConfigFunc == 0) {
                    DSLOG_ERR("dsGetLEConfig is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetLEConfigFunc) {
                ret = dsGetLEConfigFunc(dsHandle, &leEnabled);
            }
            if (ret == dsERR_NONE) {
                enabled = leEnabled;
                DSLOG_INFO("success: handle=%d, enabled=%s", handle, enabled ? "true" : "false");
            } else {
                DSLOG_ERR("dsGetLEConfig failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioLEConfig");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t EnableAudioLEConfig(const int32_t handle, const bool enable) override {
        ENTRY_LOG;
        try {
            // dsAudio.c uses dsEnableLEConfig(handle, enable) — NOT dsEnableMS12Config
            typedef dsError_t (*dsEnableLEConfig_t)(intptr_t handle, const bool enable);
            static dsEnableLEConfig_t dsEnableLEConfigFunc = nullptr;
            if (dsEnableLEConfigFunc == nullptr) {
                dsEnableLEConfigFunc = (dsEnableLEConfig_t)resolve(RDK_DSHAL_NAME, "dsEnableLEConfig");
                if (dsEnableLEConfigFunc == nullptr) {
                    DSLOG_ERR("dsEnableLEConfig is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            /* Mirror dsAudio.c _dsEnableLEConfig: only call HAL and persist
             * when the value actually changes — avoids redundant HAL calls. */
            if (enable != m_LEEnabled) {
                m_LEEnabled = enable;
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.LEEnable", enable ? "TRUE" : "FALSE");
#endif
                dsError_t dsResult = dsEnableLEConfigFunc(static_cast<intptr_t>(handle), enable);
                if (dsResult != dsERR_NONE) {
                    DSLOG_ERR("dsEnableLEConfig failed with error: %d", dsResult);
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
        } catch (...) {
            DSLOG_ERR("Exception in EnableAudioLEConfig");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetApplicationAudioConfig(const int32_t handle, const std::string& audioConfig, const bool enable) override {
        ENTRY_LOG;
        try {
            using SetApplicationAudioConfigFunction = dsError_t (*)(intptr_t, dsApplicationAudioConfig_t*, bool);
            static SetApplicationAudioConfigFunction dsSetApplicationAudioConfigFunc = nullptr;
            if (dsSetApplicationAudioConfigFunc == nullptr) {
                dsSetApplicationAudioConfigFunc = reinterpret_cast<SetApplicationAudioConfigFunction>(resolve(RDK_DSHAL_NAME, "dsSetApplicationAudioConfig"));
                if (dsSetApplicationAudioConfigFunc == nullptr) {
                    DSLOG_ERR("dsSetApplicationAudioConfig is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

            dsApplicationAudioConfig_t config = {};
            strncpy(config.configName, audioConfig.c_str(), DS_MAX_APPLICATION_AUDIO_CONFIG_NAME_LEN - 1);
            const dsError_t result = dsSetApplicationAudioConfigFunc(static_cast<intptr_t>(handle), &config, enable);
            if (result != dsERR_NONE) {
                DSLOG_ERR("dsSetApplicationAudioConfig failed with error: %d", result);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetApplicationAudioConfig");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetApplicationAudioConfig(const int32_t handle, const std::string& audioConfig, bool& enabled) override {
        ENTRY_LOG;
        try {
            using GetApplicationAudioConfigFunction = dsError_t (*)(intptr_t, dsApplicationAudioConfig_t*, bool*);
            static GetApplicationAudioConfigFunction dsGetApplicationAudioConfigFunc = nullptr;
            if (dsGetApplicationAudioConfigFunc == nullptr) {
                dsGetApplicationAudioConfigFunc = reinterpret_cast<GetApplicationAudioConfigFunction>(resolve(RDK_DSHAL_NAME, "dsGetApplicationAudioConfig"));
                if (dsGetApplicationAudioConfigFunc == nullptr) {
                    DSLOG_ERR("dsGetApplicationAudioConfig is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

            dsApplicationAudioConfig_t config = {};
            strncpy(config.configName, audioConfig.c_str(), DS_MAX_APPLICATION_AUDIO_CONFIG_NAME_LEN - 1);
            bool isEnabled = false;
            const dsError_t result = dsGetApplicationAudioConfigFunc(static_cast<intptr_t>(handle), &config, &isEnabled);
            if (result != dsERR_NONE) {
                DSLOG_ERR("dsGetApplicationAudioConfig failed with error: %d", result);
                return WPEFramework::Core::ERROR_GENERAL;
            }
            enabled = isEnabled;
        } catch (...) {
            DSLOG_ERR("Exception in GetApplicationAudioConfig");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetApplicationAudioConfigList(const int32_t handle, IDeviceSettingsAudioApplicationConfigIterator*& configList) const override {
        ENTRY_LOG;
        configList = nullptr;
        try {
            using GetApplicationAudioConfigListFunction = dsError_t (*)(intptr_t, dsApplicationAudioConfigList_t*);
            static GetApplicationAudioConfigListFunction dsGetApplicationAudioConfigListFunc = nullptr;
            if (dsGetApplicationAudioConfigListFunc == nullptr) {
                dsGetApplicationAudioConfigListFunc = reinterpret_cast<GetApplicationAudioConfigListFunction>(resolve(RDK_DSHAL_NAME, "dsGetApplicationAudioConfigList"));
                if (dsGetApplicationAudioConfigListFunc == nullptr) {
                    DSLOG_ERR("dsGetApplicationAudioConfigList is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

            dsApplicationAudioConfigList_t halConfigList = {};
            halConfigList.size = sizeof(halConfigList);
            const dsError_t result = dsGetApplicationAudioConfigListFunc(static_cast<intptr_t>(handle), &halConfigList);
            if (result != dsERR_NONE) {
                DSLOG_ERR("dsGetApplicationAudioConfigList failed with error: %d", result);
                return WPEFramework::Core::ERROR_GENERAL;
            }

            std::vector<ApplicationAudioConfig> configurations;
            const size_t capacity = sizeof(halConfigList.config) / sizeof(halConfigList.config[0]);
            const size_t count = (halConfigList.returnedCount < capacity) ? halConfigList.returnedCount : capacity;
            configurations.reserve(count);
            for (size_t index = 0; index < count; ++index) {
                ApplicationAudioConfig config;
                config.configName = halConfigList.config[index].configName;
                configurations.emplace_back(config);
            }

            using ApplicationConfigIterator = WPEFramework::RPC::IteratorType<IDeviceSettingsAudioApplicationConfigIterator>;
            configList = WPEFramework::Core::Service<ApplicationConfigIterator>::Create<IDeviceSettingsAudioApplicationConfigIterator>(configurations);
        } catch (...) {
            DSLOG_ERR("Exception in GetApplicationAudioConfigList");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioDelay(const int32_t handle, const uint32_t audioDelay) override {
        ENTRY_LOG;
        try {
            // Use resolve function for dsSetAudioDelay
            typedef dsError_t (*dsSetAudioDelay_t)(intptr_t handle, uint32_t audioDelay);
            static dsSetAudioDelay_t dsSetAudioDelayFunc = 0;
            if (dsSetAudioDelayFunc == 0) {
                dsSetAudioDelayFunc = (dsSetAudioDelay_t)resolve(RDK_DSHAL_NAME, "dsSetAudioDelay");
                if (dsSetAudioDelayFunc == 0) {
                    DSLOG_ERR("dsSetAudioDelay is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

            // dsAudio.c _dsSetAudioDelay: only invoke the HAL setter while the port is enabled;
            // persistence still happens unconditionally afterward.
            dsAudioPortType_t _portType = getAudioPortType(static_cast<intptr_t>(handle));
            uint32_t retCode = WPEFramework::Core::ERROR_NONE;
            if (_portType < dsAUDIOPORT_TYPE_MAX && _audioPortEnabled[_portType]) {
                dsError_t dsResult = dsSetAudioDelayFunc(static_cast<intptr_t>(handle), audioDelay);
                if (dsResult == dsERR_NONE) {
                    DSLOG_INFO("success: handle=%d, delay=%u", handle, audioDelay);
                } else {
                    DSLOG_ERR("dsSetAudioDelay failed with error: %d", dsResult);
                    retCode = WPEFramework::Core::ERROR_GENERAL;
                }
            } else {
                DSLOG_INFO("Not setting audiodelay as port is not enabled: handle=%d", handle);
            }

#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
            std::string _delay = std::to_string(audioDelay);
            switch (_portType) {
                case dsAUDIOPORT_TYPE_SPDIF:    device::HostPersistence::getInstance().persistHostProperty("SPDIF0.audio.Delay",    _delay); break;
                case dsAUDIOPORT_TYPE_HDMI:     device::HostPersistence::getInstance().persistHostProperty("HDMI0.audio.Delay",     _delay); break;
                case dsAUDIOPORT_TYPE_SPEAKER:  device::HostPersistence::getInstance().persistHostProperty("SPEAKER0.audio.Delay",  _delay); break;
                case dsAUDIOPORT_TYPE_HDMI_ARC: device::HostPersistence::getInstance().persistHostProperty("HDMI_ARC0.audio.Delay", _delay); break;
                default: break;
            }
#endif
            EXIT_LOG;
            return retCode;
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioDelay");
            return WPEFramework::Core::ERROR_GENERAL;
        }
    }

    uint32_t GetAudioDelay(const int32_t handle, uint32_t &audioDelay) override {
        ENTRY_LOG;
        try {
            // dsAudio.c _dsGetAudioDelay never calls a HAL getter; it always reads the
            // persisted value via dsGetAudioDelayInternal(). Mirror that exactly here.
            dsAudioPortType_t _portType = getAudioPortType(static_cast<intptr_t>(handle));
            audioDelay = getAudioDelayInternal(_portType);
            DSLOG_INFO("success: handle=%d, delay=%u", handle, audioDelay);
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioDelay");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioDelayOffset(const int32_t handle, const uint32_t delayOffset) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            typedef dsError_t (*dsSetAudioDelayOffset_t)(intptr_t handle, uint32_t delayOffset);
            static dsSetAudioDelayOffset_t dsSetAudioDelayOffsetFunc = 0;
            if (dsSetAudioDelayOffsetFunc == 0) {
                dsSetAudioDelayOffsetFunc = (dsSetAudioDelayOffset_t)resolve(RDK_DSHAL_NAME, "dsSetAudioDelayOffset");
                if(dsSetAudioDelayOffsetFunc == 0) {
                    DSLOG_ERR("dsSetAudioDelayOffset is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetAudioDelayOffsetFunc) {
                ret = dsSetAudioDelayOffsetFunc(dsHandle, delayOffset);
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, offset=%u", handle, delayOffset);
            } else {
                DSLOG_ERR("dsSetAudioDelayOffset failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioDelayOffset");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioDelayOffset(const int32_t handle, uint32_t &delayOffset) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            uint32_t dsOffset;
            
            typedef dsError_t (*dsGetAudioDelayOffset_t)(intptr_t handle, uint32_t* delayOffset);
            static dsGetAudioDelayOffset_t dsGetAudioDelayOffsetFunc = 0;
            if (dsGetAudioDelayOffsetFunc == 0) {
                dsGetAudioDelayOffsetFunc = (dsGetAudioDelayOffset_t)resolve(RDK_DSHAL_NAME, "dsGetAudioDelayOffset");
                if(dsGetAudioDelayOffsetFunc == 0) {
                    DSLOG_ERR("dsGetAudioDelayOffset is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsGetAudioDelayOffsetFunc) {
                ret = dsGetAudioDelayOffsetFunc(dsHandle, &dsOffset);
            }
            
            if (ret == dsERR_NONE) {
                delayOffset = dsOffset;
                DSLOG_INFO("success: handle=%d, offset=%u", handle, delayOffset);
            } else {
                DSLOG_ERR("dsGetAudioDelayOffset failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioDelayOffset");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioCompression(const int32_t handle, const int32_t compressionLevel) override {
        ENTRY_LOG;
        try {
            // Use resolve function for dsSetAudioCompression
            typedef dsError_t (*dsSetAudioCompression_t)(intptr_t handle, int compression);
            static dsSetAudioCompression_t dsSetAudioCompressionFunc = 0;
            if (dsSetAudioCompressionFunc == 0) {
                dsSetAudioCompressionFunc = (dsSetAudioCompression_t)resolve(RDK_DSHAL_NAME, "dsSetAudioCompression");
                if (dsSetAudioCompressionFunc == 0) {
                    DSLOG_ERR("dsSetAudioCompression is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsSetAudioCompressionFunc) {
                dsResult = dsSetAudioCompressionFunc(static_cast<intptr_t>(handle), compressionLevel);
            }
            if (dsResult == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, level=%d", handle, compressionLevel);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.Compression", std::to_string(compressionLevel));
#endif
            } else {
                DSLOG_ERR("dsSetAudioCompression failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioCompression");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioCompression(const int32_t handle, int32_t &compressionLevel) override {
        ENTRY_LOG;
        try {
            int compression = 0;
            // Use resolve function for dsGetAudioCompression
            typedef dsError_t (*dsGetAudioCompression_t)(intptr_t handle, int* compression);
            static dsGetAudioCompression_t dsGetAudioCompressionFunc = 0;
            if (dsGetAudioCompressionFunc == 0) {
                dsGetAudioCompressionFunc = (dsGetAudioCompression_t)resolve(RDK_DSHAL_NAME, "dsGetAudioCompression");
                if (dsGetAudioCompressionFunc == 0) {
                    DSLOG_ERR("dsGetAudioCompression is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetAudioCompressionFunc) {
                dsResult = dsGetAudioCompressionFunc(static_cast<intptr_t>(handle), &compression);
            }
            if (dsResult == dsERR_NONE) {
                compressionLevel = compression;
                DSLOG_INFO("success: handle=%d, level=%d", handle, compressionLevel);
            } else {
                DSLOG_ERR("dsGetAudioCompression failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioCompression");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioDialogEnhancement(const int32_t handle, const int32_t level) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Use resolve function for dsSetDialogEnhancement
            typedef dsError_t (*dsSetDialogEnhancement_t)(intptr_t handle, int level);
            static dsSetDialogEnhancement_t dsSetDialogEnhancementFunc = 0;
            if (dsSetDialogEnhancementFunc == 0) {
                dsSetDialogEnhancementFunc = (dsSetDialogEnhancement_t)resolve(RDK_DSHAL_NAME, "dsSetDialogEnhancement");
                if (dsSetDialogEnhancementFunc == 0) {
                    DSLOG_ERR("dsSetDialogEnhancement is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetDialogEnhancementFunc) {
                ret = dsSetDialogEnhancementFunc(dsHandle, level);
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, level=%d", handle, level);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty(getCurrentProfileProperty("EnhancerLevel"), std::to_string(level));
#endif
            } else {
                DSLOG_ERR("dsSetDialogEnhancement failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioDialogEnhancement");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioDialogEnhancement(const int32_t handle, int32_t &level) override {
        ENTRY_LOG;
        try {
            int dialogLevel = 0;
            // Use resolve function for dsGetDialogEnhancement
            typedef dsError_t (*dsGetDialogEnhancement_t)(intptr_t handle, int* level);
            static dsGetDialogEnhancement_t dsGetDialogEnhancementFunc = 0;
            if (dsGetDialogEnhancementFunc == 0) {
                dsGetDialogEnhancementFunc = (dsGetDialogEnhancement_t)resolve(RDK_DSHAL_NAME, "dsGetDialogEnhancement");
                if (dsGetDialogEnhancementFunc == 0) {
                    DSLOG_ERR("dsGetDialogEnhancement is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetDialogEnhancementFunc) {
                dsResult = dsGetDialogEnhancementFunc(static_cast<intptr_t>(handle), &dialogLevel);
            }
            if (dsResult == dsERR_NONE) {
                level = dialogLevel;
                DSLOG_INFO("success: handle=%d, level=%d", handle, level);
            } else {
                DSLOG_ERR("dsGetDialogEnhancement failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioDialogEnhancement");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioDolbyVolumeMode(const int32_t handle, const bool enable) override {
        ENTRY_LOG;
        try {
            // Use resolve function for dsSetDolbyVolumeMode
            typedef dsError_t (*dsSetDolbyVolumeMode_t)(intptr_t handle, bool enable);
            static dsSetDolbyVolumeMode_t dsSetDolbyVolumeModeFunc = 0;
            if (dsSetDolbyVolumeModeFunc == 0) {
                dsSetDolbyVolumeModeFunc = (dsSetDolbyVolumeMode_t)resolve(RDK_DSHAL_NAME, "dsSetDolbyVolumeMode");
                if (dsSetDolbyVolumeModeFunc == 0) {
                    DSLOG_ERR("dsSetDolbyVolumeMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsSetDolbyVolumeModeFunc) {
                dsResult = dsSetDolbyVolumeModeFunc(static_cast<intptr_t>(handle), enable);
            }
            if (dsResult == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, enable=%s", handle, enable ? "true" : "false");
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.DolbyVolumeMode", enable ? "TRUE" : "FALSE");
#endif
            } else {
                DSLOG_ERR("dsSetDolbyVolumeMode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioDolbyVolumeMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioDolbyVolumeMode(const int32_t handle, bool &enabled) override {
        ENTRY_LOG;
        try {
            bool dolbyMode = false;
            // Use resolve function for dsGetDolbyVolumeMode
            typedef dsError_t (*dsGetDolbyVolumeMode_t)(intptr_t handle, bool* mode);
            static dsGetDolbyVolumeMode_t dsGetDolbyVolumeModeFunc = 0;
            if (dsGetDolbyVolumeModeFunc == 0) {
                dsGetDolbyVolumeModeFunc = (dsGetDolbyVolumeMode_t)resolve(RDK_DSHAL_NAME, "dsGetDolbyVolumeMode");
                if (dsGetDolbyVolumeModeFunc == 0) {
                    DSLOG_ERR("dsGetDolbyVolumeMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetDolbyVolumeModeFunc) {
                dsResult = dsGetDolbyVolumeModeFunc(static_cast<intptr_t>(handle), &dolbyMode);
            }
            if (dsResult == dsERR_NONE) {
                enabled = dolbyMode;
                DSLOG_INFO("success: handle=%d, enabled=%s", handle, enabled ? "true" : "false");
            } else {
                DSLOG_ERR("dsGetDolbyVolumeMode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioDolbyVolumeMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioIntelligentEqualizerMode(const int32_t handle, const int32_t mode) override {
        ENTRY_LOG;
        try {
            // Use resolve function for dsSetIntelligentEqualizerMode
            typedef dsError_t (*dsSetIntelligentEqualizerMode_t)(intptr_t handle, int mode);
            static dsSetIntelligentEqualizerMode_t dsSetIntelligentEqualizerModeFunc = 0;
            if (dsSetIntelligentEqualizerModeFunc == 0) {
                dsSetIntelligentEqualizerModeFunc = (dsSetIntelligentEqualizerMode_t)resolve(RDK_DSHAL_NAME, "dsSetIntelligentEqualizerMode");
                if (dsSetIntelligentEqualizerModeFunc == 0) {
                    DSLOG_ERR("dsSetIntelligentEqualizerMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsSetIntelligentEqualizerModeFunc) {
                dsResult = dsSetIntelligentEqualizerModeFunc(static_cast<intptr_t>(handle), mode);
            }
            if (dsResult == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, mode=%d", handle, mode);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.IntelligentEQ", std::to_string(mode));
#endif
            } else {
                DSLOG_ERR("dsSetIntelligentEqualizerMode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioIntelligentEqualizerMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioIntelligentEqualizerMode(const int32_t handle, int32_t &mode) override {
        ENTRY_LOG;
        try {
            int eqMode = 0;
            // Use resolve function for dsGetIntelligentEqualizerMode
            typedef dsError_t (*dsGetIntelligentEqualizerMode_t)(intptr_t handle, int* mode);
            static dsGetIntelligentEqualizerMode_t dsGetIntelligentEqualizerModeFunc = 0;
            if (dsGetIntelligentEqualizerModeFunc == 0) {
                dsGetIntelligentEqualizerModeFunc = (dsGetIntelligentEqualizerMode_t)resolve(RDK_DSHAL_NAME, "dsGetIntelligentEqualizerMode");
                if (dsGetIntelligentEqualizerModeFunc == 0) {
                    DSLOG_ERR("dsGetIntelligentEqualizerMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetIntelligentEqualizerModeFunc) {
                dsResult = dsGetIntelligentEqualizerModeFunc(static_cast<intptr_t>(handle), &eqMode);
            }
            if (dsResult == dsERR_NONE) {
                mode = eqMode;
                DSLOG_INFO("success: handle=%d, mode=%d", handle, mode);
            } else {
                DSLOG_ERR("dsGetIntelligentEqualizerMode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioIntelligentEqualizerMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioVolumeLeveller(const int32_t handle, const WPEFramework::Exchange::IDeviceSettingsAudio::VolumeLeveller volumeLeveller) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            dsVolumeLeveller_t dsVolLeveller;
            dsVolLeveller.mode = static_cast<int>(volumeLeveller.mode);
            dsVolLeveller.level = static_cast<int>(volumeLeveller.level);
            // Use resolve function for dsSetVolumeLeveller
            typedef dsError_t (*dsSetVolumeLeveller_t)(intptr_t handle, dsVolumeLeveller_t leveller);
            static dsSetVolumeLeveller_t dsSetVolumeLevellerFunc = 0;
            if (dsSetVolumeLevellerFunc == 0) {
                dsSetVolumeLevellerFunc = (dsSetVolumeLeveller_t)resolve(RDK_DSHAL_NAME, "dsSetVolumeLeveller");
                if (dsSetVolumeLevellerFunc == 0) {
                    DSLOG_ERR("dsSetVolumeLeveller is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetVolumeLevellerFunc) {
                ret = dsSetVolumeLevellerFunc(dsHandle, dsVolLeveller);
            }
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, mode=%d, level=%d", handle, volumeLeveller.mode, volumeLeveller.level);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                std::string _PropertyMode  = getCurrentProfileProperty("VolumeLeveller.mode");
                std::string _PropertyLevel = getCurrentProfileProperty("VolumeLeveller.level");
                device::HostPersistence::getInstance().persistHostProperty(_PropertyMode, std::to_string(volumeLeveller.mode));
                if ((volumeLeveller.mode == 0) || (volumeLeveller.mode == 1)) {
                    device::HostPersistence::getInstance().persistHostProperty(_PropertyLevel, std::to_string(volumeLeveller.level));
                }
#endif
            } else {
                DSLOG_ERR("dsSetVolumeLeveller failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioVolumeLeveller");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioVolumeLeveller(const int32_t handle, WPEFramework::Exchange::IDeviceSettingsAudio::VolumeLeveller &volumeLeveller) override {
        ENTRY_LOG;
        try {
            dsVolumeLeveller_t volLeveller;
            // Use resolve function for dsGetVolumeLeveller
            typedef dsError_t (*dsGetVolumeLeveller_t)(intptr_t handle, dsVolumeLeveller_t* leveller);
            static dsGetVolumeLeveller_t dsGetVolumeLevellerFunc = 0;
            if (dsGetVolumeLevellerFunc == 0) {
                dsGetVolumeLevellerFunc = (dsGetVolumeLeveller_t)resolve(RDK_DSHAL_NAME, "dsGetVolumeLeveller");
                if (dsGetVolumeLevellerFunc == 0) {
                    DSLOG_ERR("dsGetVolumeLeveller is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetVolumeLevellerFunc) {
                dsResult = dsGetVolumeLevellerFunc(static_cast<intptr_t>(handle), &volLeveller);
            }
            if (dsResult == dsERR_NONE) {
                // Convert dsVolumeLeveller_t to VolumeLeveller enum
                volumeLeveller.mode = static_cast<uint8_t>(volLeveller.mode);
                volumeLeveller.level = static_cast<uint8_t>(volLeveller.level);
            } else {
                DSLOG_ERR("dsGetVolumeLeveller failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioVolumeLeveller");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioBassEnhancer(const int32_t handle, const int32_t boost) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // Use resolve function for dsSetBassEnhancer
            typedef dsError_t (*dsSetBassEnhancer_t)(intptr_t handle, int boost);
            static dsSetBassEnhancer_t dsSetBassEnhancerFunc = 0;
            if (dsSetBassEnhancerFunc == 0) {
                dsSetBassEnhancerFunc = (dsSetBassEnhancer_t)resolve(RDK_DSHAL_NAME, "dsSetBassEnhancer");
                if (dsSetBassEnhancerFunc == 0) {
                    DSLOG_ERR("dsSetBassEnhancer is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetBassEnhancerFunc) {
                ret = dsSetBassEnhancerFunc(dsHandle, boost);
            }
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, boost=%d", handle, boost);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.BassBoost", std::to_string(boost));
#endif
            } else {
                DSLOG_ERR("dsSetBassEnhancer failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioBassEnhancer");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioBassEnhancer(const int32_t handle, int32_t &boost) override {
        ENTRY_LOG;
        try {
            int bassBoost = 0;
            // Use resolve function for dsGetBassEnhancer
            typedef dsError_t (*dsGetBassEnhancer_t)(intptr_t handle, int* boost);
            static dsGetBassEnhancer_t dsGetBassEnhancerFunc = 0;
            if (dsGetBassEnhancerFunc == 0) {
                dsGetBassEnhancerFunc = (dsGetBassEnhancer_t)resolve(RDK_DSHAL_NAME, "dsGetBassEnhancer");
                if (dsGetBassEnhancerFunc == 0) {
                    DSLOG_ERR("dsGetBassEnhancer is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetBassEnhancerFunc) {
                dsResult = dsGetBassEnhancerFunc(static_cast<intptr_t>(handle), &bassBoost);
            }
            if (dsResult == dsERR_NONE) {
                boost = bassBoost;
            } else {
                DSLOG_ERR("dsGetBassEnhancer failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioBassEnhancer");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t EnableAudioSurroudDecoder(const int32_t handle, const bool enable) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // Use resolve function for dsEnableSurroundDecoder
            typedef dsError_t (*dsEnableSurroundDecoder_t)(intptr_t handle, bool enable);
            static dsEnableSurroundDecoder_t dsEnableSurroundDecoderFunc = 0;
            if (dsEnableSurroundDecoderFunc == 0) {
                dsEnableSurroundDecoderFunc = (dsEnableSurroundDecoder_t)resolve(RDK_DSHAL_NAME, "dsEnableSurroundDecoder");
                if (dsEnableSurroundDecoderFunc == 0) {
                    DSLOG_ERR("dsEnableSurroundDecoder is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsEnableSurroundDecoderFunc) {
                ret = dsEnableSurroundDecoderFunc(dsHandle, enable);
            }
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, enable=%s", handle, enable ? "true" : "false");
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.SurroundDecoderEnabled", enable ? "TRUE" : "FALSE");
#endif
            } else {
                DSLOG_ERR("dsEnableSurroundDecoder failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in EnableAudioSurroudDecoder");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t IsAudioSurroudDecoderEnabled(const int32_t handle, bool &enabled) override {
        ENTRY_LOG;
        try {
            bool decoderEnabled = false;
            // Use resolve function for dsIsSurroundDecoderEnabled
            typedef dsError_t (*dsIsSurroundDecoderEnabled_t)(intptr_t handle, bool* enabled);
            static dsIsSurroundDecoderEnabled_t dsIsSurroundDecoderEnabledFunc = 0;
            if (dsIsSurroundDecoderEnabledFunc == 0) {
                dsIsSurroundDecoderEnabledFunc = (dsIsSurroundDecoderEnabled_t)resolve(RDK_DSHAL_NAME, "dsIsSurroundDecoderEnabled");
                if (dsIsSurroundDecoderEnabledFunc == 0) {
                    DSLOG_ERR("dsIsSurroundDecoderEnabled is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsIsSurroundDecoderEnabledFunc) {
                dsResult = dsIsSurroundDecoderEnabledFunc(static_cast<intptr_t>(handle), &decoderEnabled);
            }
            if (dsResult == dsERR_NONE) {
                enabled = decoderEnabled;
            } else {
                DSLOG_ERR("dsIsSurroundDecoderEnabled failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in IsAudioSurroudDecoderEnabled");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioDRCMode(const int32_t handle, const int32_t drcMode) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // Use resolve function for dsSetDRCMode
            typedef dsError_t (*dsSetDRCMode_t)(intptr_t handle, int mode);
            static dsSetDRCMode_t dsSetDRCModeFunc = 0;
            if (dsSetDRCModeFunc == 0) {
                dsSetDRCModeFunc = (dsSetDRCMode_t)resolve(RDK_DSHAL_NAME, "dsSetDRCMode");
                if (dsSetDRCModeFunc == 0) {
                    DSLOG_ERR("dsSetDRCMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetDRCModeFunc) {
                ret = dsSetDRCModeFunc(dsHandle, drcMode);
            }
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, drcMode=%d", handle, drcMode);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.DRCMode", drcMode ? "RF" : "Line");
#endif
            } else {
                DSLOG_ERR("dsSetDRCMode failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioDRCMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioDRCMode(const int32_t handle, int32_t &drcMode) override {
        ENTRY_LOG;
        try {
            int mode = 0;
            // Use resolve function for dsGetDRCMode
            typedef dsError_t (*dsGetDRCMode_t)(intptr_t handle, int* mode);
            static dsGetDRCMode_t dsGetDRCModeFunc = 0;
            if (dsGetDRCModeFunc == 0) {
                dsGetDRCModeFunc = (dsGetDRCMode_t)resolve(RDK_DSHAL_NAME, "dsGetDRCMode");
                if (dsGetDRCModeFunc == 0) {
                    DSLOG_ERR("dsGetDRCMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetDRCModeFunc) {
                dsResult = dsGetDRCModeFunc(static_cast<intptr_t>(handle), &mode);
            }
            if (dsResult == dsERR_NONE) {
                drcMode = mode;
            } else {
                DSLOG_ERR("dsGetDRCMode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioDRCMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioSurroudVirtualizer(const int32_t handle, const WPEFramework::Exchange::IDeviceSettingsAudio::SurroundVirtualizer surroundVirtualizer) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            dsSurroundVirtualizer_t dsSurVirtualizer;
            dsSurVirtualizer.mode = static_cast<int>(surroundVirtualizer.mode);
            dsSurVirtualizer.boost = surroundVirtualizer.boost;
            // Use resolve function for dsSetSurroundVirtualizer
            typedef dsError_t (*dsSetSurroundVirtualizer_t)(intptr_t handle, dsSurroundVirtualizer_t virtualizer);
            static dsSetSurroundVirtualizer_t dsSetSurroundVirtualizerFunc = 0;
            if (dsSetSurroundVirtualizerFunc == 0) {
                dsSetSurroundVirtualizerFunc = (dsSetSurroundVirtualizer_t)resolve(RDK_DSHAL_NAME, "dsSetSurroundVirtualizer");
                if (dsSetSurroundVirtualizerFunc == 0) {
                    DSLOG_ERR("dsSetSurroundVirtualizer is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetSurroundVirtualizerFunc) {
                ret = dsSetSurroundVirtualizerFunc(dsHandle, dsSurVirtualizer);
            }
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, mode=%d, boost=%d", handle, surroundVirtualizer.mode, surroundVirtualizer.boost);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                std::string _PropertyMode  = getCurrentProfileProperty("SurroundVirtualizer.mode");
                std::string _PropertyBoost = getCurrentProfileProperty("SurroundVirtualizer.boost");
                device::HostPersistence::getInstance().persistHostProperty(_PropertyMode, std::to_string(surroundVirtualizer.mode));
                if ((surroundVirtualizer.mode >= 0) && (surroundVirtualizer.mode <= 2)) {
                    device::HostPersistence::getInstance().persistHostProperty(_PropertyBoost, std::to_string(surroundVirtualizer.boost));
                }
#endif
            } else {
                DSLOG_ERR("dsSetSurroundVirtualizer failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioSurroudVirtualizer");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioSurroudVirtualizer(const int32_t handle, WPEFramework::Exchange::IDeviceSettingsAudio::SurroundVirtualizer &surroundVirtualizer) override {
        ENTRY_LOG;
        try {
            dsSurroundVirtualizer_t virtualizer;
            // Use resolve function for dsGetSurroundVirtualizer
            typedef dsError_t (*dsGetSurroundVirtualizer_t)(intptr_t handle, dsSurroundVirtualizer_t* virtualizer);
            static dsGetSurroundVirtualizer_t dsGetSurroundVirtualizerFunc = 0;
            if (dsGetSurroundVirtualizerFunc == 0) {
                dsGetSurroundVirtualizerFunc = (dsGetSurroundVirtualizer_t)resolve(RDK_DSHAL_NAME, "dsGetSurroundVirtualizer");
                if (dsGetSurroundVirtualizerFunc == 0) {
                    DSLOG_ERR("dsGetSurroundVirtualizer is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetSurroundVirtualizerFunc) {
                dsResult = dsGetSurroundVirtualizerFunc(static_cast<intptr_t>(handle), &virtualizer);
            }
            if (dsResult == dsERR_NONE) {
                // Convert dsSurroundVirtualizer_t to SurroundVirtualizer enum
                surroundVirtualizer.mode = static_cast<uint8_t>(virtualizer.mode);
                surroundVirtualizer.boost = static_cast<uint8_t>(virtualizer.boost);
            } else {
                DSLOG_ERR("dsGetSurroundVirtualizer failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioSurroudVirtualizer");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioMISteering(const int32_t handle, const bool enable) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // Use resolve function for dsSetMISteering
            typedef dsError_t (*dsSetMISteering_t)(intptr_t handle, bool enable);
            static dsSetMISteering_t dsSetMISteeringFunc = 0;
            if (dsSetMISteeringFunc == 0) {
                dsSetMISteeringFunc = (dsSetMISteering_t)resolve(RDK_DSHAL_NAME, "dsSetMISteering");
                if (dsSetMISteeringFunc == 0) {
                    DSLOG_ERR("dsSetMISteering is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetMISteeringFunc) {
                ret = dsSetMISteeringFunc(dsHandle, enable);
            }
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, enable=%s", handle, enable ? "true" : "false");
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.MISteering", enable ? "Enabled" : "Disabled");
#endif
            } else {
                DSLOG_ERR("dsSetMISteering failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioMISteering");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioMISteering(const int32_t handle, bool &enable) override {
        ENTRY_LOG;
        try {
            bool miSteering = false;
            // Use resolve function for dsGetMISteering
            typedef dsError_t (*dsGetMISteering_t)(intptr_t handle, bool* steering);
            static dsGetMISteering_t dsGetMISteeringFunc = 0;
            if (dsGetMISteeringFunc == 0) {
                dsGetMISteeringFunc = (dsGetMISteering_t)resolve(RDK_DSHAL_NAME, "dsGetMISteering");
                if (dsGetMISteeringFunc == 0) {
                    DSLOG_ERR("dsGetMISteering is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetMISteeringFunc) {
                dsResult = dsGetMISteeringFunc(static_cast<intptr_t>(handle), &miSteering);
            }
            if (dsResult == dsERR_NONE) {
                enable = miSteering;
            } else {
                DSLOG_ERR("dsGetMISteering failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioMISteering");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioGraphicEqualizerMode(const int32_t handle, const int32_t mode) override {
        ENTRY_LOG;
        try {
            // Use resolve function for dsSetGraphicEqualizerMode
            typedef dsError_t (*dsSetGraphicEqualizerMode_t)(intptr_t handle, int mode);
            static dsSetGraphicEqualizerMode_t dsSetGraphicEqualizerModeFunc = 0;
            if (dsSetGraphicEqualizerModeFunc == 0) {
                dsSetGraphicEqualizerModeFunc = (dsSetGraphicEqualizerMode_t)resolve(RDK_DSHAL_NAME, "dsSetGraphicEqualizerMode");
                if (dsSetGraphicEqualizerModeFunc == 0) {
                    DSLOG_ERR("dsSetGraphicEqualizerMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsSetGraphicEqualizerModeFunc) {
                dsResult = dsSetGraphicEqualizerModeFunc(static_cast<intptr_t>(handle), mode);
            }
            if (dsResult == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, mode=%d", handle, mode);
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.GraphicEQ", std::to_string(mode));
#endif
            } else {
                DSLOG_ERR("dsSetGraphicEqualizerMode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioGraphicEqualizerMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioGraphicEqualizerMode(const int32_t handle, int32_t &mode) override {
        ENTRY_LOG;
        try {
            int eqMode = 0;
            // Use resolve function for dsGetGraphicEqualizerMode
            typedef dsError_t (*dsGetGraphicEqualizerMode_t)(intptr_t handle, int* mode);
            static dsGetGraphicEqualizerMode_t dsGetGraphicEqualizerModeFunc = 0;
            if (dsGetGraphicEqualizerModeFunc == 0) {
                dsGetGraphicEqualizerModeFunc = (dsGetGraphicEqualizerMode_t)resolve(RDK_DSHAL_NAME, "dsGetGraphicEqualizerMode");
                if (dsGetGraphicEqualizerModeFunc == 0) {
                    DSLOG_ERR("dsGetGraphicEqualizerMode is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t dsResult = dsERR_GENERAL;
            if (0 != dsGetGraphicEqualizerModeFunc) {
                dsResult = dsGetGraphicEqualizerModeFunc(static_cast<intptr_t>(handle), &eqMode);
            }
            if (dsResult == dsERR_NONE) {
                mode = eqMode;
                DSLOG_INFO("success: handle=%d, mode=%d", handle, mode);
            } else {
                DSLOG_ERR("dsGetGraphicEqualizerMode failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioGraphicEqualizerMode");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioMS12ProfileList(const int32_t handle, WPEFramework::Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) const override {
        ENTRY_LOG;
        ms12ProfileList = nullptr;
        try {
            // dsAudio.c: _dsGetMS12AudioProfileList resolves and calls dsGetMS12AudioProfileList
            typedef dsError_t (*dsGetMS12AudioProfileList_t)(intptr_t handle, dsMS12AudioProfileList_t* profiles);
            static dsGetMS12AudioProfileList_t dsGetMS12AudioProfileListFunc = 0;
            if (dsGetMS12AudioProfileListFunc == 0) {
                dsGetMS12AudioProfileListFunc = (dsGetMS12AudioProfileList_t)resolve(RDK_DSHAL_NAME, "dsGetMS12AudioProfileList");
                if (dsGetMS12AudioProfileListFunc == 0) {
                    DSLOG_ERR(" dsGetMS12AudioProfileList is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

            dsMS12AudioProfileList_t pList;
            memset(&pList, 0, sizeof(pList));
            dsError_t dsResult = dsGetMS12AudioProfileListFunc(static_cast<intptr_t>(handle), &pList);
            if (dsResult != dsERR_NONE) {
                DSLOG_ERR(" dsGetMS12AudioProfileList failed, error=%d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }

            DSLOG_INFO(" handle=%d, count=%d, profiles=%s",
                    handle, pList.audioProfileCount, pList.audioProfileList);

            // Parse the comma-separated audioProfileList string into MS12AudioProfile structs
            // (matches dsAudio.c pattern: audioProfileList is comma-separated, audioProfileCount is count)
            std::vector<WPEFramework::Exchange::IDeviceSettingsAudio::MS12AudioProfile> profileVec;
            char profileBuffer[MAX_PROFILE_LIST_BUFFER_LEN];
            strncpy(profileBuffer, pList.audioProfileList, MAX_PROFILE_LIST_BUFFER_LEN - 1);
            profileBuffer[MAX_PROFILE_LIST_BUFFER_LEN - 1] = '\0';

            char* token = strtok(profileBuffer, ",");
            while (token != nullptr) {
                // Skip leading/trailing whitespace
                while (*token == ' ') token++;
                if (*token != '\0') {
                    WPEFramework::Exchange::IDeviceSettingsAudio::MS12AudioProfile profile;
                    profile.audioProfile = std::string(token);
                    profileVec.push_back(profile);
                }
                token = strtok(nullptr, ",");
            }

            DSLOG_INFO(" parsed %zu profiles", profileVec.size());

            // Create the COM-RPC iterator
            using MS12ProfileIterator = WPEFramework::RPC::IteratorType<IDeviceSettingsAudioMS12AudioProfileIterator>;
            ms12ProfileList = WPEFramework::Core::Service<MS12ProfileIterator>::Create<IDeviceSettingsAudioMS12AudioProfileIterator>(profileVec);

        } catch (...) {
            DSLOG_ERR("Exception in GetAudioMS12ProfileList");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioMS12Profile(const int32_t handle, string &profile) override {
        ENTRY_LOG;
        try {
            char profileStr[256] = {0};
            dsError_t dsResult = dsGetMS12AudioProfile(static_cast<intptr_t>(handle), profileStr);
            if (dsResult == dsERR_NONE) {
                profile = std::string(profileStr);
            } else {
                DSLOG_ERR("dsGetMS12AudioProfile failed with error: %d", dsResult);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioMS12Profile");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioMS12Profile(const int32_t handle, const string& profile) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            dsError_t ret = dsSetMS12AudioProfile(dsHandle, profile.c_str());
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, profile=%s", handle, profile.c_str());
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                device::HostPersistence::getInstance().persistHostProperty("audio.MS12Profile", profile);
#endif
                if (profile != "Off") {
                    applyMS12ProfileSettingsOverride(dsHandle);
                }
            } else {
                DSLOG_ERR("dsSetMS12AudioProfile failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioMS12Profile");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioMixerLevels(const int32_t handle, const WPEFramework::Exchange::IDeviceSettingsAudio::AudioInput audioInput, const int32_t volume) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            dsAudioInput_t dsInput = static_cast<dsAudioInput_t>(audioInput);
            
            typedef dsError_t (*dsSetMixerLevel_t)(intptr_t handle, dsAudioInput_t input, int32_t level);
            static dsSetMixerLevel_t dsSetMixerLevelFunc = 0;
            if (dsSetMixerLevelFunc == 0) {
                dsSetMixerLevelFunc = (dsSetMixerLevel_t)resolve(RDK_DSHAL_NAME, "dsSetMixerLevel");
                if(dsSetMixerLevelFunc == 0) {
                    DSLOG_ERR("dsSetMixerLevel is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }
            
            dsError_t ret = dsERR_GENERAL;
            if (0 != dsSetMixerLevelFunc) {
                ret = dsSetMixerLevelFunc(dsHandle, dsInput, volume);
            }
            
            if (ret == dsERR_NONE) {
                DSLOG_INFO("success: handle=%d, input=%d, volume=%d", handle, static_cast<int>(audioInput), volume);
            } else {
                DSLOG_ERR("dsSetMixerLevel failed with error: %d", ret);
                return WPEFramework::Core::ERROR_GENERAL;
            }
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioMixerLevels");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetAudioMS12SettingsOverride(const int32_t handle, const string profileName, 
                                         const string profileSettingsName, const string profileSettingValue, 
                                         const string profileState) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        // dsAudio.c: _dsSetMS12SetttingsOverride is pure in-process logic — no single HAL function.
        // It orchestrates dsSetDialogEnhancement/dsSetBassEnhancer/dsSetVolumeLeveller/dsSetSurroundVirtualizer.
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            std::string _AProfile("Off");
            try { _AProfile = device::HostPersistence::getInstance().getProperty("audio.MS12Profile"); }
            catch(...) { try { _AProfile = device::HostPersistence::getInstance().getDefaultProperty("audio.MS12Profile"); } catch(...) { _AProfile = "Off"; } }

            if (profileName == _AProfile) {
                // Active profile — apply the setting immediately via HAL
                if (profileSettingsName == "DialogEnhance") {
                    typedef dsError_t (*dsSetDialogEnhancement_t)(intptr_t h, int level);
                    dsSetDialogEnhancement_t fn = (dsSetDialogEnhancement_t)resolve(RDK_DSHAL_NAME, "dsSetDialogEnhancement");
                    if (fn) {
                        if (profileState == "ADD") {
                            int val = atoi(profileSettingValue.c_str());
                            if (fn(dsHandle, val) == dsERR_NONE)
                                device::HostPersistence::getInstance().persistHostProperty(getCurrentProfileProperty("EnhancerLevel"), profileSettingValue);
                        } else if (profileState == "REMOVE") {
                            std::string _p = getCurrentProfileProperty("EnhancerLevel");
                            std::string _def("0"); try { _def = device::HostPersistence::getInstance().getDefaultProperty(_p); } catch(...) {}
                            if (fn(dsHandle, atoi(_def.c_str())) == dsERR_NONE)
                                device::HostPersistence::getInstance().persistHostProperty(_p, _def);
                        }
                    }
                } else if (profileSettingsName == "VolumeLevellerMode") {
                    int m = atoi(profileSettingValue.c_str());
                    if (m == 0 || m == 1) device::HostPersistence::getInstance().persistHostProperty(getCurrentProfileProperty("VolumeLeveller.mode"), profileSettingValue);
                } else if (profileSettingsName == "VolumeLevellerLevel") {
                    typedef dsError_t (*dsSetVolumeLeveller_t)(intptr_t h, dsVolumeLeveller_t vl);
                    dsSetVolumeLeveller_t fn = (dsSetVolumeLeveller_t)resolve(RDK_DSHAL_NAME, "dsSetVolumeLeveller");
                    if (fn) {
                        if (profileState == "ADD") {
                            std::string _pMode = getCurrentProfileProperty("VolumeLeveller.mode");
                            dsVolumeLeveller_t vl;
                            try { vl.mode = atoi(device::HostPersistence::getInstance().getProperty(_pMode).c_str()); } catch(...) { vl.mode = 0; }
                            vl.level = atoi(profileSettingValue.c_str());
                            if (fn(dsHandle, vl) == dsERR_NONE)
                                device::HostPersistence::getInstance().persistHostProperty(getCurrentProfileProperty("VolumeLeveller.level"), profileSettingValue);
                        } else if (profileState == "REMOVE") {
                            std::string _pm = getCurrentProfileProperty("VolumeLeveller.mode"), _pl = getCurrentProfileProperty("VolumeLeveller.level");
                            std::string _dm("0"), _dl("0"); try { _dm = device::HostPersistence::getInstance().getDefaultProperty(_pm); } catch(...) {} try { _dl = device::HostPersistence::getInstance().getDefaultProperty(_pl); } catch(...) {}
                            dsVolumeLeveller_t vl; vl.mode = atoi(_dm.c_str()); vl.level = atoi(_dl.c_str());
                            if (fn(dsHandle, vl) == dsERR_NONE) { device::HostPersistence::getInstance().persistHostProperty(_pm, _dm); device::HostPersistence::getInstance().persistHostProperty(_pl, _dl); }
                        }
                    }
                } else if (profileSettingsName == "BassEnhancer") {
                    typedef dsError_t (*dsSetBassEnhancer_t)(intptr_t h, int boost);
                    dsSetBassEnhancer_t fn = (dsSetBassEnhancer_t)resolve(RDK_DSHAL_NAME, "dsSetBassEnhancer");
                    if (fn) {
                        if (profileState == "ADD") {
                            if (fn(dsHandle, atoi(profileSettingValue.c_str())) == dsERR_NONE)
                                device::HostPersistence::getInstance().persistHostProperty("audio.BassBoost", profileSettingValue);
                        } else if (profileState == "REMOVE") {
                            std::string _p = getCurrentProfileProperty("BassBoost");
                            std::string _def("0"); try { _def = device::HostPersistence::getInstance().getDefaultProperty(_p); } catch(...) {}
                            if (fn(dsHandle, atoi(_def.c_str())) == dsERR_NONE)
                                device::HostPersistence::getInstance().persistHostProperty("audio.BassBoost", _def);
                        }
                    }
                } else if (profileSettingsName == "SurroundVirtualizerMode") {
                    int m = atoi(profileSettingValue.c_str());
                    if (m >= 0 && m <= 2) device::HostPersistence::getInstance().persistHostProperty(getCurrentProfileProperty("SurroundVirtualizer.mode"), profileSettingValue);
                } else if (profileSettingsName == "SurroundVirtualizerLevel") {
                    typedef dsError_t (*dsSetSurroundVirtualizer_t)(intptr_t h, dsSurroundVirtualizer_t virt);
                    dsSetSurroundVirtualizer_t fn = (dsSetSurroundVirtualizer_t)resolve(RDK_DSHAL_NAME, "dsSetSurroundVirtualizer");
                    if (fn) {
                        if (profileState == "ADD") {
                            std::string _pMode = getCurrentProfileProperty("SurroundVirtualizer.mode");
                            dsSurroundVirtualizer_t virt;
                            try { virt.mode = atoi(device::HostPersistence::getInstance().getProperty(_pMode).c_str()); } catch(...) { virt.mode = 0; }
                            virt.boost = atoi(profileSettingValue.c_str());
                            if (fn(dsHandle, virt) == dsERR_NONE)
                                device::HostPersistence::getInstance().persistHostProperty(getCurrentProfileProperty("SurroundVirtualizer.boost"), profileSettingValue);
                        } else if (profileState == "REMOVE") {
                            std::string _pm = getCurrentProfileProperty("SurroundVirtualizer.mode"), _pb = getCurrentProfileProperty("SurroundVirtualizer.boost");
                            std::string _dm("0"), _db("0"); try { _dm = device::HostPersistence::getInstance().getDefaultProperty(_pm); } catch(...) {} try { _db = device::HostPersistence::getInstance().getDefaultProperty(_pb); } catch(...) {}
                            dsSurroundVirtualizer_t virt; virt.mode = atoi(_dm.c_str()); virt.boost = atoi(_db.c_str());
                            if (fn(dsHandle, virt) == dsERR_NONE) { device::HostPersistence::getInstance().persistHostProperty(_pm, _dm); device::HostPersistence::getInstance().persistHostProperty(_pb, _db); }
                        }
                    }
                } else {
                    DSLOG_WARN(" Unknown setting name: %s", profileSettingsName.c_str());
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            } else {
                // Non-active profile — just persist the value for future use
                std::string hostProperty;
                if      (profileSettingsName == "DialogEnhance")          hostProperty = generateProfileProperty(profileName, "EnhancerLevel");
                else if (profileSettingsName == "VolumeLevellerMode")      hostProperty = generateProfileProperty(profileName, "VolumeLeveller.mode");
                else if (profileSettingsName == "VolumeLevellerLevel")     hostProperty = generateProfileProperty(profileName, "VolumeLeveller.level");
                else if (profileSettingsName == "BassEnhancer")            hostProperty = "audio.BassBoost";
                else if (profileSettingsName == "SurroundVirtualizerMode") hostProperty = generateProfileProperty(profileName, "SurroundVirtualizer.mode");
                else if (profileSettingsName == "SurroundVirtualizerLevel")hostProperty = generateProfileProperty(profileName, "SurroundVirtualizer.boost");
                else { DSLOG_WARN(" Unknown setting name: %s", profileSettingsName.c_str()); return WPEFramework::Core::ERROR_GENERAL; }

                if (profileState == "ADD") {
                    device::HostPersistence::getInstance().persistHostProperty(hostProperty, profileSettingValue);
                } else if (profileState == "REMOVE") {
                    std::string _def("0"); try { _def = device::HostPersistence::getInstance().getDefaultProperty(hostProperty); } catch(...) {}
                    device::HostPersistence::getInstance().persistHostProperty(hostProperty, _def);
                }
            }
            DSLOG_INFO("success: handle=%d, profile=%s, setting=%s, state=%s",
                    handle, profileName.c_str(), profileSettingsName.c_str(), profileState.c_str());
        } catch (...) {
            DSLOG_ERR("Exception in SetAudioMS12SettingsOverride");
            return WPEFramework::Core::ERROR_GENERAL;
        }
#else
        DSLOG_INFO(" DS_AUDIO_SETTINGS_PERSISTENCE not enabled");
#endif
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t ResetAudioDialogEnhancement(const int32_t handle) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // dsAudio.c: _resetDialogEnhancerLevel reads default, calls dsSetDialogEnhancement, persists
            typedef dsError_t (*dsSetDialogEnhancement_t)(intptr_t handle, int enhancerLevel);
            static dsSetDialogEnhancement_t dsSetDialogEnhancementFunc = 0;
            if (dsSetDialogEnhancementFunc == 0) {
                dsSetDialogEnhancementFunc = (dsSetDialogEnhancement_t)resolve(RDK_DSHAL_NAME, "dsSetDialogEnhancement");
                if (dsSetDialogEnhancementFunc == 0) {
                    DSLOG_ERR("dsSetDialogEnhancement is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
            std::string _Property = getCurrentProfileProperty("EnhancerLevel");
            std::string _EnhancerLevel("0");
            try { _EnhancerLevel = device::HostPersistence::getInstance().getDefaultProperty(_Property); } catch(...) { _EnhancerLevel = "0"; }
            int m_enhancerLevel = atoi(_EnhancerLevel.c_str());
            if (dsSetDialogEnhancementFunc(dsHandle, m_enhancerLevel) == dsERR_NONE) {
                DSLOG_INFO(" handle=%d, default level=%d", handle, m_enhancerLevel);
                device::HostPersistence::getInstance().persistHostProperty(_Property, _EnhancerLevel);
            } else {
                DSLOG_ERR("dsSetDialogEnhancement failed");
                return WPEFramework::Core::ERROR_GENERAL;
            }
#else
            if (dsSetDialogEnhancementFunc(dsHandle, 0) != dsERR_NONE) {
                DSLOG_ERR("failed");
                return WPEFramework::Core::ERROR_GENERAL;
            }
            DSLOG_INFO("success: handle=%d", handle);
#endif
        } catch (...) {
            DSLOG_ERR("Exception in ResetAudioDialogEnhancement");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t ResetAudioBassEnhancer(const int32_t handle) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // dsAudio.c: _resetBassEnhancer reads default, calls dsSetBassEnhancer, persists
            typedef dsError_t (*dsSetBassEnhancer_t)(intptr_t handle, int boost);
            static dsSetBassEnhancer_t dsSetBassEnhancerFunc = 0;
            if (dsSetBassEnhancerFunc == 0) {
                dsSetBassEnhancerFunc = (dsSetBassEnhancer_t)resolve(RDK_DSHAL_NAME, "dsSetBassEnhancer");
                if (dsSetBassEnhancerFunc == 0) {
                    DSLOG_ERR("dsSetBassEnhancer is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
            std::string _Property = getCurrentProfileProperty("BassBoost");
            std::string _BassBoost("0");
            try { _BassBoost = device::HostPersistence::getInstance().getDefaultProperty(_Property); } catch(...) { _BassBoost = "0"; }
            int m_bassBoost = atoi(_BassBoost.c_str());
            if (dsSetBassEnhancerFunc(dsHandle, m_bassBoost) == dsERR_NONE) {
                DSLOG_INFO(" handle=%d, default boost=%d", handle, m_bassBoost);
                device::HostPersistence::getInstance().persistHostProperty("audio.BassBoost", _BassBoost);
            } else {
                DSLOG_ERR("dsSetBassEnhancer failed");
                return WPEFramework::Core::ERROR_GENERAL;
            }
#else
            if (dsSetBassEnhancerFunc(dsHandle, 0) != dsERR_NONE) {
                DSLOG_ERR("failed");
                return WPEFramework::Core::ERROR_GENERAL;
            }
            DSLOG_INFO("success: handle=%d", handle);
#endif
        } catch (...) {
            DSLOG_ERR("Exception in ResetAudioBassEnhancer");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t ResetAudioSurroundVirtualizer(const int32_t handle) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // dsAudio.c: _resetSurroundVirtualizer reads defaults for mode+boost, calls dsSetSurroundVirtualizer, persists
            typedef dsError_t (*dsSetSurroundVirtualizer_t)(intptr_t handle, dsSurroundVirtualizer_t virtualizer);
            static dsSetSurroundVirtualizer_t dsSetSurroundVirtualizerFunc = 0;
            if (dsSetSurroundVirtualizerFunc == 0) {
                dsSetSurroundVirtualizerFunc = (dsSetSurroundVirtualizer_t)resolve(RDK_DSHAL_NAME, "dsSetSurroundVirtualizer");
                if (dsSetSurroundVirtualizerFunc == 0) {
                    DSLOG_ERR("dsSetSurroundVirtualizer is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
            std::string _PropertyMode = getCurrentProfileProperty("SurroundVirtualizer.mode");
            std::string _PropertyBoost = getCurrentProfileProperty("SurroundVirtualizer.boost");
            std::string _SVMode("0"), _SVBoost("0");
            try { _SVMode  = device::HostPersistence::getInstance().getDefaultProperty(_PropertyMode);  } catch(...) { _SVMode  = "0"; }
            try { _SVBoost = device::HostPersistence::getInstance().getDefaultProperty(_PropertyBoost); } catch(...) { _SVBoost = "0"; }
            dsSurroundVirtualizer_t m_virtualizer;
            m_virtualizer.mode  = atoi(_SVMode.c_str());
            m_virtualizer.boost = atoi(_SVBoost.c_str());
            if (dsSetSurroundVirtualizerFunc(dsHandle, m_virtualizer) == dsERR_NONE) {
                DSLOG_INFO(" handle=%d, mode=%d boost=%d", handle, m_virtualizer.mode, m_virtualizer.boost);
                device::HostPersistence::getInstance().persistHostProperty(_PropertyMode,  _SVMode);
                device::HostPersistence::getInstance().persistHostProperty(_PropertyBoost, _SVBoost);
            } else {
                DSLOG_ERR("dsSetSurroundVirtualizer failed");
                return WPEFramework::Core::ERROR_GENERAL;
            }
#else
            dsSurroundVirtualizer_t m_virt = {0, 0};
            if (dsSetSurroundVirtualizerFunc(dsHandle, m_virt) != dsERR_NONE) {
                DSLOG_ERR("failed");
                return WPEFramework::Core::ERROR_GENERAL;
            }
            DSLOG_INFO("success: handle=%d", handle);
#endif
        } catch (...) {
            DSLOG_ERR("Exception in ResetAudioSurroundVirtualizer");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t ResetAudioVolumeLeveller(const int32_t handle) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            // dsAudio.c: _resetVolumeLeveller reads defaults for mode+level, calls dsSetVolumeLeveller, persists
            typedef dsError_t (*dsSetVolumeLeveller_t)(intptr_t handle, dsVolumeLeveller_t volLeveller);
            static dsSetVolumeLeveller_t dsSetVolumeLevellerFunc = 0;
            if (dsSetVolumeLevellerFunc == 0) {
                dsSetVolumeLevellerFunc = (dsSetVolumeLeveller_t)resolve(RDK_DSHAL_NAME, "dsSetVolumeLeveller");
                if (dsSetVolumeLevellerFunc == 0) {
                    DSLOG_ERR("dsSetVolumeLeveller is not defined");
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            }

#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
            std::string _PropertyMode  = getCurrentProfileProperty("VolumeLeveller.mode");
            std::string _PropertyLevel = getCurrentProfileProperty("VolumeLeveller.level");
            std::string _volLevellerMode("0"), _volLevellerLevel("0");
            try { _volLevellerMode  = device::HostPersistence::getInstance().getDefaultProperty(_PropertyMode);  } catch(...) { _volLevellerMode  = "0"; }
            try { _volLevellerLevel = device::HostPersistence::getInstance().getDefaultProperty(_PropertyLevel); } catch(...) { _volLevellerLevel = "0"; }
            dsVolumeLeveller_t m_vl;
            m_vl.mode  = atoi(_volLevellerMode.c_str());
            m_vl.level = atoi(_volLevellerLevel.c_str());
            if (dsSetVolumeLevellerFunc(dsHandle, m_vl) == dsERR_NONE) {
                DSLOG_INFO(" handle=%d, mode=%d level=%d", handle, m_vl.mode, m_vl.level);
                device::HostPersistence::getInstance().persistHostProperty(_PropertyMode,  _volLevellerMode);
                device::HostPersistence::getInstance().persistHostProperty(_PropertyLevel, _volLevellerLevel);
            } else {
                DSLOG_ERR("dsSetVolumeLeveller failed");
                return WPEFramework::Core::ERROR_GENERAL;
            }
#else
            dsVolumeLeveller_t m_vl = {0, 0};
            if (dsSetVolumeLevellerFunc(dsHandle, m_vl) != dsERR_NONE) {
                DSLOG_ERR("failed");
                return WPEFramework::Core::ERROR_GENERAL;
            }
            DSLOG_INFO("success: handle=%d", handle);
#endif
        } catch (...) {
            DSLOG_ERR("Exception in ResetAudioVolumeLeveller");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetAudioHDMIARCPortId(const int32_t handle, int32_t &portId) override {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            // Get HDMI ARC Port ID from device persistence (reference from dsAudio.c)
            std::string hdmiARCPortId("0"); // Default value
            try {
                hdmiARCPortId = device::HostPersistence::getInstance().getDefaultProperty("HDMIARC.port.Id");
            } catch (...) {
                DSLOG_WARN("Failed to get HDMIARC.port.Id from persistence, using default value -1");
                hdmiARCPortId = "-1";
            }
            
            portId = atoi(hdmiARCPortId.c_str());
            DSLOG_INFO("success: handle=%d, portId=%d", handle, portId);
        } catch (...) {
            DSLOG_ERR("Exception in GetAudioHDMIARCPortId");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetStereoAuto(const int32_t handle, int32_t &autoMode) override
    {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            const dsAudioPortType_t portType = getAudioPortType(static_cast<intptr_t>(handle));
            /* Mirrors _dsGetStereoAuto: returns current runtime auto-state only, no persistence path. */
            autoMode = isStereoAutoEnabled(portType) ? 1 : 0;
            DSLOG_INFO("success: handle=%d, autoMode=%d", handle, autoMode);
        } catch (...) {
            DSLOG_ERR("Exception in GetStereoAuto");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetStereoAuto(const int32_t handle, const int32_t autoMode, const bool persist) override
    {
        ENTRY_LOG;
        if (!_isInitialized) {
            DSLOG_ERR("Audio platform not initialized");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        try {
            intptr_t dsHandle = static_cast<intptr_t>(handle);
            
            // Handle persistence similar to dsAudio.c _dsSetStereoAuto implementation
            if (persist) {
                dsAudioPortType_t portType = getAudioPortType(dsHandle);
                switch (portType) {
                    case dsAUDIOPORT_TYPE_HDMI:
                        device::HostPersistence::getInstance().persistHostProperty("HDMI0.AudioMode.AUTO", autoMode ? "TRUE" : "FALSE");
                        DSLOG_INFO("Persisted HDMI stereo auto mode: autoMode=%d", autoMode);
                        break;

                    case dsAUDIOPORT_TYPE_HDMI_ARC:
                        device::HostPersistence::getInstance().persistHostProperty("HDMI_ARC0.AudioMode.AUTO", autoMode ? "TRUE" : "FALSE");
                        DSLOG_INFO("Persisted HDMI_ARC stereo auto mode: autoMode=%d", autoMode);
                        break;

                    case dsAUDIOPORT_TYPE_SPDIF:
                        device::HostPersistence::getInstance().persistHostProperty("SPDIF0.AudioMode.AUTO", autoMode ? "TRUE" : "FALSE");
                        DSLOG_INFO("Persisted SPDIF stereo auto mode: autoMode=%d", autoMode);
                        break;

                    case dsAUDIOPORT_TYPE_SPEAKER:
                        device::HostPersistence::getInstance().persistHostProperty("SPEAKER0.AudioMode.AUTO", autoMode ? "TRUE" : "FALSE");
                        DSLOG_INFO("Persisted SPEAKER stereo auto mode: autoMode=%d", autoMode);
                        break;

                    default:
                        DSLOG_WARN("persistence not supported for port type: %d", portType);
                        break;
                }
            }
            
            // Call the HAL function - only for HDMI_ARC and SPDIF ports as per dsAudio.c logic
            dsAudioPortType_t portType = getAudioPortType(dsHandle);
            if ((portType == dsAUDIOPORT_TYPE_HDMI_ARC) || (portType == dsAUDIOPORT_TYPE_SPDIF)) {
                // Use resolve function for dsSetStereoAuto
                typedef dsError_t (*dsSetStereoAuto_t)(intptr_t handle, int autoMode);
                static dsSetStereoAuto_t dsSetStereoAutoFunc = 0;
                if (dsSetStereoAutoFunc == 0) {
                    dsSetStereoAutoFunc = (dsSetStereoAuto_t)resolve(RDK_DSHAL_NAME, "dsSetStereoAuto");
                    if (dsSetStereoAutoFunc == 0) {
                        DSLOG_ERR("dsSetStereoAuto is not defined");
                        return WPEFramework::Core::ERROR_GENERAL;
                    }
                }
                
                dsError_t ret = dsERR_GENERAL;
                if (0 != dsSetStereoAutoFunc) {
                    ret = dsSetStereoAutoFunc(dsHandle, autoMode);
                }
                if (ret == dsERR_NONE) {
                    DSLOG_INFO("success: handle=%d, autoMode=%d, persist=%s",
                           handle, autoMode, persist ? "true" : "false");
                } else {
                    DSLOG_ERR("dsSetStereoAuto failed with error: %d", ret);
                    return WPEFramework::Core::ERROR_GENERAL;
                }
            } else {
                DSLOG_INFO("HAL call skipped for port type %d (only HDMI_ARC/SPDIF supported): handle=%d, autoMode=%d",
                       portType, handle, autoMode);
            }

            setStereoAutoState(portType, autoMode != 0);
        } catch (...) {
            DSLOG_ERR("Exception in SetStereoAuto");
            return WPEFramework::Core::ERROR_GENERAL;
        }
        EXIT_LOG;
        return WPEFramework::Core::ERROR_NONE;
    }

private:
    void applyMS12ProfileSettingsOverride(const intptr_t handle)
    {
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
        const std::string enhancerProperty = getCurrentProfileProperty("EnhancerLevel");
        const std::string bassProperty = getCurrentProfileProperty("BassBoost");
        const std::string volumeModeProperty = getCurrentProfileProperty("VolumeLeveller.mode");
        const std::string volumeLevelProperty = getCurrentProfileProperty("VolumeLeveller.level");
        const std::string virtualizerModeProperty = getCurrentProfileProperty("SurroundVirtualizer.mode");
        const std::string virtualizerBoostProperty = getCurrentProfileProperty("SurroundVirtualizer.boost");

        try {
            typedef dsError_t (*dsSetDialogEnhancement_t)(intptr_t, int);
            const dsSetDialogEnhancement_t setDialogEnhancement =
                reinterpret_cast<dsSetDialogEnhancement_t>(resolve(RDK_DSHAL_NAME, "dsSetDialogEnhancement"));
            if (setDialogEnhancement != nullptr) {
                const int value = atoi(device::HostPersistence::getInstance().getProperty(enhancerProperty).c_str());
                if (setDialogEnhancement(handle, value) == dsERR_NONE) {
                    device::HostPersistence::getInstance().persistHostProperty(enhancerProperty, std::to_string(value));
                }
            }
        } catch (...) {
            DSLOG_INFO("No persisted dialog enhancement value for active MS12 profile");
        }

        try {
            typedef dsError_t (*dsSetBassEnhancer_t)(intptr_t, int);
            const dsSetBassEnhancer_t setBassEnhancer =
                reinterpret_cast<dsSetBassEnhancer_t>(resolve(RDK_DSHAL_NAME, "dsSetBassEnhancer"));
            if (setBassEnhancer != nullptr) {
                const int value = atoi(device::HostPersistence::getInstance().getProperty("audio.BassBoost").c_str());
                if (setBassEnhancer(handle, value) == dsERR_NONE) {
                    device::HostPersistence::getInstance().persistHostProperty("audio.BassBoost", std::to_string(value));
                }
            }
        } catch (...) {
            DSLOG_INFO("No persisted bass boost value for active MS12 profile");
        }

        try {
            typedef dsError_t (*dsSetVolumeLeveller_t)(intptr_t, dsVolumeLeveller_t);
            const dsSetVolumeLeveller_t setVolumeLeveller =
                reinterpret_cast<dsSetVolumeLeveller_t>(resolve(RDK_DSHAL_NAME, "dsSetVolumeLeveller"));
            if (setVolumeLeveller != nullptr) {
                dsVolumeLeveller_t value;
                value.mode = atoi(device::HostPersistence::getInstance().getProperty(volumeModeProperty).c_str());
                value.level = atoi(device::HostPersistence::getInstance().getProperty(volumeLevelProperty).c_str());
                if (setVolumeLeveller(handle, value) == dsERR_NONE) {
                    device::HostPersistence::getInstance().persistHostProperty(volumeModeProperty, std::to_string(value.mode));
                    device::HostPersistence::getInstance().persistHostProperty(volumeLevelProperty, std::to_string(value.level));
                }
            }
        } catch (...) {
            DSLOG_INFO("No persisted volume leveller value for active MS12 profile");
        }

        try {
            typedef dsError_t (*dsSetSurroundVirtualizer_t)(intptr_t, dsSurroundVirtualizer_t);
            const dsSetSurroundVirtualizer_t setSurroundVirtualizer =
                reinterpret_cast<dsSetSurroundVirtualizer_t>(resolve(RDK_DSHAL_NAME, "dsSetSurroundVirtualizer"));
            if (setSurroundVirtualizer != nullptr) {
                dsSurroundVirtualizer_t value;
                value.mode = atoi(device::HostPersistence::getInstance().getProperty(virtualizerModeProperty).c_str());
                value.boost = atoi(device::HostPersistence::getInstance().getProperty(virtualizerBoostProperty).c_str());
                if (setSurroundVirtualizer(handle, value) == dsERR_NONE) {
                    device::HostPersistence::getInstance().persistHostProperty(virtualizerModeProperty, std::to_string(value.mode));
                    device::HostPersistence::getInstance().persistHostProperty(virtualizerBoostProperty, std::to_string(value.boost));
                }
            }
        } catch (...) {
            DSLOG_INFO("No persisted surround virtualizer value for active MS12 profile");
        }
#else
        (void)handle;
#endif
    }

    // Implementation of audio settings initialization from dsAudioMgr_init
    void initializeAudioSettings()
    {
        ENTRY_LOG;
        try {
            // Initialize audio configuration settings from persistence
            // This is adapted from dsAudioMgr_init logic in dsAudio.c
            
            DSLOG_INFO("Initializing comprehensive audio settings from persistence and platform defaults...");
            
            // Initialize audio port settings for all supported audio port types
            initializeAudioPortSettings();
            
            // Initialize MS12 audio processing features if supported
            initializeMS12Settings();
            
            DSLOG_INFO("Audio platform and settings initialization completed successfully");
                    
        } catch (...) {
            DSLOG_ERR("Exception in initializing audio settings");
        }
        EXIT_LOG;
    }
    
    // Audio configuration initialization from AudioConfigInit function
    void audioConfigInit()
    {
        ENTRY_LOG;
        try {
            DSLOG_INFO("Starting comprehensive audio configuration initialization...");
            
            intptr_t handle = 0;
            
            // 1. Initialize LE (Loudness Equivalence) Configuration
            typedef dsError_t (*dsEnableLEConfig_t)(intptr_t handle, const bool enable);
            dsEnableLEConfig_t dsEnableLEConfigFunc = nullptr;
            
            if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                dsEnableLEConfigFunc = (dsEnableLEConfig_t) resolve(RDK_DSHAL_NAME, "dsEnableLEConfig");
                if (dsEnableLEConfigFunc) {
                    DSLOG_INFO("dsEnableLEConfig(int, bool) is defined and loaded");
                    std::string leEnable("FALSE");
                    try {
                        leEnable = device::HostPersistence::getInstance().getProperty("audio.LEEnable");
                    } catch(...) {
                        #ifndef DS_LE_DEFAULT_DISABLED
                        leEnable = "TRUE";
                        #endif
                        DSLOG_INFO("LE : Persisting default LE status: %s", leEnable.c_str());
                        device::HostPersistence::getInstance().persistHostProperty("audio.LEEnable", leEnable);
                    }
                    
                    bool leEnabled = (leEnable == "TRUE");
                    dsEnableLEConfigFunc(handle, leEnabled);
                    m_LEEnabled = leEnabled;  // sync static state with what was applied to HAL
                    DSLOG_INFO("LE (Loudness Equivalence) initialized: %s", leEnabled ? "enabled" : "disabled");
                } else {
                    DSLOG_INFO("dsEnableLEConfig(int, bool) is not available in HAL");
                }
            } else {
                DSLOG_ERR("dsEnableLEConfig failed - HDMI port 0 not available");
            }
            
            #ifdef DS_AUDIO_SETTINGS_PERSISTENCE
            // 2. Initialize Audio Gain for SPEAKER and HDMI ports
            typedef dsError_t (*dsSetAudioGain_t)(intptr_t handle, float gain);
            dsSetAudioGain_t dsSetAudioGainFunc = nullptr;
            
            dsSetAudioGainFunc = (dsSetAudioGain_t) resolve(RDK_DSHAL_NAME, "dsSetAudioGain");
            if (dsSetAudioGainFunc) {
                DSLOG_INFO("dsSetAudioGain_t(int, float) is defined and loaded");
                std::string audioGain("0");
                float audioGainValue = 0;
                
                // SPEAKER init
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    try {
                        audioGain = device::HostPersistence::getInstance().getProperty("SPEAKER0.audio.Gain");
                    } catch(...) {
                        try {
                            DSLOG_INFO("SPEAKER0.audio.Gain not found in persistence store. Try system default");
                            audioGain = device::HostPersistence::getInstance().getDefaultProperty("SPEAKER0.audio.Gain");
                        } catch(...) {
                            audioGain = "0";
                        }
                    }
                    audioGainValue = atof(audioGain.c_str());
                    if (dsSetAudioGainFunc(handle, audioGainValue) == dsERR_NONE) {
                        DSLOG_INFO("Port SPEAKER0: Initialized audio gain: %f", audioGainValue);
                    }
                }
                
                // HDMI init
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    try {
                        audioGain = device::HostPersistence::getInstance().getProperty("HDMI0.audio.Gain");
                    } catch(...) {
                        try {
                            DSLOG_INFO("HDMI0.audio.Gain not found in persistence store. Try system default");
                            audioGain = device::HostPersistence::getInstance().getDefaultProperty("HDMI0.audio.Gain");
                        } catch(...) {
                            audioGain = "0";
                        }
                    }
                    audioGainValue = atof(audioGain.c_str());
                    if (dsSetAudioGainFunc(handle, audioGainValue) == dsERR_NONE) {
                        DSLOG_INFO("Port HDMI0: Initialized audio gain: %f", audioGainValue);
                    }
                }
                // SPDIF init
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPDIF, 0, &handle) == dsERR_NONE) {
                    try {
                        audioGain = device::HostPersistence::getInstance().getProperty("SPDIF0.audio.Gain");
                    } catch(...) {
                        try {
                            DSLOG_INFO("SPDIF0.audio.Gain not found in persistence store. Try system default");
                            audioGain = device::HostPersistence::getInstance().getDefaultProperty("SPDIF0.audio.Gain");
                        } catch(...) {
                            audioGain = "0";
                        }
                    }
                    audioGainValue = atof(audioGain.c_str());
                    if (dsSetAudioGainFunc(handle, audioGainValue) == dsERR_NONE) {
                        DSLOG_INFO("Port SPDIF0: Initialized audio gain: %f", audioGainValue);
                    }
                }
            } else {
                DSLOG_INFO("dsSetAudioGain_t(int, float) is not available in HAL");
            }
            
            // 3. Initialize Audio Level for SPDIF, SPEAKER, HEADPHONE, and HDMI ports
            typedef dsError_t (*dsSetAudioLevel_t)(intptr_t handle, float level);
            static dsSetAudioLevel_t dsSetAudioLevelFunc = nullptr;
            
            if (dsSetAudioLevelFunc == nullptr) {
                dsSetAudioLevelFunc = (dsSetAudioLevel_t) resolve(RDK_DSHAL_NAME, "dsSetAudioLevel");
                if (dsSetAudioLevelFunc) {
                    DSLOG_INFO("dsSetAudioLevel_t(int, float) is defined and loaded");
                        std::string audioLevel("0");
                        float audioLevelValue = 0;
                        float lastAudioLevel = 0;
                        bool hasLastAudioLevel = false;
                        
                        // SPDIF init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPDIF, 0, &handle) == dsERR_NONE) {
                            try {
                                audioLevel = device::HostPersistence::getInstance().getProperty("SPDIF0.audio.Level");
                            } catch(...) {
                                try {
                                    DSLOG_INFO("SPDIF0.audio.Level not found in persistence store. Try system default");
                                    audioLevel = device::HostPersistence::getInstance().getDefaultProperty("SPDIF0.audio.Level");
                                } catch(...) {
                                    audioLevel = "40";
                                }
                            }
                            audioLevelValue = atof(audioLevel.c_str());
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                            g_audioLevelCacheSpdif.store(audioLevelValue);
#endif
                            lastAudioLevel = audioLevelValue;
                            hasLastAudioLevel = true;
                            if (dsSetAudioLevelFunc(handle, audioLevelValue) == dsERR_NONE) {
                                DSLOG_INFO("Port SPDIF0: Initialized audio level: %f", audioLevelValue);
                            }
                        }
                        
                        // SPEAKER init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            try {
                                audioLevel = device::HostPersistence::getInstance().getProperty("SPEAKER0.audio.Level");
                            } catch(...) {
                                try {
                                    DSLOG_INFO("SPEAKER0.audio.Level not found in persistence store. Try system default");
                                    audioLevel = device::HostPersistence::getInstance().getDefaultProperty("SPEAKER0.audio.Level");
                                } catch(...) {
                                    audioLevel = "40";
                                }
                            }
                            audioLevelValue = atof(audioLevel.c_str());
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                            g_audioLevelCacheSpeaker.store(audioLevelValue);
#endif
                            lastAudioLevel = audioLevelValue;
                            hasLastAudioLevel = true;
                            if (dsSetAudioLevelFunc(handle, audioLevelValue) == dsERR_NONE) {
                                DSLOG_INFO("Port SPEAKER0: Initialized audio level: %f", audioLevelValue);
                            }
                        }
                        
                        // HEADPHONE init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HEADPHONE, 0, &handle) == dsERR_NONE) {
                            try {
                                audioLevel = device::HostPersistence::getInstance().getProperty("HEADPHONE0.audio.Level");
                            } catch(...) {
                                try {
                                    DSLOG_INFO("HEADPHONE0.audio.Level not found in persistence store. Try system default");
                                    audioLevel = device::HostPersistence::getInstance().getDefaultProperty("HEADPHONE0.audio.Level");
                                } catch(...) {
                                    audioLevel = "40";
                                }
                            }
                            audioLevelValue = atof(audioLevel.c_str());
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                            g_audioLevelCacheHeadphone.store(audioLevelValue);
#endif
                            const bool isHeadphoneConnected = isAudioOutputConnectedForInitialization(handle);
                            if (isHeadphoneConnected) {
                                lastAudioLevel = audioLevelValue;
                                hasLastAudioLevel = true;
                            }
                            if (dsSetAudioLevelFunc(handle, audioLevelValue) == dsERR_NONE) {
                                DSLOG_INFO("Port HEADPHONE0: Initialized audio level: %f (connected: %d)",
                                    audioLevelValue, isHeadphoneConnected);
                            }
                        }
                        
                        // HDMI init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            try {
                                audioLevel = device::HostPersistence::getInstance().getProperty("HDMI0.audio.Level");
                            } catch(...) {
                                try {
                                    DSLOG_INFO("HDMI0.audio.Level not found in persistence store. Try system default");
                                    audioLevel = device::HostPersistence::getInstance().getDefaultProperty("HDMI0.audio.Level");
                                } catch(...) {
                                    audioLevel = "40";
                                }
                            }
                            audioLevelValue = atof(audioLevel.c_str());
#ifdef DS_AUDIO_SETTINGS_PERSISTENCE
                            g_audioLevelCacheHdmi.store(audioLevelValue);
#endif
                            lastAudioLevel = audioLevelValue;
                            hasLastAudioLevel = true;
                            if (dsSetAudioLevelFunc(handle, audioLevelValue) == dsERR_NONE) {
                                DSLOG_INFO("Port HDMI0: Initialized audio level: %f", audioLevelValue);
                            }
                        }

                        if (hasLastAudioLevel) {
                            g_LastVolumeLevel.store(lastAudioLevel);
                            DSLOG_INFO("Initialized cached audio level: %f", lastAudioLevel);
                        }
                } else {
                    DSLOG_INFO("dsSetAudioLevel_t(int, float) is not defined");
                }
            }
            
            // 4. Initialize Audio Delay for SPEAKER, HDMI, and HDMI_ARC ports
            typedef dsError_t (*dsSetAudioDelay_t)(intptr_t handle, uint32_t audioDelayMs);
            static dsSetAudioDelay_t dsSetAudioDelayFunc = nullptr;
            
            if (dsSetAudioDelayFunc == nullptr) {
                dsSetAudioDelayFunc = (dsSetAudioDelay_t) resolve(RDK_DSHAL_NAME, "dsSetAudioDelay");
                if (dsSetAudioDelayFunc) {
                    DSLOG_INFO("dsSetAudioDelay_t(int, uint32_t) is defined and loaded");
                        std::string audioDelay("0");
                        int audioDelayValue = 0;
                        
                        // SPEAKER init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            try {
                                audioDelay = device::HostPersistence::getInstance().getProperty("SPEAKER0.audio.Delay");
                            } catch(...) {
                                try {
                                    DSLOG_INFO("SPEAKER0.audio.Delay not found in persistence store. Try system default");
                                    audioDelay = device::HostPersistence::getInstance().getDefaultProperty("SPEAKER0.audio.Delay");
                                } catch(...) {
                                    audioDelay = "0";
                                }
                            }
                            audioDelayValue = atoi(audioDelay.c_str());
                            if (dsSetAudioDelayFunc(handle, audioDelayValue) == dsERR_NONE) {
                                DSLOG_INFO("Port SPEAKER0: Initialized audio delay: %d ms", audioDelayValue);
                            }
                        }
                        
                        // HDMI init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            try {
                                audioDelay = device::HostPersistence::getInstance().getProperty("HDMI0.audio.Delay");
                            } catch(...) {
                                try {
                                    DSLOG_INFO("HDMI0.audio.Delay not found in persistence store. Try system default");
                                    audioDelay = device::HostPersistence::getInstance().getDefaultProperty("HDMI0.audio.Delay");
                                } catch(...) {
                                    audioDelay = "0";
                                }
                            }
                            audioDelayValue = atoi(audioDelay.c_str());
                            if (dsSetAudioDelayFunc(handle, audioDelayValue) == dsERR_NONE) {
                                DSLOG_INFO("Port HDMI0: Initialized audio delay: %d ms", audioDelayValue);
                            }
                        }
                        
                        // HDMI ARC init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI_ARC, 0, &handle) == dsERR_NONE) {
                            try {
                                audioDelay = device::HostPersistence::getInstance().getProperty("HDMI_ARC0.audio.Delay");
                            } catch(...) {
                                try {
                                    DSLOG_INFO("HDMI_ARC0.audio.Delay not found in persistence store. Try system default");
                                    audioDelay = device::HostPersistence::getInstance().getDefaultProperty("HDMI_ARC0.audio.Delay");
                                } catch(...) {
                                    audioDelay = "0";
                                }
                            }
                            audioDelayValue = atoi(audioDelay.c_str());
                            if (dsSetAudioDelayFunc(handle, audioDelayValue) == dsERR_NONE) {
                                DSLOG_INFO("Port HDMI_ARC0: Initialized audio delay: %d ms", audioDelayValue);
                            }
                        }
                } else {
                    DSLOG_INFO("dsSetAudioDelay_t(int, uint32_t) is not defined");
                }
            }
            
            // 5. Initialize Primary Language
            typedef dsError_t (*dsSetPrimaryLanguage_t)(intptr_t handle, const char* pLang);
            static dsSetPrimaryLanguage_t dsSetPrimaryLanguageFunc = nullptr;
            
            if (dsSetPrimaryLanguageFunc == nullptr) {
                dsSetPrimaryLanguageFunc = (dsSetPrimaryLanguage_t) resolve(RDK_DSHAL_NAME, "dsSetPrimaryLanguage");
                if (dsSetPrimaryLanguageFunc) {
                    DSLOG_INFO("dsSetPrimaryLanguage_t(int, char*) is defined and loaded");
                        std::string primaryLanguage("eng");
                        handle = 0;
                        
                        try {
                            primaryLanguage = device::HostPersistence::getInstance().getProperty("audio.PrimaryLanguage");
                        } catch(...) {
                            try {
                                DSLOG_INFO("audio.PrimaryLanguage not found in persistence store. Try system default");
                                primaryLanguage = device::HostPersistence::getInstance().getDefaultProperty("audio.PrimaryLanguage");
                            } catch(...) {
                                primaryLanguage = "eng";
                            }
                        }
                        
                        if (dsSetPrimaryLanguageFunc(handle, primaryLanguage.c_str()) == dsERR_NONE) {
                            DSLOG_INFO("Initialized Primary Language: %s", primaryLanguage.c_str());
                        }
                } else {
                    DSLOG_INFO("dsSetPrimaryLanguage_t(int, char*) is not defined");
                }
            }
            
            // 6. Initialize Secondary Language
            typedef dsError_t (*dsSetSecondaryLanguage_t)(intptr_t handle, const char* sLang);
            static dsSetSecondaryLanguage_t dsSetSecondaryLanguageFunc = nullptr;
            
            if (dsSetSecondaryLanguageFunc == nullptr) {
                dsSetSecondaryLanguageFunc = (dsSetSecondaryLanguage_t) resolve(RDK_DSHAL_NAME, "dsSetSecondaryLanguage");
                if (dsSetSecondaryLanguageFunc) {
                    DSLOG_INFO("dsSetSecondaryLanguage_t(int, char*) is defined and loaded");
                        std::string secondaryLanguage("eng");
                        handle = 0;
                        
                        try {
                            secondaryLanguage = device::HostPersistence::getInstance().getProperty("audio.SecondaryLanguage");
                        } catch(...) {
                            try {
                                DSLOG_INFO("audio.SecondaryLanguage not found in persistence store. Try system default");
                                secondaryLanguage = device::HostPersistence::getInstance().getDefaultProperty("audio.SecondaryLanguage");
                            } catch(...) {
                                secondaryLanguage = "eng";
                            }
                        }
                        
                        if (dsSetSecondaryLanguageFunc(handle, secondaryLanguage.c_str()) == dsERR_NONE) {
                            DSLOG_INFO("Initialized Secondary Language: %s", secondaryLanguage.c_str());
                        }
                } else {
                    DSLOG_INFO("dsSetSecondaryLanguage_t(int, char*) is not defined");
                }
            }
            
            // 7. Initialize Fader Control
            typedef dsError_t (*dsSetFaderControl_t)(intptr_t handle, int mixerbalance);
            static dsSetFaderControl_t dsSetFaderControlFunc = nullptr;
            
            if (dsSetFaderControlFunc == nullptr) {
                dsSetFaderControlFunc = (dsSetFaderControl_t) resolve(RDK_DSHAL_NAME, "dsSetFaderControl");
                if (dsSetFaderControlFunc) {
                    DSLOG_INFO("dsSetFaderControl_t(int, int) is defined and loaded");
                        std::string faderControl("0");
                        int faderControlValue = 0;
                        handle = 0;
                        
                        try {
                            faderControl = device::HostPersistence::getInstance().getProperty("audio.FaderControl");
                        } catch(...) {
                            try {
                                DSLOG_INFO("audio.FaderControl not found in persistence store. Try system default");
                                faderControl = device::HostPersistence::getInstance().getDefaultProperty("audio.FaderControl");
                            } catch(...) {
                                faderControl = "0";
                            }
                        }
                        
                        faderControlValue = atoi(faderControl.c_str());
                        if (dsSetFaderControlFunc(handle, faderControlValue) == dsERR_NONE) {
                            DSLOG_INFO("Initialized Fader Control, mixing: %d", faderControlValue);
                        }
                } else {
                    DSLOG_INFO("dsSetFaderControl_t(int, int) is not defined");
                }
            }
            
            // 8. Initialize Associated Audio Mixing
            typedef dsError_t (*dsSetAssociatedAudioMixing_t)(intptr_t handle, bool mixing);
            static dsSetAssociatedAudioMixing_t dsSetAssociatedAudioMixingFunc = nullptr;
            
            if (dsSetAssociatedAudioMixingFunc == nullptr) {
                dsSetAssociatedAudioMixingFunc = (dsSetAssociatedAudioMixing_t) resolve(RDK_DSHAL_NAME, "dsSetAssociatedAudioMixing");
                if (dsSetAssociatedAudioMixingFunc) {
                    DSLOG_INFO("dsSetAssociatedAudioMixing_t (intptr_t handle, bool mixing) is defined and loaded");
                        std::string associatedAudioMixing("Disabled");
                        bool associatedAudioMixingValue = false;
                        handle = 0;
                        
                        try {
                            associatedAudioMixing = device::HostPersistence::getInstance().getProperty("audio.AssociatedAudioMixing");
                        } catch(...) {
                            try {
                                DSLOG_INFO("audio.AssociatedAudioMixing not found in persistence store. Try system default");
                                associatedAudioMixing = device::HostPersistence::getInstance().getDefaultProperty("audio.AssociatedAudioMixing");
                            } catch(...) {
                                associatedAudioMixing = "Disabled";
                            }
                        }
                        
                        associatedAudioMixingValue = (associatedAudioMixing == "Enabled");
                        if (dsSetAssociatedAudioMixingFunc(handle, associatedAudioMixingValue) == dsERR_NONE) {
                            DSLOG_INFO("Initialized AssociatedAudioMixingFunc: %s", associatedAudioMixingValue ? "enabled" : "disabled");
                        }
                    } else {
                        DSLOG_INFO("dsSetAssociatedAudioMixing_t (intptr_t handle, bool enable) is not defined");
                    }
                }
            #endif // DS_AUDIO_SETTINGS_PERSISTENCE
            
            // 9. Initialize MS12 Audio Profile Support
            std::string ms12ProfileSupport("FALSE");
            std::string ms12Profile("Off");
            
            try {
                ms12ProfileSupport = device::HostPersistence::getInstance().getDefaultProperty("audio.MS12Profile.supported");
            } catch(...) {
                ms12ProfileSupport = "FALSE";
                DSLOG_INFO("audio.MS12Profile.supported setting not found in hostDataDefault");
            }
            DSLOG_INFO("audio.MS12Profile.supported = %s", ms12ProfileSupport.c_str());
            
            if (ms12ProfileSupport == "TRUE") {
                // MS12 Profile is supported - initialize MS12 Audio Profile
                typedef dsError_t (*dsSetMS12AudioProfile_t)(intptr_t handle, const char* profile);
                static dsSetMS12AudioProfile_t dsSetMS12AudioProfileFunc = nullptr;

                if (dsSetMS12AudioProfileFunc == nullptr) {
                    dsSetMS12AudioProfileFunc = (dsSetMS12AudioProfile_t) resolve(RDK_DSHAL_NAME, "dsSetMS12AudioProfile");
                    if (dsSetMS12AudioProfileFunc) {
                        DSLOG_INFO("dsSetMS12AudioProfile_t(int, const char*) is defined and loaded");

                        try {
                            ms12Profile = device::HostPersistence::getInstance().getProperty("audio.MS12Profile");
                        } catch(...) {
                            try {
                                DSLOG_INFO("audio.MS12Profile not found in persistence store. Try system default");
                                ms12Profile = device::HostPersistence::getInstance().getDefaultProperty("audio.MS12Profile");
                            } catch(...) {
                                ms12Profile = "Off";
                            }
                        }

                        // SPEAKER init for MS12 profile
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetMS12AudioProfileFunc(handle, ms12Profile.c_str()) == dsERR_NONE) {
                                DSLOG_INFO("Port SPEAKER0: Initialized MS12 Audio Profile: %s", ms12Profile.c_str());
                                device::HostPersistence::getInstance().persistHostProperty("audio.MS12Profile", ms12Profile.c_str());
                            } else {
                                DSLOG_INFO("Port SPEAKER0: Initialization failed !!! MS12 Audio Profile: %s", ms12Profile.c_str());
                            }
                        }
                    } else {
                        DSLOG_INFO("dsSetMS12AudioProfile_t(int, const char*) is not defined");
                    }
                }
            }
            
            // Initialize individual MS12 settings based on profile support and override settings
            if ((ms12ProfileSupport == "TRUE") && (ms12Profile != "Off")) {
                // MS12 Profile supported and active - check for individual overrides
                initializeMS12ProfileOverrides();
            } else if (ms12ProfileSupport == "FALSE") {
                // MS12 Profile not supported - initialize individual settings from persistence
                initializeIndividualMS12Settings();
            }
            
            DSLOG_INFO("Comprehensive audio configuration initialization completed successfully");
            
        } catch (...) {
            DSLOG_ERR("Exception in audioConfigInit");
        }
        EXIT_LOG;
    }
    
    // Initialize MS12 profile override settings when profile is active
    void initializeMS12ProfileOverrides()
    {
        ENTRY_LOG;
        try {
            intptr_t handle = 0;
            std::string profileOverride = "FALSE";
            
            // Audio Compression Profile Override
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.Compression.ms12ProfileOverride");
            } catch(...) {
                profileOverride = "FALSE";
            }
            
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetAudioCompression_t)(intptr_t handle, int compressionLevel);
                dsSetAudioCompression_t dsSetAudioCompressionFunc = nullptr;
                
                dsSetAudioCompressionFunc = (dsSetAudioCompression_t) resolve(RDK_DSHAL_NAME, "dsSetAudioCompression");
                if (dsSetAudioCompressionFunc) {
                    try {
                        std::string audioCompression = device::HostPersistence::getInstance().getProperty("audio.Compression");
                        int compressionLevel = atoi(audioCompression.c_str());
                        
                        // SPEAKER and HDMI init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetAudioCompressionFunc(handle, compressionLevel) == dsERR_NONE) {
                                DSLOG_INFO("Port SPEAKER0: Initialized audio compression: %d", compressionLevel);
                            }
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetAudioCompressionFunc(handle, compressionLevel) == dsERR_NONE) {
                                DSLOG_INFO("Port HDMI0: Initialized audio compression: %d", compressionLevel);
                            }
                        }
                    } catch(...) {
                        DSLOG_INFO("audio.Compression not found in persistence store. System Default configured through profiles");
                    }
                }
            }
            
            // Dialog Enhancement Profile Override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.DialogEnhancer.ms12ProfileOverride");
            } catch(...) {
                profileOverride = "FALSE";
            }
            
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetDialogEnhancement_t)(intptr_t handle, int enhancerLevel);
                dsSetDialogEnhancement_t dsSetDialogEnhancementFunc = nullptr;
                
                dsSetDialogEnhancementFunc = (dsSetDialogEnhancement_t) resolve(RDK_DSHAL_NAME, "dsSetDialogEnhancement");
                if (dsSetDialogEnhancementFunc) {
                    try {
                        std::string currentProfile = getCurrentProfileProperty("EnhancerLevel");
                        std::string enhancerLevel = device::HostPersistence::getInstance().getProperty(currentProfile);
                        int enhancerValue = atoi(enhancerLevel.c_str());
                        
                        // SPEAKER and HDMI init
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetDialogEnhancementFunc(handle, enhancerValue) == dsERR_NONE) {
                                DSLOG_INFO("Port SPEAKER0: Initialized dialog enhancement level: %d", enhancerValue);
                            }
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetDialogEnhancementFunc(handle, enhancerValue) == dsERR_NONE) {
                                DSLOG_INFO("Port HDMI0: Initialized dialog enhancement level: %d", enhancerValue);
                            }
                        }
                    } catch(...) {
                        DSLOG_INFO("audio.EnhancerLevel not found in persistence store. System Default configured through profiles");
                    }
                }
            }
            
            // DolbyVolumeMode override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.DolbyVolumeMode.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetDolbyVolumeMode_ov_t)(intptr_t handle, bool enable);
                dsSetDolbyVolumeMode_ov_t dsSetDolbyVolumeModeFunc = (dsSetDolbyVolumeMode_ov_t) resolve(RDK_DSHAL_NAME, "dsSetDolbyVolumeMode");
                if (dsSetDolbyVolumeModeFunc) {
                    try {
                        std::string dolbyMode = device::HostPersistence::getInstance().getProperty("audio.DolbyVolumeMode");
                        bool m_dolbyVolumeMode = (dolbyMode == "TRUE");
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetDolbyVolumeModeFunc(handle, m_dolbyVolumeMode) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized Dolby Volume Mode: %d", m_dolbyVolumeMode);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetDolbyVolumeModeFunc(handle, m_dolbyVolumeMode) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized Dolby Volume Mode: %d", m_dolbyVolumeMode);
                        }
                    } catch(...) { DSLOG_INFO("audio.DolbyVolumeMode not found. System Default configured through profiles"); }
                }
            }

            // IntelligentEQ override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.IntelligentEQ.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetIEQMode_ov_t)(intptr_t handle, int mode);
                dsSetIEQMode_ov_t dsSetIEQModeFunc = (dsSetIEQMode_ov_t) resolve(RDK_DSHAL_NAME, "dsSetIntelligentEqualizerMode");
                if (dsSetIEQModeFunc) {
                    try {
                        int m_IEQMode = atoi(device::HostPersistence::getInstance().getProperty("audio.IntelligentEQ").c_str());
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetIEQModeFunc(handle, m_IEQMode) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized Intelligent Equalizer mode: %d", m_IEQMode);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetIEQModeFunc(handle, m_IEQMode) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized Intelligent Equalizer mode: %d", m_IEQMode);
                        }
                    } catch(...) { DSLOG_INFO("audio.IntelligentEQ not found. System Default configured through profiles"); }
                }
            }

            // VolumeLeveller override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.VolumeLeveller.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetVolLev_ov_t)(intptr_t handle, dsVolumeLeveller_t volLeveller);
                dsSetVolLev_ov_t dsSetVolLevFunc = (dsSetVolLev_ov_t) resolve(RDK_DSHAL_NAME, "dsSetVolumeLeveller");
                if (dsSetVolLevFunc) {
                    std::string _pMode = getCurrentProfileProperty("VolumeLeveller.mode");
                    std::string _pLevel = getCurrentProfileProperty("VolumeLeveller.level");
                    try {
                        dsVolumeLeveller_t m_vl;
                        m_vl.mode  = atoi(device::HostPersistence::getInstance().getProperty(_pMode).c_str());
                        m_vl.level = atoi(device::HostPersistence::getInstance().getProperty(_pLevel).c_str());
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetVolLevFunc(handle, m_vl) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized Volume Leveller: Mode: %d, Level: %d", m_vl.mode, m_vl.level);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetVolLevFunc(handle, m_vl) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized Volume Leveller: Mode: %d, Level: %d", m_vl.mode, m_vl.level);
                        }
                    } catch(...) { DSLOG_INFO("audio.VolumeLeveller not found. System Default configured through profiles"); }
                }
            }

            // BassBoost override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.BassBoost.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetBass_ov_t)(intptr_t handle, int boost);
                dsSetBass_ov_t dsSetBassFunc = (dsSetBass_ov_t) resolve(RDK_DSHAL_NAME, "dsSetBassEnhancer");
                if (dsSetBassFunc) {
                    try {
                        int m_bassBoost = atoi(device::HostPersistence::getInstance().getProperty("audio.BassBoost").c_str());
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetBassFunc(handle, m_bassBoost) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized Bass Boost: %d", m_bassBoost);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetBassFunc(handle, m_bassBoost) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized Bass Boost: %d", m_bassBoost);
                        }
                    } catch(...) { DSLOG_INFO("audio.BassBoost not found. System Default configured through profiles"); }
                }
            }

            // SurroundDecoder override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.SurroundDecoder.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsEnableSurrDec_ov_t)(intptr_t handle, bool enabled);
                dsEnableSurrDec_ov_t dsEnableSurrDecFunc = (dsEnableSurrDec_ov_t) resolve(RDK_DSHAL_NAME, "dsEnableSurroundDecoder");
                if (dsEnableSurrDecFunc) {
                    try {
                        std::string sd = device::HostPersistence::getInstance().getProperty("audio.SurroundDecoderEnabled");
                        bool m_surroundDecoder = (sd == "TRUE");
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsEnableSurrDecFunc(handle, m_surroundDecoder) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized Surround Decoder: %d", m_surroundDecoder);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsEnableSurrDecFunc(handle, m_surroundDecoder) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized Surround Decoder: %d", m_surroundDecoder);
                        }
                    } catch(...) { DSLOG_INFO("audio.SurroundDecoderEnabled not found. System Default configured through profiles"); }
                }
            }

            // DRCMode override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.DRCMode.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetDRC_ov_t)(intptr_t handle, int mode);
                dsSetDRC_ov_t dsSetDRCFunc = (dsSetDRC_ov_t) resolve(RDK_DSHAL_NAME, "dsSetDRCMode");
                if (dsSetDRCFunc) {
                    try {
                        std::string drc = device::HostPersistence::getInstance().getProperty("audio.DRCMode");
                        int m_DRCMode = (drc == "RF") ? 1 : 0;
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetDRCFunc(handle, m_DRCMode) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized DRCMode: %d", m_DRCMode);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetDRCFunc(handle, m_DRCMode) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized DRCMode: %d", m_DRCMode);
                        }
                    } catch(...) { DSLOG_INFO("audio.DRCMode not found. System Default configured through profiles"); }
                }
            }

            // SurroundVirtualizer override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.SurroundVirtualizer.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetSurrVirt_ov_t)(intptr_t handle, dsSurroundVirtualizer_t virtualizer);
                dsSetSurrVirt_ov_t dsSetSurrVirtFunc = (dsSetSurrVirt_ov_t) resolve(RDK_DSHAL_NAME, "dsSetSurroundVirtualizer");
                if (dsSetSurrVirtFunc) {
                    std::string _pMode = getCurrentProfileProperty("SurroundVirtualizer.mode");
                    std::string _pBoost = getCurrentProfileProperty("SurroundVirtualizer.boost");
                    try {
                        dsSurroundVirtualizer_t m_virt;
                        m_virt.mode  = atoi(device::HostPersistence::getInstance().getProperty(_pMode).c_str());
                        m_virt.boost = atoi(device::HostPersistence::getInstance().getProperty(_pBoost).c_str());
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetSurrVirtFunc(handle, m_virt) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized Surround Virtualizer: Mode: %d, Boost: %d", m_virt.mode, m_virt.boost);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetSurrVirtFunc(handle, m_virt) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized Surround Virtualizer: Mode: %d, Boost: %d", m_virt.mode, m_virt.boost);
                        }
                    } catch(...) { DSLOG_INFO("audio.SurroundVirtualizer not found. System Default configured through profiles"); }
                }
            }

            // MISteering override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.MISteering.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetMISteering_ov_t)(intptr_t handle, bool enabled);
                dsSetMISteering_ov_t dsSetMIFunc = (dsSetMISteering_ov_t) resolve(RDK_DSHAL_NAME, "dsSetMISteering");
                if (dsSetMIFunc) {
                    try {
                        std::string mi = device::HostPersistence::getInstance().getProperty("audio.MISteering");
                        bool m_MISteering = (mi == "Enabled");
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetMIFunc(handle, m_MISteering) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized MI Steering: %d", m_MISteering);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetMIFunc(handle, m_MISteering) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized MI Steering: %d", m_MISteering);
                        }
                    } catch(...) { DSLOG_INFO("audio.MISteering not found. System Default configured through profiles"); }
                }
            }

            // GraphicEQ override
            profileOverride = "FALSE";
            try {
                profileOverride = device::HostPersistence::getInstance().getDefaultProperty("audio.GraphicEQ.ms12ProfileOverride");
            } catch(...) { profileOverride = "FALSE"; }
            if (profileOverride == "TRUE") {
                typedef dsError_t (*dsSetGEQ_ov_t)(intptr_t handle, int mode);
                dsSetGEQ_ov_t dsSetGEQFunc = (dsSetGEQ_ov_t) resolve(RDK_DSHAL_NAME, "dsSetGraphicEqualizerMode");
                if (dsSetGEQFunc) {
                    try {
                        int m_GEQMode = atoi(device::HostPersistence::getInstance().getProperty("audio.GraphicEQ").c_str());
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                            if (dsSetGEQFunc(handle, m_GEQMode) == dsERR_NONE)
                                DSLOG_INFO("Port SPEAKER0: Initialized Graphic Equalizer mode: %d", m_GEQMode);
                        }
                        handle = 0;
                        if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                            if (dsSetGEQFunc(handle, m_GEQMode) == dsERR_NONE)
                                DSLOG_INFO("Port HDMI0: Initialized Graphic Equalizer mode: %d", m_GEQMode);
                        }
                    } catch(...) { DSLOG_INFO("audio.GraphicEQ not found. System Default configured through profiles"); }
                }
            }

        } catch (...) {
            DSLOG_ERR("Exception in initializeMS12ProfileOverrides");
        }
        EXIT_LOG;
    }
    
    // Initialize individual MS12 settings when profile is not supported
    void initializeIndividualMS12Settings()
    {
        ENTRY_LOG;
        try {
            intptr_t handle = 0;
            
            // Initialize Audio Compression
            typedef dsError_t (*dsSetAudioCompression_t)(intptr_t handle, int compressionLevel);
            dsSetAudioCompression_t dsSetAudioCompressionFunc = nullptr;
            
            dsSetAudioCompressionFunc = (dsSetAudioCompression_t) resolve(RDK_DSHAL_NAME, "dsSetAudioCompression");
            if (dsSetAudioCompressionFunc) {
                std::string audioCompression("0");
                try {
                    audioCompression = device::HostPersistence::getInstance().getProperty("audio.Compression");
                } catch(...) {
                    try {
                        audioCompression = device::HostPersistence::getInstance().getDefaultProperty("audio.Compression");
                    } catch(...) {
                        audioCompression = "0";
                    }
                }
                
                int compressionLevel = atoi(audioCompression.c_str());
                
                // SPEAKER and HDMI init
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetAudioCompressionFunc(handle, compressionLevel) == dsERR_NONE) {
                        DSLOG_INFO("Port SPEAKER0: Initialized audio compression: %d", compressionLevel);
                    }
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetAudioCompressionFunc(handle, compressionLevel) == dsERR_NONE) {
                        DSLOG_INFO("Port HDMI0: Initialized audio compression: %d", compressionLevel);
                    }
                }
            }
            
            // Initialize Dialog Enhancement
            typedef dsError_t (*dsSetDialogEnhancement_t)(intptr_t handle, int enhancerLevel);
            dsSetDialogEnhancement_t dsSetDialogEnhancementFunc = nullptr;
            
            dsSetDialogEnhancementFunc = (dsSetDialogEnhancement_t) resolve(RDK_DSHAL_NAME, "dsSetDialogEnhancement");
            if (dsSetDialogEnhancementFunc) {
                std::string enhancerLevel("0");
                try {
                    enhancerLevel = device::HostPersistence::getInstance().getProperty("audio.EnhancerLevel");
                } catch(...) {
                    try {
                        enhancerLevel = device::HostPersistence::getInstance().getDefaultProperty("audio.EnhancerLevel");
                    } catch(...) {
                        enhancerLevel = "0";
                    }
                }
                
                int enhancerValue = atoi(enhancerLevel.c_str());
                
                // SPEAKER and HDMI init
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetDialogEnhancementFunc(handle, enhancerValue) == dsERR_NONE) {
                        DSLOG_INFO("Port SPEAKER0: Initialized dialog enhancement level: %d", enhancerValue);
                    }
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetDialogEnhancementFunc(handle, enhancerValue) == dsERR_NONE) {
                        DSLOG_INFO("Port HDMI0: Initialized dialog enhancement level: %d", enhancerValue);
                    }
                }
            }
            
            // DolbyVolumeMode (with bDolbyVolumeOverrideCheck: VolumeLeveller overrides DolbyVolumeMode)
            typedef dsError_t (*dsSetDolbyVolumeMode_ind_t)(intptr_t handle, bool enable);
            dsSetDolbyVolumeMode_ind_t dsSetDolbyVolumeModeIndFunc = nullptr;
            bool bDolbyVolumeOverrideCheck = true;
            dsSetDolbyVolumeModeIndFunc = (dsSetDolbyVolumeMode_ind_t) resolve(RDK_DSHAL_NAME, "dsSetDolbyVolumeMode");
            if (dsSetDolbyVolumeModeIndFunc) {
                std::string dolbyMode("FALSE");
                bool m_dolbyVolumeMode = false;
                try {
                    dolbyMode = device::HostPersistence::getInstance().getProperty("audio.DolbyVolumeMode");
                    bDolbyVolumeOverrideCheck = false;
                    DSLOG_INFO("audio.DolbyVolumeMode found in persistence store: %s", dolbyMode.c_str());
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.DolbyVolumeMode not found in persistence store. Try system default");
                        dolbyMode = device::HostPersistence::getInstance().getDefaultProperty("audio.DolbyVolumeMode");
                    } catch(...) { 
                        DSLOG_WARN("audio.DolbyVolumeMode not found in system default.");
                    }
                }
                m_dolbyVolumeMode = (dolbyMode == "TRUE");
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetDolbyVolumeModeIndFunc(handle, m_dolbyVolumeMode) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized Dolby Volume Mode: %d", m_dolbyVolumeMode);
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetDolbyVolumeModeIndFunc(handle, m_dolbyVolumeMode) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized Dolby Volume Mode: %d", m_dolbyVolumeMode);
                }
            }

            // IntelligentEQ
            typedef dsError_t (*dsSetIEQMode_ind_t)(intptr_t handle, int mode);
            dsSetIEQMode_ind_t dsSetIEQModeIndFunc = nullptr;
            dsSetIEQModeIndFunc = (dsSetIEQMode_ind_t) resolve(RDK_DSHAL_NAME, "dsSetIntelligentEqualizerMode");
            if (dsSetIEQModeIndFunc) {
                std::string ieqMode("0");
                try {
                    ieqMode = device::HostPersistence::getInstance().getProperty("audio.IntelligentEQ");
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.IntelligentEQ not found in persistence store. Try system default");
                        ieqMode = device::HostPersistence::getInstance().getDefaultProperty("audio.IntelligentEQ");
                    } catch(...) { ieqMode = "0"; }
                }
                int m_IEQMode = atoi(ieqMode.c_str());
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetIEQModeIndFunc(handle, m_IEQMode) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized Intelligent Equalizer mode: %d", m_IEQMode);
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetIEQModeIndFunc(handle, m_IEQMode) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized Intelligent Equalizer mode: %d", m_IEQMode);
                }
            }

            // VolumeLeveller (bDolbyVolumeOverrideCheck: set true if found, then apply instead of DolbyVolumeMode)
            typedef dsError_t (*dsSetVolLev_ind_t)(intptr_t handle, dsVolumeLeveller_t volLeveller);
            dsSetVolLev_ind_t dsSetVolLevIndFunc = nullptr;
            dsSetVolLevIndFunc = (dsSetVolLev_ind_t) resolve(RDK_DSHAL_NAME, "dsSetVolumeLeveller");
            if (dsSetVolLevIndFunc) {
                std::string volMode("0"), volLevel("0");
                dsVolumeLeveller_t m_vl;
                try {
                    volMode  = device::HostPersistence::getInstance().getProperty("audio.VolumeLeveller.mode");
                    volLevel = device::HostPersistence::getInstance().getProperty("audio.VolumeLeveller.level");
                    bDolbyVolumeOverrideCheck = true;
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.VolumeLeveller not found in persistence store. Try system default");
                        volMode  = device::HostPersistence::getInstance().getDefaultProperty("audio.VolumeLeveller.mode");
                        volLevel = device::HostPersistence::getInstance().getDefaultProperty("audio.VolumeLeveller.level");
                    } catch(...) { volMode = "0"; volLevel = "0"; }
                }
                m_vl.mode  = atoi(volMode.c_str());
                m_vl.level = atoi(volLevel.c_str());
                DSLOG_INFO("bDolbyVolumeOverrideCheck value: %d", (int)bDolbyVolumeOverrideCheck);
                handle = 0;
                if (bDolbyVolumeOverrideCheck && dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetVolLevIndFunc(handle, m_vl) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized Volume Leveller: Mode: %d, Level: %d", m_vl.mode, m_vl.level);
                }
                handle = 0;
                if (bDolbyVolumeOverrideCheck && dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetVolLevIndFunc(handle, m_vl) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized Volume Leveller: Mode: %d, Level: %d", m_vl.mode, m_vl.level);
                }
            }

            // BassBoost
            typedef dsError_t (*dsSetBass_ind_t)(intptr_t handle, int boost);
            dsSetBass_ind_t dsSetBassIndFunc = nullptr;
            dsSetBassIndFunc = (dsSetBass_ind_t) resolve(RDK_DSHAL_NAME, "dsSetBassEnhancer");
            if (dsSetBassIndFunc) {
                std::string bassBoost("0");
                try {
                    bassBoost = device::HostPersistence::getInstance().getProperty("audio.BassBoost");
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.BassBoost not found in persistence store. Try system default");
                        bassBoost = device::HostPersistence::getInstance().getDefaultProperty("audio.BassBoost");
                    } catch(...) { bassBoost = "0"; }
                }
                int m_bassBoost = atoi(bassBoost.c_str());
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetBassIndFunc(handle, m_bassBoost) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized Bass Boost: %d", m_bassBoost);
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetBassIndFunc(handle, m_bassBoost) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized Bass Boost: %d", m_bassBoost);
                }
            }

            // SurroundDecoder
            typedef dsError_t (*dsEnableSurrDec_ind_t)(intptr_t handle, bool enabled);
            dsEnableSurrDec_ind_t dsEnableSurrDecIndFunc = nullptr;
            dsEnableSurrDecIndFunc = (dsEnableSurrDec_ind_t) resolve(RDK_DSHAL_NAME, "dsEnableSurroundDecoder");
            if (dsEnableSurrDecIndFunc) {
                std::string sd("FALSE");
                try {
                    sd = device::HostPersistence::getInstance().getProperty("audio.SurroundDecoderEnabled");
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.SurroundDecoderEnabled not found in persistence store. Try system default");
                        sd = device::HostPersistence::getInstance().getDefaultProperty("audio.SurroundDecoderEnabled");
                    } catch(...) { sd = "FALSE"; }
                }
                bool m_surroundDecoder = (sd == "TRUE");
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsEnableSurrDecIndFunc(handle, m_surroundDecoder) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized Surround Decoder: %d", m_surroundDecoder);
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsEnableSurrDecIndFunc(handle, m_surroundDecoder) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized Surround Decoder: %d", m_surroundDecoder);
                }
            }

            // DRCMode
            typedef dsError_t (*dsSetDRC_ind_t)(intptr_t handle, int mode);
            dsSetDRC_ind_t dsSetDRCIndFunc = nullptr;
            dsSetDRCIndFunc = (dsSetDRC_ind_t) resolve(RDK_DSHAL_NAME, "dsSetDRCMode");
            if (dsSetDRCIndFunc) {
                std::string drcMode("Line");
                try {
                    drcMode = device::HostPersistence::getInstance().getProperty("audio.DRCMode");
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.DRCMode not found in persistence store. Try system default");
                        drcMode = device::HostPersistence::getInstance().getDefaultProperty("audio.DRCMode");
                    } catch(...) { drcMode = "Line"; }
                }
                int m_DRCMode = (drcMode == "RF") ? 1 : 0;
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetDRCIndFunc(handle, m_DRCMode) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized DRCMode: %d", m_DRCMode);
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetDRCIndFunc(handle, m_DRCMode) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized DRCMode: %d", m_DRCMode);
                }
            }

            // SurroundVirtualizer
            typedef dsError_t (*dsSetSurrVirt_ind_t)(intptr_t handle, dsSurroundVirtualizer_t virtualizer);
            dsSetSurrVirt_ind_t dsSetSurrVirtIndFunc = nullptr;
            dsSetSurrVirtIndFunc = (dsSetSurrVirt_ind_t) resolve(RDK_DSHAL_NAME, "dsSetSurroundVirtualizer");
            if (dsSetSurrVirtIndFunc) {
                std::string svMode("0"), svBoost("0");
                dsSurroundVirtualizer_t m_virt;
                try {
                    svMode  = device::HostPersistence::getInstance().getProperty("audio.SurroundVirtualizer.mode");
                    svBoost = device::HostPersistence::getInstance().getProperty("audio.SurroundVirtualizer.boost");
                    m_virt.mode  = atoi(svMode.c_str());
                    m_virt.boost = atoi(svBoost.c_str());
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.SurroundVirtualizer.mode/boost not found in persistence store. Try system default");
                        svMode  = device::HostPersistence::getInstance().getDefaultProperty("audio.SurroundVirtualizer.mode");
                        svBoost = device::HostPersistence::getInstance().getDefaultProperty("audio.SurroundVirtualizer.boost");
                    } catch(...) { svMode = "0"; svBoost = "0"; }
                }
                m_virt.mode  = atoi(svMode.c_str());
                m_virt.boost = atoi(svBoost.c_str());
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetSurrVirtIndFunc(handle, m_virt) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized Surround Virtualizer: Mode: %d, Boost: %d", m_virt.mode, m_virt.boost);
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetSurrVirtIndFunc(handle, m_virt) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized Surround Virtualizer: Mode: %d, Boost: %d", m_virt.mode, m_virt.boost);
                }
            }

            // MISteering
            typedef dsError_t (*dsSetMISteering_ind_t)(intptr_t handle, bool enabled);
            dsSetMISteering_ind_t dsSetMIIndFunc = nullptr;
            dsSetMIIndFunc = (dsSetMISteering_ind_t) resolve(RDK_DSHAL_NAME, "dsSetMISteering");
            if (dsSetMIIndFunc) {
                std::string miSteering("Disabled");
                try {
                    miSteering = device::HostPersistence::getInstance().getProperty("audio.MISteering");
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.MISteering not found in persistence store. Try system default");
                        miSteering = device::HostPersistence::getInstance().getDefaultProperty("audio.MISteering");
                    } catch(...) { miSteering = "Disabled"; }
                }
                bool m_MISteering = (miSteering == "Enabled");
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetMIIndFunc(handle, m_MISteering) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized MI Steering: %d", m_MISteering);
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetMIIndFunc(handle, m_MISteering) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized MI Steering: %d", m_MISteering);
                    else
                        DSLOG_INFO("Port HDMI0: Initialization MI Steering: %d failed. Port not available", m_MISteering);
                }
            }

            // GraphicEQ
            typedef dsError_t (*dsSetGEQ_ind_t)(intptr_t handle, int mode);
            dsSetGEQ_ind_t dsSetGEQIndFunc = nullptr;
            dsSetGEQIndFunc = (dsSetGEQ_ind_t) resolve(RDK_DSHAL_NAME, "dsSetGraphicEqualizerMode");
            if (dsSetGEQIndFunc) {
                std::string geqMode("0");
                try {
                    geqMode = device::HostPersistence::getInstance().getProperty("audio.GraphicEQ");
                } catch(...) {
                    try {
                        DSLOG_INFO("audio.GraphicEQ not found in persistence store. Try system default");
                        geqMode = device::HostPersistence::getInstance().getDefaultProperty("audio.GraphicEQ");
                    } catch(...) { geqMode = "0"; }
                }
                int m_GEQMode = atoi(geqMode.c_str());
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                    if (dsSetGEQIndFunc(handle, m_GEQMode) == dsERR_NONE)
                        DSLOG_INFO("Port SPEAKER0: Initialized Graphic Equalizer mode: %d", m_GEQMode);
                }
                handle = 0;
                if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                    if (dsSetGEQIndFunc(handle, m_GEQMode) == dsERR_NONE)
                        DSLOG_INFO("Port HDMI0: Initialized Graphic Equalizer mode: %d", m_GEQMode);
                }
            }

        } catch (...) {
            DSLOG_ERR("Exception in initializeIndividualMS12Settings");
        }
        EXIT_LOG;
    }
    
    // Helper method to get current profile property
    std::string getCurrentProfileProperty(const std::string& property)
    {
        std::string currentProfile = "Off";
        try {
            currentProfile = device::HostPersistence::getInstance().getProperty("audio.MS12Profile");
        } catch(...) {
            currentProfile = "Off";
        }
        
        return generateProfileProperty(currentProfile, property);
    }
    
    // Helper method to generate profile property string
    std::string generateProfileProperty(const std::string& profile, const std::string& property)
    {
        return "audio." + profile + "." + property;
    }
    
    // Resolve function - exactly like HDMI implementation
    static void* resolve(const std::string& libName, const std::string& symbolName) {
        void* handle = dlopen(libName.c_str(), RTLD_LAZY);
        if (!handle) {
            std::cerr << "dlopen failed for " << libName << ": " << dlerror() << std::endl;
            return nullptr;
        }
        void* symbol = dlsym(handle, symbolName.c_str());
        if (!symbol) {
            std::cerr << "dlsym failed for " << symbolName << ": " << dlerror() << std::endl;
        }
        dlclose(handle);  // Fix resource leak
        return symbol;
    }
    
    // Initialize audio port settings (from dsAudioMgr_init)
    void initializeAudioPortSettings()
    {
        ENTRY_LOG;
        try {
            DSLOG_INFO("Starting comprehensive audio port settings initialization from persistence...");
            
            // Initialize HDMI Audio Mode Settings from Persistence
            #ifdef IGNORE_EDID_LOGIC
            std::string hdmiAudioModeSettings("SURROUND");
            #else
            std::string hdmiAudioModeSettings("STEREO");
            #endif
            
            dsAudioStereoMode_t hdmiAudioMode;
            
            DSLOG_INFO("Checking Host persistence for HDMI audio settings");
            try {
                hdmiAudioModeSettings = device::HostPersistence::getInstance().getProperty("HDMI0.AudioMode");
            } catch(...) {
                DSLOG_INFO("HDMI0.AudioMode not in host persistence. Checking default.");
                try {
                    hdmiAudioModeSettings = device::HostPersistence::getInstance().getDefaultProperty("HDMI0.AudioMode");
                } catch(...) {
                    DSLOG_INFO("HDMI0.AudioMode not in default host persistence.");
                }
            }
            
            DSLOG_INFO("The HDMI Audio Mode Setting on startup is %s", hdmiAudioModeSettings.c_str());
            
            // Parse HDMI audio mode string to enum
            if (hdmiAudioModeSettings.compare("SURROUND") == 0) {
                hdmiAudioMode = dsAUDIO_STEREO_SURROUND;
            } else if (hdmiAudioModeSettings.compare("PASSTHRU") == 0) {
                hdmiAudioMode = dsAUDIO_STEREO_PASSTHRU;
            } else if (hdmiAudioModeSettings.compare("DOLBYDIGITAL") == 0) {
                hdmiAudioMode = dsAUDIO_STEREO_DD;
            } else if (hdmiAudioModeSettings.compare("DOLBYDIGITALPLUS") == 0) {
                hdmiAudioMode = dsAUDIO_STEREO_DDPLUS;
            } else if (hdmiAudioModeSettings.compare("STEREO") == 0) {
                hdmiAudioMode = dsAUDIO_STEREO_STEREO;
            } else {
                #ifdef IGNORE_EDID_LOGIC
                hdmiAudioMode = dsAUDIO_STEREO_SURROUND;
                #else
                hdmiAudioMode = dsAUDIO_STEREO_STEREO;
                #endif
            }
            
            // Initialize Audio Auto Mode Settings from Persistence
            std::string hdmiAudioModeAuto("FALSE");
            bool hdmiAutoMode = false;
            
            try {
                hdmiAudioModeAuto = device::HostPersistence::getInstance().getProperty("HDMI0.AudioMode.AUTO");
            } catch(...) {
                DSLOG_INFO("HDMI0.AudioMode.AUTO not found in persistence store. Try system default");
                try {
                    hdmiAudioModeAuto = device::HostPersistence::getInstance().getDefaultProperty("HDMI0.AudioMode.AUTO");
                } catch(...) {
                    #ifdef IGNORE_EDID_LOGIC
                    hdmiAudioModeAuto = "TRUE";
                    #else
                    hdmiAudioModeAuto = "FALSE";
                    #endif
                }
            }
            
            // Initialize ARC Audio Auto Mode Settings
            std::string arcAudioModeAuto("FALSE");
            bool arcAutoMode = false;
            
            try {
                arcAudioModeAuto = device::HostPersistence::getInstance().getProperty("HDMI_ARC0.AudioMode.AUTO");
            } catch(...) {
                try {
                    DSLOG_INFO("HDMI_ARC0.AudioMode.AUTO not found in persistence store. Try system default");
                    arcAudioModeAuto = device::HostPersistence::getInstance().getDefaultProperty("HDMI_ARC0.AudioMode.AUTO");
                } catch(...) {
                    arcAudioModeAuto = "FALSE";
                }
            }
            
            // Initialize SPDIF Audio Auto Mode Settings
            std::string spdifAudioModeAuto("FALSE");
            bool spdifAutoMode = false;
            
            try {
                spdifAudioModeAuto = device::HostPersistence::getInstance().getProperty("SPDIF0.AudioMode.AUTO");
            } catch(...) {
                try {
                    DSLOG_INFO("SPDIF0.AudioMode.AUTO not found in persistence store. Try system default");
                    spdifAudioModeAuto = device::HostPersistence::getInstance().getDefaultProperty("SPDIF0.AudioMode.AUTO");
                } catch(...) {
                    spdifAudioModeAuto = "FALSE";
                }
            }
            
            // Initialize SPEAKER Audio Auto Mode Settings
            std::string speakerAudioModeAuto("TRUE");
            bool speakerAutoMode = true;
            
            try {
                speakerAudioModeAuto = device::HostPersistence::getInstance().getProperty("SPEAKER0.AudioMode.AUTO");
            } catch(...) {
                try {
                    DSLOG_INFO("SPEAKER0.AudioMode.AUTO not found in persistence store. Try system default");
                    speakerAudioModeAuto = device::HostPersistence::getInstance().getDefaultProperty("SPEAKER0.AudioMode.AUTO");
                } catch(...) {
                    speakerAudioModeAuto = "TRUE";
                }
            }
            
            // Parse auto mode settings
            hdmiAutoMode = (hdmiAudioModeAuto.compare("TRUE") == 0);
            arcAutoMode = (arcAudioModeAuto.compare("TRUE") == 0);
            spdifAutoMode = (spdifAudioModeAuto.compare("TRUE") == 0);
            speakerAutoMode = (speakerAudioModeAuto.compare("TRUE") == 0);
            
            DSLOG_INFO("The HDMI Audio Auto Setting on startup is %s", hdmiAudioModeAuto.c_str());
            DSLOG_INFO("The HDMI ARC Audio Auto Setting on startup is %s", arcAudioModeAuto.c_str());
            DSLOG_INFO("The SPDIF Audio Auto Setting on startup is %s", spdifAudioModeAuto.c_str());
            DSLOG_INFO("The SPEAKER Audio Auto Setting on startup is %s", speakerAudioModeAuto.c_str());
            
            // Initialize SPDIF Audio Mode Settings
            std::string spdifModeSettings("STEREO");
            dsAudioStereoMode_t spdifAudioMode;
            
            spdifModeSettings = device::HostPersistence::getInstance().getProperty("SPDIF0.AudioMode", spdifModeSettings);
            DSLOG_INFO("The SPDIF Audio Mode Setting on startup is %s", spdifModeSettings.c_str());
            
            if (spdifModeSettings.compare("SURROUND") == 0) {
                spdifAudioMode = dsAUDIO_STEREO_SURROUND;
            } else if (spdifModeSettings.compare("PASSTHRU") == 0) {
                spdifAudioMode = dsAUDIO_STEREO_PASSTHRU;
            } else {
                spdifAudioMode = dsAUDIO_STEREO_STEREO;
            }
            
            // Initialize HDMI ARC Audio Mode Settings  
            std::string arcModeSettings("STEREO");
            dsAudioStereoMode_t arcAudioMode;
            
            arcModeSettings = device::HostPersistence::getInstance().getProperty("HDMI_ARC0.AudioMode", arcModeSettings);
            DSLOG_INFO("The HDMI ARC Audio Mode Setting on startup is %s", arcModeSettings.c_str());
            
            if (arcModeSettings.compare("SURROUND") == 0) {
                arcAudioMode = dsAUDIO_STEREO_SURROUND;
            } else if (arcModeSettings.compare("PASSTHRU") == 0) {
                arcAudioMode = dsAUDIO_STEREO_PASSTHRU;
            } else {
                arcAudioMode = dsAUDIO_STEREO_STEREO;
            }
            
            // Initialize SPEAKER Audio Mode Settings
            std::string speakerModeSettings("SURROUND");
            dsAudioStereoMode_t speakerAudioMode;
            
            try {
                speakerModeSettings = device::HostPersistence::getInstance().getProperty("SPEAKER0.AudioMode", speakerModeSettings);
                DSLOG_INFO("The SPEAKER Audio Mode Setting on startup is %s", speakerModeSettings.c_str());
            } catch(...) {
                speakerModeSettings = "SURROUND";
            }
            
            if (speakerModeSettings.compare("SURROUND") == 0) {
                speakerAudioMode = dsAUDIO_STEREO_SURROUND;
            } else if (speakerModeSettings.compare("PASSTHRU") == 0) {
                speakerAudioMode = dsAUDIO_STEREO_PASSTHRU;
            } else if (speakerModeSettings.compare("STEREO") == 0) {
                speakerAudioMode = dsAUDIO_STEREO_STEREO;
            } else {
                speakerAudioMode = dsAUDIO_STEREO_SURROUND;
            }

            g_AudioHdmiAuto = hdmiAutoMode;
            g_AudioSpdifAuto = spdifAutoMode;
            g_AudioHdmiArcAuto = arcAutoMode;
            g_AudioSpeakerAuto = speakerAutoMode;
            g_HdmiAudioMode = hdmiAudioMode;
            g_SpdifAudioMode = spdifAudioMode;
            g_HdmiArcAudioMode = arcAudioMode;
            g_SpeakerAudioMode = speakerAudioMode;
            
            // Apply audio port settings using HAL functions
            intptr_t handle = 0;
            
            // Set HDMI port audio mode
            if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &handle) == dsERR_NONE) {
                if (dsSetStereoMode(handle, hdmiAudioMode) == dsERR_NONE) {
                    DSLOG_INFO("HDMI0: Applied audio mode: %d", hdmiAudioMode);
                }
                if (dsSetStereoAuto(handle, hdmiAutoMode ? 1 : 0) == dsERR_NONE) {
                    DSLOG_INFO("HDMI0: Applied auto mode: %s", hdmiAutoMode ? "TRUE" : "FALSE");
                }
            }
            
            // Set SPDIF port audio mode
            handle = 0;
            if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPDIF, 0, &handle) == dsERR_NONE) {
                if (dsSetStereoMode(handle, spdifAudioMode) == dsERR_NONE) {
                    DSLOG_INFO("SPDIF0: Applied audio mode: %d", spdifAudioMode);
                }
                if (dsSetStereoAuto(handle, spdifAutoMode ? 1 : 0) == dsERR_NONE) {
                    DSLOG_INFO("SPDIF0: Applied auto mode: %s", spdifAutoMode ? "TRUE" : "FALSE");
                }
            }
            
            // Set HDMI ARC port audio mode  
            handle = 0;
            if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI_ARC, 0, &handle) == dsERR_NONE) {
                if (dsSetStereoMode(handle, arcAudioMode) == dsERR_NONE) {
                    DSLOG_INFO("HDMI_ARC0: Applied audio mode: %d", arcAudioMode);
                    // dsAudio.c dsAudioMgr_init: telemetry when the persisted HDMI ARC mode is PASSTHRU.
                    if (arcAudioMode == dsAUDIO_STEREO_PASSTHRU) {
                        char telemetryValue[128] = {0};
                        snprintf(telemetryValue, sizeof(telemetryValue), "dsSetStereoMode The HDMI ARC Port Audio Settings Mode is %d", arcAudioMode);
                        TELEMETRY_EVENT_INT("SYS_INFO_PASSTHRUENABLED", 1);
                    }
                }
                if (dsSetStereoAuto(handle, arcAutoMode ? 1 : 0) == dsERR_NONE) {
                    DSLOG_INFO("HDMI_ARC0: Applied auto mode: %s", arcAutoMode ? "TRUE" : "FALSE");
                }
            }
            
            // Set SPEAKER port audio mode
            handle = 0;
            if (dsGetAudioPort(dsAUDIOPORT_TYPE_SPEAKER, 0, &handle) == dsERR_NONE) {
                if (dsSetStereoMode(handle, speakerAudioMode) == dsERR_NONE) {
                    DSLOG_INFO("SPEAKER0: Applied audio mode: %d", speakerAudioMode);
                }
                if (dsSetStereoAuto(handle, speakerAutoMode ? 1 : 0) == dsERR_NONE) {
                    DSLOG_INFO("SPEAKER0: Applied auto mode: %s", speakerAutoMode ? "TRUE" : "FALSE");
                }
            }
            
            DSLOG_INFO("Comprehensive audio port settings initialization completed successfully");
            
        } catch (...) {
            DSLOG_ERR("Exception in initializeAudioPortSettings");
        }
        EXIT_LOG;
    }
    
    // Initialize MS12 audio processing settings
    void initializeMS12Settings()
    {
        ENTRY_LOG;
        try {
            intptr_t handle = 0;
            
            // Initialize basic audio compression for all profiles
            typedef dsError_t (*dsSetAudioCompression_t)(intptr_t handle, int compressionLevel);
            dsSetAudioCompression_t dsSetAudioCompressionFunc = nullptr;
            
            dsSetAudioCompressionFunc = (dsSetAudioCompression_t) resolve(RDK_DSHAL_NAME, "dsSetAudioCompression");
            if (dsSetAudioCompressionFunc) {
                int defaultCompression = 0;
                
                // Initialize compression for SPEAKER and HDMI ports
                const dsAudioPortType_t compressionPorts[] = {dsAUDIOPORT_TYPE_SPEAKER, dsAUDIOPORT_TYPE_HDMI};
                const char* portNames[] = {"SPEAKER0", "HDMI0"};
                
                for (int i = 0; i < 2; i++) {
                    handle = 0;
                    if (dsGetAudioPort(compressionPorts[i], 0, &handle) == dsERR_NONE) {
                        if (dsSetAudioCompressionFunc(handle, defaultCompression) == dsERR_NONE) {
                            DSLOG_INFO("%s: Initialized audio compression: %d", portNames[i], defaultCompression);
                        }
                    }
                }
            }
            
            DSLOG_INFO("MS12 audio settings initialization completed");
            
        } catch (...) {
            DSLOG_ERR("Exception in initializeMS12Settings");
        }
        EXIT_LOG;
    }
    
    // audioOutPortConnectCallback implementation
    static void audioOutPortConnectCallback(dsAudioPortType_t portType, unsigned int uiPortNo, bool isPortConnected)
    {
        DSLOG_INFO("Audio port hotplug event: portType=%d, portNo=%d, connected=%s",
               portType, uiPortNo, isPortConnected ? "true" : "false");
        
        // Convert dsAudioPortType_t to AudioPortType
        AudioPortType wpePortType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER; // default
        switch (portType) {
            case dsAUDIOPORT_TYPE_ID_LR: wpePortType = AudioPortType::AUDIO_PORT_TYPE_LR; break;
            case dsAUDIOPORT_TYPE_HDMI: wpePortType = AudioPortType::AUDIO_PORT_TYPE_HDMI; break;
            case dsAUDIOPORT_TYPE_SPDIF: wpePortType = AudioPortType::AUDIO_PORT_TYPE_SPDIF; break;
            case dsAUDIOPORT_TYPE_SPEAKER: wpePortType = AudioPortType::AUDIO_PORT_TYPE_SPEAKER; break;
            case dsAUDIOPORT_TYPE_HDMI_ARC: wpePortType = AudioPortType::AUDIO_PORT_TYPE_HDMIARC; break;
            case dsAUDIOPORT_TYPE_HEADPHONE: wpePortType = AudioPortType::AUDIO_PORT_TYPE_HEADPHONE; break;
            default: break;
        }
        
        // Call Audio event handler through global callback if available
        if (g_AudioOutHotPlugCallback) {
            g_AudioOutHotPlugCallback(wpePortType, static_cast<uint32_t>(uiPortNo), isPortConnected);
        }
    }
    
    // audioFormatUpdateCallback implementation
    static void audioFormatUpdateCallback(dsAudioFormat_t audioFormat)
    {
        DSLOG_INFO("Audio format update event: audioFormat=%d", audioFormat);
        
        // Convert dsAudioFormat_t to AudioFormat
        AudioFormat wpeFormat = static_cast<AudioFormat>(audioFormat);
        
        // Call Audio event handler through global callback if available
        if (g_AudioFormatUpdateCallback) {
            g_AudioFormatUpdateCallback(wpeFormat);
        }
    }
    
    // audioAtmosCapsChangeCallback implementation  
    static void audioAtmosCapsChangeCallback(dsATMOSCapability_t atmosCaps, bool status)
    {
        DSLOG_INFO("Audio atmos caps change event: atmosCaps=%d, status=%s", atmosCaps, status ? "true" : "false");
        
        // Convert dsATMOSCapability_t to DolbyAtmosCapability
        DolbyAtmosCapability wpeAtmosCaps = static_cast<DolbyAtmosCapability>(atmosCaps);
        
        // Call Audio event handler through global callback if available
        if (g_DolbyAtmosCapabilitiesChangedCallback) {
            g_DolbyAtmosCapabilitiesChangedCallback(wpeAtmosCaps, status);
        }
    }
    
    // State Change Notification Functions using global callbacks
    // notifyAssociatedAudioMixingChanged implementation
    void notifyAssociatedAudioMixingChanged(bool mixing)
    {
        DSLOG_INFO("Associated audio mixing changed: %s", mixing ? "enabled" : "disabled");
        // Call Audio event handler using global callback if available
        if (g_AssociatedAudioMixingChangedCallback) {
            g_AssociatedAudioMixingChangedCallback(mixing);
        }
    }
    
    // notifyAudioFaderControlChanged implementation
    void notifyAudioFaderControlChanged(int32_t mixerBalance)
    {
        DSLOG_INFO("Audio fader control changed: mixerBalance=%d", mixerBalance);
        // Call Audio event handler using global callback if available
        if (g_AudioFaderControlChangedCallback) {
            g_AudioFaderControlChangedCallback(mixerBalance);
        }
    }
    
    // notifyAudioPrimaryLanguageChanged implementation
    void notifyAudioPrimaryLanguageChanged(const std::string& primaryLanguage)
    {
        DSLOG_INFO("Audio primary language changed: %s", primaryLanguage.c_str());
        // Call Audio event handler using global callback if available
        if (g_AudioPrimaryLanguageChangedCallback) {
            g_AudioPrimaryLanguageChangedCallback(primaryLanguage);
        }
    }
    
    // notifyAudioSecondaryLanguageChanged implementation
    void notifyAudioSecondaryLanguageChanged(const std::string& secondaryLanguage)
    {
        DSLOG_INFO("Audio secondary language changed: %s", secondaryLanguage.c_str());
        // Call Audio event handler using global callback if available
        if (g_AudioSecondaryLanguageChangedCallback) {
            g_AudioSecondaryLanguageChangedCallback(secondaryLanguage);
        }
    }
    
    // notifyAudioPortStateChanged implementation
    void notifyAudioPortStateChanged(AudioPortState audioPortState)
    {
        DSLOG_INFO("Audio port state changed: state=%d", static_cast<int>(audioPortState));
        // Call Audio event handler using global callback if available
        if (g_AudioPortStateChangedCallback) {
            g_AudioPortStateChangedCallback(audioPortState);
        }
    }
    
    // notifyAudioLevelChanged implementation
    void notifyAudioLevelChanged(int32_t audioLevel)
    {
        DSLOG_INFO("Audio level changed: audioLevel=%d", audioLevel);
        // Call Audio event handler using global callback if available
        if (g_AudioLevelChangedCallback) {
            g_AudioLevelChangedCallback(static_cast<float>(audioLevel));
        }
    }
    
    // notifyAudioModeChanged implementation
    void notifyAudioModeChanged(AudioPortType portType, AudioStereoMode mode)
    {
        DSLOG_INFO("Audio mode changed: portType=%d, mode=%d", static_cast<int>(portType), static_cast<int>(mode));
        // Call Audio event handler using global callback if available
        if (g_AudioModeChangedCallback) {
            g_AudioModeChangedCallback(portType, mode);
        }
    }

    // Callback management implementation following HdmiIn pattern
    void setAllCallbacks(const CallbackBundle bundle) override
    {
        ENTRY_LOG;
        
        // Register audio callbacks following HdmiIn pattern
        if (bundle.OnAudioOutHotPlug) {
            DSLOG_INFO("Audio Output Hot Plug Event Callback Registered");
            g_AudioOutHotPlugCallback = bundle.OnAudioOutHotPlug;
        }
        
        if (bundle.OnAudioFormatUpdate) {
            DSLOG_INFO("Audio Format Update Event Callback Registered");
            g_AudioFormatUpdateCallback = bundle.OnAudioFormatUpdate;
        }
        
        if (bundle.OnDolbyAtmosCapabilitiesChanged) {
            DSLOG_INFO("Dolby Atmos Capabilities Changed Event Callback Registered");
            g_DolbyAtmosCapabilitiesChangedCallback = bundle.OnDolbyAtmosCapabilitiesChanged;
        }
        
        if (bundle.OnAssociatedAudioMixingChanged) {
            DSLOG_INFO("Associated Audio Mixing Changed Event Callback Registered");
            g_AssociatedAudioMixingChangedCallback = bundle.OnAssociatedAudioMixingChanged;
        }
        
        if (bundle.OnAudioFaderControlChanged) {
            DSLOG_INFO("Audio Fader Control Changed Event Callback Registered");
            g_AudioFaderControlChangedCallback = bundle.OnAudioFaderControlChanged;
        }
        
        if (bundle.OnAudioPrimaryLanguageChanged) {
            DSLOG_INFO("Audio Primary Language Changed Event Callback Registered");
            g_AudioPrimaryLanguageChangedCallback = bundle.OnAudioPrimaryLanguageChanged;
        }
        
        if (bundle.OnAudioSecondaryLanguageChanged) {
            DSLOG_INFO("Audio Secondary Language Changed Event Callback Registered");
            g_AudioSecondaryLanguageChangedCallback = bundle.OnAudioSecondaryLanguageChanged;
        }
        
        if (bundle.OnAudioPortStateChanged) {
            DSLOG_INFO("Audio Port State Changed Event Callback Registered");
            g_AudioPortStateChangedCallback = bundle.OnAudioPortStateChanged;
        }
        
        if (bundle.OnAudioLevelChanged) {
            DSLOG_INFO("Audio Level Changed Event Callback Registered");
            g_AudioLevelChangedCallback = bundle.OnAudioLevelChanged;
        }
        
        if (bundle.OnAudioModeChanged) {
            DSLOG_INFO("Audio Mode Changed Event Callback Registered");
            g_AudioModeChangedCallback = bundle.OnAudioModeChanged;
        }
        
        DSLOG_INFO("Audio callbacks set successfully");
        EXIT_LOG;
    }

    void getPersistenceValue() override
    {
        ENTRY_LOG;
        // Initialize persistence-related values if needed
        DSLOG_INFO("Audio persistence values loaded");
        EXIT_LOG;
    }
};
