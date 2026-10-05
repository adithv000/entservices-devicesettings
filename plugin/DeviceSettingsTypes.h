/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
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
**/

#pragma once

#include <functional>
#include <map>
#include <iostream>
#include <fstream>
#include <exception>
#include <string>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unistd.h>

// RDK profile search - inlined from UtilsSearchRDKProfile
#define RDK_PROFILE "RDK_PROFILE="
#define PROFILE_TV "TV"
#define PROFILE_STB "STB"

typedef enum profile {
    NOT_FOUND = -1,
    STB = 0,
    TV,
    MAX
} profile_t;

extern profile_t profileType;

inline profile_t searchRdkProfile(void) {
    const char* devPropPath = "/etc/device.properties";
    char line[256], *rdkProfile = NULL;
    profile_t ret = NOT_FOUND;
    FILE* file;

    file = fopen(devPropPath, "r");
    if (file == NULL) {
        printf("File not found issue \n");
        return NOT_FOUND;
    }

    while (fgets(line, sizeof(line), file)) {
        rdkProfile = strstr(line, RDK_PROFILE);
        if (rdkProfile != NULL) {
            rdkProfile += strlen(RDK_PROFILE);
            printf("Found RDK_PROFILE: %s \n", rdkProfile);
            break;
        }
    }

    if (rdkProfile != NULL) {
        if (strncmp(rdkProfile, PROFILE_TV, strlen(PROFILE_TV)) == 0) {
            ret = TV;
        } else if (strncmp(rdkProfile, PROFILE_STB, strlen(PROFILE_STB)) == 0) {
            ret = STB;
        }
    } else {
        printf("Found RDK_PROFILE: NOT_FOUND \n");
        ret = NOT_FOUND;
    }
    fclose(file);
    return ret;
}
#include <sys/stat.h>
#include <sys/types.h>
#include <interfaces/IDeviceSettings.h>
#include <interfaces/IDeviceSettingsAudio.h>
#include <interfaces/IDeviceSettingsCompositeIn.h>
#include <interfaces/IDeviceSettingsDisplay.h>
#include <interfaces/IDeviceSettingsFPD.h>
#include <interfaces/IDeviceSettingsHDMIIn.h>
#include <interfaces/IDeviceSettingsHost.h>
#include <interfaces/IDeviceSettingsVideoDevice.h>
#include <interfaces/IDeviceSettingsVideoPort.h>
#include "DeviceSettingsLogger.h"

#define USE_LEGACY_INTERFACE

#ifdef USE_LEGACY_INTERFACE
using DeviceSetting               = WPEFramework::Exchange::IDeviceSettings;
using DeviceSettingsFPD            = WPEFramework::Exchange::IDeviceSettingsFPD;
using DeviceSettingsHDMIIn         = WPEFramework::Exchange::IDeviceSettingsHDMIIn;
using DeviceSettingsCompositeIn    = WPEFramework::Exchange::IDeviceSettingsCompositeIn;
using DeviceSettingsAudio          = WPEFramework::Exchange::IDeviceSettingsAudio;
using DeviceSettingsVideoDevice    = WPEFramework::Exchange::IDeviceSettingsVideoDevice;
using DeviceSettingsDisplay        = WPEFramework::Exchange::IDeviceSettingsDisplay;
using DeviceSettingsHost           = WPEFramework::Exchange::IDeviceSettingsHost;
using DeviceSettingsVideoPort      = WPEFramework::Exchange::IDeviceSettingsVideoPort;
#else
using DeviceSettingsManagerFPD            = WPEFramework::Exchange::IDeviceSettingsManager::IFPD;
using DeviceSettingsManagerHDMIIn         = WPEFramework::Exchange::IDeviceSettingsManager::IHDMIIn;
using DeviceSettingsManagerCompositeIn    = WPEFramework::Exchange::IDeviceSettingsManager::ICompositeIn;
using DeviceSettingsManagerAudio          = WPEFramework::Exchange::IDeviceSettingsManager::IAudio;
using DeviceSettingsManagerVideoDevice    = WPEFramework::Exchange::IDeviceSettingsManager::IVideoDevice;
using DeviceSettingsManagerDisplay        = WPEFramework::Exchange::IDeviceSettingsManager::IDisplay;
using DeviceSettingsManagerHost           = WPEFramework::Exchange::IDeviceSettingsManager::IHost;
using DeviceSettingsManagerVideoPort      = WPEFramework::Exchange::IDeviceSettingsManager::IVideoPort;
#endif

// HDMI In type aliases for convenience
using HDMIInPort               = DeviceSettingsHDMIIn::HDMIInPort;
using HDMIInSignalStatus       = DeviceSettingsHDMIIn::HDMIInSignalStatus;
using HDMIVideoPortResolution  = DeviceSettingsHDMIIn::HDMIVideoPortResolution;
using HDMIInAviContentType     = DeviceSettingsHDMIIn::HDMIInAviContentType;
using HDMIInVRRType            = DeviceSettingsHDMIIn::HDMIInVRRType;
using HDMIInStatus             = DeviceSettingsHDMIIn::HDMIInStatus;
using HDMIVideoPlaneType       = DeviceSettingsHDMIIn::HDMIVideoPlaneType;
using HDMIInVRRStatus          = DeviceSettingsHDMIIn::HDMIInVRRStatus;
using HDMIInCapabilityVersion  = DeviceSettingsHDMIIn::HDMIInCapabilityVersion;
using HDMIInEdidVersion        = DeviceSettingsHDMIIn::HDMIInEdidVersion;
using HDMIInVideoZoom          = DeviceSettingsHDMIIn::HDMIInVideoZoom;
using HDMIInVideoRectangle     = DeviceSettingsHDMIIn::HDMIInVideoRectangle;
using HDMIVideoAspectRatio        = DeviceSettingsHDMIIn::HDMIVideoAspectRatio;
using HDMIInVideoStereoScopicMode = DeviceSettingsHDMIIn::HDMIInVideoStereoScopicMode;
using HDMIInVideoFrameRate        = DeviceSettingsHDMIIn::HDMIInVideoFrameRate;
using HDMIInVideoResolution       = DeviceSettingsHDMIIn::HDMIInVideoResolution;
using HDMIInTVResolution       = DeviceSettingsHDMIIn::HDMIInTVResolution;
using IHDMIInPortConnectionStatusIterator = DeviceSettingsHDMIIn::IHDMIInPortConnectionStatusIterator;
using IHDMIInGameFeatureListIterator      = DeviceSettingsHDMIIn::IHDMIInGameFeatureListIterator;
//using GameFeatureListIteratorImpl = WPEFramework::Core::Service<WPEFramework::RPC::IIteratorType<IHDMIInGameFeatureListIterator>>;

// FPD type aliases for convenience
using FPDTimeFormat = DeviceSettingsFPD::FPDTimeFormat;
using FPDIndicator = DeviceSettingsFPD::FPDIndicator;
using FPDState = DeviceSettingsFPD::FPDState;
using FPDTextDisplay = DeviceSettingsFPD::FPDTextDisplay;
using FPDMode = DeviceSettingsFPD::FPDMode;
using FPDLEDState = DeviceSettingsFPD::FPDLEDState;
using FPDColorConfig = DeviceSetting::FPDColorConfig;
using FPDIndicatorConfig = DeviceSetting::FPDIndicatorConfig;
using FPDColorBinding = DeviceSetting::FPDColorBinding;
using FPDTextDisplayConfig = DeviceSetting::FPDTextDisplayConfig;

// Audio type aliases for convenience
using AudioPortType = DeviceSettingsAudio::AudioPortType;
using AudioPortState = DeviceSettingsAudio::AudioPortState;
using AudioFormat = DeviceSettingsAudio::AudioFormat;
using AudioEncoding = DeviceSettingsAudio::AudioEncoding;
using AudioStereoMode = DeviceSettingsAudio::StereoMode;
using AudioDuckingType = DeviceSettingsAudio::AudioDuckingType;
using AudioDuckingAction = DeviceSettingsAudio::AudioDuckingAction;
using DolbyAtmosCapability = DeviceSettingsAudio::DolbyAtmosCapability;
using AudioCompression = DeviceSettingsAudio::AudioCompression;
using AudioCapabilities = DeviceSettingsAudio::AudioCapabilities;
using AudioARCType = DeviceSettingsAudio::AudioARCType;
using AudioInput = DeviceSettingsAudio::AudioInput;
using MS12Capabilities = DeviceSettingsAudio::MS12Capabilities;
using MS12AudioProfile = DeviceSettingsAudio::MS12AudioProfile;
using VolumeLeveller = DeviceSettingsAudio::VolumeLeveller;
using SurroundVirtualizer = DeviceSettingsAudio::SurroundVirtualizer;
using SurroundMode = DeviceSettingsAudio::SurroundMode;
using MS12Feature = DeviceSettingsAudio::MS12Feature;
using AudioMS12ProfileState = DeviceSettingsAudio::MS12ProfileState;
using AudioARCStatus = DeviceSettingsAudio::AudioARCStatus;
using AudioTypeConfigInfo = DeviceSetting::AudioTypeConfigInfo;
using AudioPortConfigInfo = DeviceSettingsAudio::AudioPortConfigInfo;
using IDeviceSettingsAudioEncodingIterator = DeviceSettingsAudio::IDeviceSettingsAudioEncodingIterator;
using IDeviceSettingsAudioCompressionIterator = DeviceSettingsAudio::IDeviceSettingsAudioCompressionIterator;
using IDeviceSettingsStereoModeIterator = DeviceSettingsAudio::IDeviceSettingsStereoModeIterator;
using IDeviceSettingsAudioMS12AudioProfileIterator = DeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator;

// VideoPort type aliases for convenience
using VideoPortType = DeviceSettingsVideoPort::VideoPort;
using VideoPortResolution = DeviceSettingsVideoPort::VideoPortResolution;
using VideoResolution = DeviceSettingsVideoPort::VideoResolution;
using VideoAspectRatio = DeviceSettingsVideoPort::VideoAspectRatio;
using VideoStereoScopicMode = DeviceSettingsVideoPort::VideoStereoScopicMode;
using VideoFrameRate = DeviceSettingsVideoPort::VideoFrameRate;
using VideoPortColorSpace = DeviceSettingsVideoPort::DisplayColorSpace;
using VideoPortQuantizationRange = DeviceSettingsVideoPort::DisplayQuantizationRange;
using VideoPortHdcpStatus = DeviceSettingsVideoPort::HDCPStatus;
using VideoPortHdcpProtocolVersion = DeviceSettingsVideoPort::HDCPProtocolVersion;
using HDRStandard = DeviceSettingsVideoPort::HDRStandard;
using ResolutionChange = DeviceSettingsVideoPort::ResolutionChange;
using DisplayMatrixCoefficients = DeviceSettingsVideoPort::DisplayMatrixCoefficients;
using DSOutputSettings = DeviceSettingsVideoPort::DSOutputSettings;
using VideoBackgroundColor = DeviceSettingsVideoPort::VideoBackgroundColor;
using DisplayColorDepth = DeviceSettingsVideoPort::DisplayColorDepth;
using TVResolution = DeviceSettingsVideoPort::TVResolution;
using VideoPortSurroundMode = DeviceSettingsVideoPort::VideoPortSurroundMode;
using VideoScanMode = DeviceSettingsVideoPort::VideoScanMode;
using VideoPortTypeConfig = DeviceSettingsVideoPort::VideoPortTypeConfig;
using VideoPortPortConfig = DeviceSettingsVideoPort::VideoPortPortConfig;
using IVideoPortResolutionIterator = DeviceSettingsVideoPort::IVideoPortResolutionIterator;

// Display type aliases for convenience
using DisplayEvent = DeviceSettingsDisplay::DisplayEvent;
using DisplayTVResolution = DeviceSettingsDisplay::DisplayTVResolution;
using DisplayVideoAspectRatio = DeviceSettingsDisplay::DisplayVideoAspectRatio;
using DisplayInVideoStereoScopicMode = DeviceSettingsDisplay::DisplayInVideoStereoScopicMode;
using DisplayInVideoFrameRate = DeviceSettingsDisplay::DisplayInVideoFrameRate;
using DisplayPortType = DeviceSettingsDisplay::DisplayPortType;
using DisplayAVIContentType = DeviceSettingsDisplay::DisplayAVIContentType;
using DisplayAVIScanInformation = DeviceSettingsDisplay::DisplayAVIScanInformation;
using DisplayVideoPortResolution = DeviceSettingsDisplay::DisplayVideoPortResolution;
using DisplayEDID = DeviceSettingsDisplay::DisplayEDID;
using IDSVideoPortResolutionIterator = DeviceSettingsDisplay::IDSVideoPortResolutionIterator;
using IDisplayNotification = DeviceSettingsDisplay::INotification;
using IDisplayHDMIHotPlugNotification = DeviceSettingsDisplay::IDisplayHDMIHotPlugNotification;

// CompositeIn type aliases for convenience
using CompositeInPort = DeviceSettingsCompositeIn::CompositeInPort;
using CompositeInSignalStatus = DeviceSettingsCompositeIn::CompositeInSignalStatus;
using CompositeInStatus = DeviceSettingsCompositeIn::CompositeInStatus;
using CompositeInVideoRectangle = DeviceSettingsCompositeIn::VideoRectangle;

// VideoDevice type aliases for convenience
using VideoDeviceZoom = DeviceSettingsVideoDevice::VideoZoom;
using VideoDeviceCodec = DeviceSettingsVideoDevice::VideoCodec;
using VideoDeviceCodecHEVCProfile = DeviceSettingsVideoDevice::VideoCodecHEVCProfile;
using VideoDeviceCodecProfileSupport = DeviceSettingsVideoDevice::VideoCodecProfileSupport;
using VideoDeviceConfigInfo = DeviceSettingsVideoDevice::VideoDeviceConfigInfo;
using IDeviceSettingsVideoCodecProfileSupportIterator = DeviceSettingsVideoDevice::IDeviceSettingsVideoCodecProfileSupportIterator;

// Legacy DSMGR/RPC compatibility definitions used by DSController and DSPwrEventListener.
#ifndef DSMGR_MAX_VIDEO_PORT_NAME_LENGTH
#define DSMGR_MAX_VIDEO_PORT_NAME_LENGTH 16
#endif

#ifndef PWRMGR_MAX_REBOOT_REASON_LENGTH
#define PWRMGR_MAX_REBOOT_REASON_LENGTH 100
#endif

#ifndef IARM_BUS_DSMGR_PLUGIN_NAME
#define IARM_BUS_DSMGR_PLUGIN_NAME "DSMgr_Plugin"
#endif

typedef enum _DSMgr_EventId_t {
    IARM_BUS_DSMGR_EVENT_RES_PRECHANGE = 0,
    IARM_BUS_DSMGR_EVENT_RES_POSTCHANGE,
    IARM_BUS_DSMGR_EVENT_ZOOM_SETTINGS,
    IARM_BUS_DSMGR_EVENT_HDMI_HOTPLUG,
    IARM_BUS_DSMGR_EVENT_AUDIO_MODE,
    IARM_BUS_DSMGR_EVENT_HDCP_STATUS,
    IARM_BUS_DSMGR_EVENT_RX_SENSE,
    IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG,
    IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS,
    IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS,
    IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE,
    IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS,
    IARM_BUS_DSMGR_EVENT_HDMI_IN_VRR_STATUS,
    IARM_BUS_DSMGR_EVENT_COMPOSITE_IN_HOTPLUG,
    IARM_BUS_DSMGR_EVENT_COMPOSITE_IN_SIGNAL_STATUS,
    IARM_BUS_DSMGR_EVENT_COMPOSITE_IN_STATUS,
    IARM_BUS_DSMGR_EVENT_COMPOSITE_IN_VIDEO_MODE_UPDATE,
    IARM_BUS_DSMGR_EVENT_TIME_FORMAT_CHANGE,
    IARM_BUS_DSMGR_EVENT_AUDIO_LEVEL_CHANGED,
    IARM_BUS_DSMGR_EVENT_AUDIO_OUT_HOTPLUG,
    IARM_BUS_DSMGR_EVENT_AUDIO_FORMAT_UPDATE,
    IARM_BUS_DSMGR_EVENT_AUDIO_PRIMARY_LANGUAGE_CHANGED,
    IARM_BUS_DSMGR_EVENT_AUDIO_SECONDARY_LANGUAGE_CHANGED,
    IARM_BUS_DSMGR_EVENT_AUDIO_FADER_CONTROL_CHANGED,
    IARM_BUS_DSMGR_EVENT_AUDIO_ASSOCIATED_AUDIO_MIXING_CHANGED,
    IARM_BUS_DSMGR_EVENT_VIDEO_FORMAT_UPDATE,
    IARM_BUS_DSMGR_EVENT_DISPLAY_FRAMRATE_PRECHANGE,
    IARM_BUS_DSMGR_EVENT_DISPLAY_FRAMRATE_POSTCHANGE,
    IARM_BUS_DSMGR_EVENT_AUDIO_PORT_STATE,
    IARM_BUS_DSMGR_EVENT_SLEEP_MODE_CHANGED,
    IARM_BUS_DSMGR_EVENT_HDMI_IN_AVI_CONTENT_TYPE,
    IARM_BUS_DSMGR_EVENT_HDMI_IN_AV_LATENCY,
    IARM_BUS_DSMGR_EVENT_ATMOS_CAPS_CHANGED,
    IARM_BUS_DSMGR_EVENT_MAX,
} IARM_Bus_DSMgr_EventId_t;

typedef struct _DSMgr_EventData_t {
    union {
        struct {
            int event;
        } hdmi_hpd;
        struct {
            int hdcpStatus;
        } hdmi_hdcp;
    } data;
} IARM_Bus_DSMgr_EventData_t;

typedef struct _dsMgrStandbyVideoStateParam_t {
    char port[DSMGR_MAX_VIDEO_PORT_NAME_LENGTH];
    int isEnabled;
    int result;
} dsMgrStandbyVideoStateParam_t;

typedef struct _dsMgrRebootConfigParam_t {
    char reboot_reason_custom[PWRMGR_MAX_REBOOT_REASON_LENGTH];
    int powerState;
    int result;
} dsMgrRebootConfigParam_t;

typedef struct _dsMgrAVPortStateParam_t {
    int avPortPowerState;
    int result;
} dsMgrAVPortStateParam_t;

typedef struct _dsMgrLEDStatusParam_t {
    int ledState;
    int result;
} dsMgrLEDStatusParam_t;

typedef struct _dsEdidIgnoreParam_t {
    intptr_t handle;
    bool ignoreEDID;
} dsEdidIgnoreParam_t;

// Plugin-wide exception logging helpers.
// Use these instead of catching device::Exception from lib32-devicesettings.
namespace WPEFramework {
namespace Plugin {
namespace DeviceSettingsExceptionHelper {
    inline void LogException(const char* context, const std::exception& e)
    {
        DSLOG_ERR("%s: %s", context, e.what());
    }

    inline void LogUnknownException(const char* context)
    {
        DSLOG_ERR("%s: unknown exception", context);
    }
} // namespace DeviceSettingsExceptionHelper
} // namespace Plugin
} // namespace WPEFramework

// Common constants
#define API_VERSION_MAJOR 1
#define API_VERSION_MINOR 0
#define API_VERSION_PATCH 0

#define TVSETTINGS_DALS_RFC_PARAM "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.TvSettings.DynamicAutoLatency"

#ifndef RDK_DSHAL_NAME
#define RDK_DSHAL_NAME "libds-hal.so.0"
#endif

#ifdef DEBUG_LOGGING
#define ENTRY_LOG do { DSLOG_INFO("%d: Enter %s", __LINE__, __func__); } while(0);
#define EXIT_LOG do { DSLOG_INFO("%d: Exit %s", __LINE__, __func__); } while(0);
#else
#define ENTRY_LOG do { } while(0)
#define EXIT_LOG do { } while(0)
#endif

#ifdef DEBUG_LOGGING
#define DEBUG_LOG(fmt, ...) DSLOG_INFO(fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) do { } while(0)
#endif

namespace WPEFramework {
namespace Plugin {
    namespace DeviceSettingsHALLoader {
        extern void* gLibraryHandle;
        extern std::mutex gLibraryLock;

        void* ResolveSymbol(const std::string& libName, const std::string& symbolName);
        void ReleaseAllLibraries();
    }
}
}

// Exact replica of original HostPersistence implementation to avoid DS_LIBRARIES dependency
namespace device {
    class HostPersistence {
    private:
        std::map<std::string, std::string> _properties;
        std::map<std::string, std::string> _defaultProperties;
        std::string filePath;
        std::string defaultFilePath;
        bool _isInitialized = false;

        void ensureInitialized() {
            if (!_isInitialized) {
                DSLOG_INFO("RDK_DSHAL_NAME: %s",RDK_DSHAL_NAME);
                load();
                _isInitialized = true;
            }
        }

        void loadFromFile(const std::string &fileName, std::map<std::string, std::string> &map) {
            char keyValue[1024] = "";
            char key[1024] = "";
            FILE *filePtr = NULL;

            filePtr = fopen(fileName.c_str(), "r");
            if (filePtr != NULL) {
                while (fscanf(filePtr, "%1023s\t%1023s", key, keyValue) == 2) {
                    map.insert({key, keyValue});
                }
                fclose(filePtr);
            } else {
                // File doesn't exist - this is okay for initial startup
            }
        }

        void writeToFile(const std::string &fileName) {
            unlink(fileName.c_str());

            if (_properties.size() > 0) {
                /*
                 * Replacing the ofstream to fwrite
                 * Because the ofstream.close or ofstream.flush or ofstream.rdbuf->sync
                 * does not sync the data onto disk.
                 * TBD - This need to be changed to C++ APIs in future.
                 */

                FILE *file = fopen(fileName.c_str(), "w");
                if (file != NULL) {
                    for (auto it = _properties.begin(); it != _properties.end(); ++it) {
                        std::string dataToWrite = it->first + "\t" + it->second + "\n";
                        unsigned int size = dataToWrite.length();
                        size_t written = fwrite(dataToWrite.c_str(), 1, size, file);
                        if (written != size) {
                            DSLOG_ERR("HostPersistence write failed for key %s", it->first.c_str());
                            break;
                        }
                    }

                    fflush(file);           // Flush buffers to FS
                    fsync(fileno(file));    // Flush file to HDD
                    fclose(file);
                }
            }
        }

    public:
        HostPersistence() {
            /*
             * TBD This need to be removed and 
             * Persistent path shall be set from startup script
             * To do this Host Persistent need to be part of DS Manager
             * TBD
             */

            #if defined(HAS_HDD_PERSISTENT)
                /*Product having HDD Persistent*/
                filePath = "/tmp/mnt/diska3/persistent/ds/hostData";
            #elif defined(HAS_FLASH_PERSISTENT)
                /*Product having Flash Persistent*/
                filePath = "/opt/persistent/ds/hostData";
            #else
                /*Product having Flash Persistent*/
                filePath = "/opt/persistent/ds/hostData";
            #endif
            defaultFilePath = "/etc/hostDataDefault";
            // _isInitialized remains false — load() will be called lazily on first access
        }

        HostPersistence(const std::string &storeFileName) {
            filePath = storeFileName;
            defaultFilePath = "/etc/hostDataDefault";
            // _isInitialized remains false — load() will be called lazily on first access
        }

        virtual ~HostPersistence() {
            // Auto-generated destructor stub
        }

        static HostPersistence& getInstance() {
            static HostPersistence instance;
            return instance;
        }

        void load() {
            DSLOG_INFO("loading user data from '%s'", filePath.c_str());
            DSLOG_INFO("loading default data from '%s'", defaultFilePath.c_str());
            try {
                loadFromFile(filePath, _properties);
                DSLOG_INFO("loaded %zu user properties from '%s'", _properties.size(), filePath.c_str());
            } catch (...) {
                // Backup file is corrupt or not available
                DSLOG_WARN("'%s' not available, trying backup '%stmpDB'", filePath.c_str(), filePath.c_str());
                try {
                    loadFromFile(filePath + "tmpDB", _properties);
                    DSLOG_INFO("loaded %zu user properties from backup '%stmpDB'", _properties.size(), filePath.c_str());
                } catch (...) {
                    DSLOG_WARN("backup also not available, starting with empty user properties");
                    /* Remove all properties, and start with default values */
                }
            }

            try {
                loadFromFile(defaultFilePath, _defaultProperties);
                DSLOG_INFO("loaded %zu default properties from '%s'", _defaultProperties.size(), defaultFilePath.c_str());
            } catch (...) {
                DSLOG_WARN("'%s' not available, default properties will be empty", defaultFilePath.c_str());
                // System file is corrupt or not available
            }
        }

        std::string getProperty(const std::string &key) {
            /* Ensure data is loaded before accessing properties */
            ensureInitialized();

            /* Check the validness of the key */
            if (key.empty()) {
                throw std::invalid_argument("The KEY is empty");
            }

            DSLOG_INFO("key='%s' from '%s'", key.c_str(), filePath.c_str());
            std::map<std::string, std::string>::const_iterator eFound = _properties.find(key);
            if (eFound == _properties.end()) {
                DSLOG_WARN("key='%s' NOT FOUND in '%s'", key.c_str(), filePath.c_str());
                throw std::invalid_argument("The Item IS NOT FOUND");
            } else {
                DSLOG_INFO("key='%s' value='%s' (from '%s')", key.c_str(), eFound->second.c_str(), filePath.c_str());
                return eFound->second;
            }
        }

        std::string getProperty(const std::string &key, const std::string &defValue) {
            /* Ensure data is loaded before accessing properties */
            ensureInitialized();

            /* Check the validness of the key */
            if (key.empty()) {
                throw std::invalid_argument("The KEY is empty");
            }

            DSLOG_INFO("key='%s' from '%s'", key.c_str(), filePath.c_str());
            std::map<std::string, std::string>::const_iterator eFound = _properties.find(key);
            if (eFound == _properties.end()) {
                DSLOG_INFO("key='%s' NOT FOUND, returning default='%s'", key.c_str(), defValue.c_str());
                return defValue;
            } else {
                DSLOG_INFO("key='%s' value='%s' " "(from '%s')", key.c_str(), eFound->second.c_str(), filePath.c_str());
                return eFound->second;
            }
        }

        std::string getDefaultProperty(const std::string &key) {
            /* Ensure data is loaded before accessing properties */
            ensureInitialized();

            /* Check the validness of the key */
            if (key.empty()) {
                throw std::invalid_argument("The KEY is empty");
            }

            DSLOG_INFO("key='%s' from '%s'", key.c_str(), defaultFilePath.c_str());
            std::map<std::string, std::string>::const_iterator eFound = _defaultProperties.find(key);
            if (eFound == _defaultProperties.end()) {
                DSLOG_WARN("key='%s' NOT FOUND in '%s'", key.c_str(), defaultFilePath.c_str());
                throw std::invalid_argument("The Item IS NOT FOUND");
            } else {
                DSLOG_INFO("key='%s' value='%s' (from '%s')", key.c_str(), eFound->second.c_str(), defaultFilePath.c_str());
                return eFound->second;
            }
        }

        void persistHostProperty(const std::string &key, const std::string &value) {
            /* Ensure data is loaded before accessing properties */
            ensureInitialized();

            if (key.empty() || value.empty()) {
                throw std::invalid_argument("Given KEY or VALUE is empty");
            }

            DSLOG_INFO("key='%s' value='%s' to '%s'", key.c_str(), value.c_str(), filePath.c_str());

            try {
                std::string eRet = getProperty(key);

                if (eRet.compare(value) == 0) {
                    /* Same value. No need to do anything */
                    DSLOG_INFO("key='%s' value unchanged, skip write", key.c_str());
                    return;
                }

                /* Save a current copy before modifying */
                writeToFile(filePath + "tmpDB");

                /* First of all check whether the entry is already present in the hashtable */
                _properties.erase(key);

            } catch (const std::invalid_argument &e) {
                // Entry Not found
            } catch (...) {
                // Other exceptions
            }

            _properties.insert({key, value});
            writeToFile(filePath);
            DSLOG_INFO("key='%s' value='%s' written to '%s'", key.c_str(), value.c_str(), filePath.c_str());
        }
    };
}

struct CallbackBundle {
    // HDMIIn callbacks
    std::function<void(HDMIInPort, bool)> OnHDMIInHotPlugEvent;
    std::function<void(HDMIInPort, HDMIInSignalStatus)> OnHDMIInSignalStatusEvent;
    std::function<void(HDMIInPort, bool)> OnHDMIInStatusEvent;
    std::function<void(HDMIInPort, HDMIVideoPortResolution)> OnHDMIInVideoModeUpdateEvent;
    std::function<void(HDMIInPort, bool)> OnHDMIInAllmStatusEvent;
    std::function<void(HDMIInPort, HDMIInAviContentType)> OnHDMIInAVIContentTypeEvent;
    std::function<void(int32_t, int32_t)> OnHDMIInAVLatencyEvent;
    std::function<void(HDMIInPort, HDMIInVRRType)> OnHDMIInVRRStatusEvent;
    
    // VideoPort callbacks
    std::function<void(const ResolutionChange)> OnResolutionPreChange;
    std::function<void(const ResolutionChange)> OnResolutionPostChange;
    std::function<void(const VideoPortHdcpStatus)> OnHDCPStatusChange;
    std::function<void(const HDRStandard)> OnVideoFormatUpdate;

    // Display event callbacks (for HAL implementations)
    std::function<void(const uint8_t, const bool)> OnDisplayRxSense;
    std::function<void(const uint8_t, const int32_t)> OnDisplayHDCPStatus;
    std::function<void(const uint8_t, const bool)> OnDisplayHDMIHotPlug;
    
    // CompositeIn callbacks  
    std::function<void(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort, const bool)> OnCompositeInHotPlug;
    std::function<void(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort, const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus)> OnCompositeInSignalStatus;
    std::function<void(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort, const bool)> OnCompositeInStatus;
    std::function<void(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort, const WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution)> OnCompositeInVideoModeUpdate;
    
    // CompositeIn event callbacks (for HAL implementations)
    std::function<void(const CompositeInPort, const bool)> CompositeInHotPlugEventCallback;
    std::function<void(const CompositeInPort, const CompositeInSignalStatus)> CompositeInSignalStatusEventCallback;
    std::function<void(const CompositeInPort, const bool)> CompositeInStatusEventCallback;
    std::function<void(const CompositeInPort, const DisplayVideoPortResolution)> CompositeInVideoModeUpdateEventCallback;
    
    // VideoDevice callbacks
    std::function<void(const VideoDeviceZoom)> OnZoomSettingsChanged;
    std::function<void(const std::string&)> OnDisplayFrameratePreChange;
    std::function<void(const std::string&)> OnDisplayFrameratePostChange;

    // Audio callbacks
    std::function<void(AudioPortType, uint32_t, bool)> OnAudioOutHotPlug;
    std::function<void(AudioFormat)> OnAudioFormatUpdate;
    std::function<void(DolbyAtmosCapability, bool)> OnDolbyAtmosCapabilitiesChanged;
    std::function<void(bool)> OnAssociatedAudioMixingChanged;
    std::function<void(int32_t)> OnAudioFaderControlChanged;
    std::function<void(const std::string&)> OnAudioPrimaryLanguageChanged;
    std::function<void(const std::string&)> OnAudioSecondaryLanguageChanged;
    std::function<void(AudioPortState)> OnAudioPortStateChanged;
    std::function<void(float)> OnAudioLevelChanged;
    std::function<void(AudioPortType, AudioStereoMode)> OnAudioModeChanged;
    // Add other callbacks as needed
};
