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
#include <cstdio>
#include <mutex>
#include <unistd.h>
#include "dFPD.h"
#include "dsHdmiIn.h"
#include "dsError.h"
#include "dsHdmiInTypes.h"
#include "dsUtl.h"
#include "dsTypes.h"
#include "dsFPD.h"
#include "dsFPDTypes.h"

#include <WPEFramework/interfaces/IDeviceSettingsFPD.h>
#include "DeviceSettingsTypes.h"

static int fpd_isInitialized = 0;
static int fpd_isPlatInitialized = 0;
static std::mutex fpd_initMutex;

/** Structure that defines internal data base for the FP */
typedef struct _dsFPDSettings_t_
{   
    dsFPDBrightness_t brightness;
    dsFPDState_t state;
    dsFPDColor_t color;
}_FPDSettings_t;

static _FPDSettings_t srvFPDSettings[dsFPD_INDICATOR_MAX];

#ifndef dsFPD_BRIGHTNESS_DEFAULT
#define dsFPD_BRIGHTNESS_DEFAULT dsFPD_BRIGHTNESS_MAX
#endif

static dsFPDBrightness_t _dsPowerBrightness = dsFPD_BRIGHTNESS_MAX;
static dsFPDBrightness_t _dsTextBrightness  = dsFPD_BRIGHTNESS_MAX;
static dsFPDColor_t      _dsPowerLedColor   = dsFPD_COLOR_BLUE;

class dFPDImpl : public hal::dFPD::IPlatform {

    // delete copy constructor and assignment operator
    dFPDImpl(const dFPDImpl&) = delete;
    dFPDImpl& operator=(const dFPDImpl&) = delete;

public:
    dFPDImpl()
    {
        DSLOG_INFO("Constructor");
        InitialiseHAL();
    }

    virtual ~dFPDImpl()
    {
        DSLOG_INFO("Destructor");
        DeInitialiseHAL();
    }

    void InitialiseHAL()
    {
        if (!fpd_isInitialized) {
            for (int i = dsFPD_INDICATOR_MESSAGE; i < dsFPD_INDICATOR_MAX; i++)
            {
                srvFPDSettings[i].brightness = dsFPD_BRIGHTNESS_MAX;
                srvFPDSettings[i].state = dsFPD_STATE_OFF;
                            srvFPDSettings[i].color = dsFPD_COLOR_BLUE;
            }

            fpd_isInitialized = 1;

        }
        // HAL dsFPInit() is deferred to first use via EnsurePlatInit()
    }

    void DeInitialiseHAL()
    {
        std::lock_guard<std::mutex> lock(fpd_initMutex);
        if (fpd_isPlatInitialized)
        {
            dsFPTerm();
            fpd_isPlatInitialized = 0;
        }
        fpd_isInitialized = 0;
    }

    // Mirrors FrontPanelConfig::getInstance(): retry dsFPInit() up to 20 times on first HAL use.
    bool EnsurePlatInit()
    {
        std::lock_guard<std::mutex> lock(fpd_initMutex);
        if (fpd_isPlatInitialized)
            return true;

        dsError_t errorCode = dsERR_NONE;
        unsigned int retryCount = 1;
        do {
            errorCode = dsFPInit();
            if (dsERR_NONE == errorCode) {
                fpd_isPlatInitialized = 1;
                DSLOG_INFO(" dsFPInit succeeded");
            } else {
                DSLOG_ERR(" dsFPInit failed with error[%d]. Retrying... (%d/20)", errorCode, retryCount);
                usleep(50000);
            }
        } while ((!fpd_isPlatInitialized) && (retryCount++ < 20));

        if (!fpd_isPlatInitialized) {
            DSLOG_ERR(" dsFPInit failed after 20 retries");
            return false;
        }

        try {
            int maxBrightness = dsFPD_BRIGHTNESS_DEFAULT;
            std::string value;

            try {
                value = device::HostPersistence::getInstance().getProperty("Power.brightness");
            } catch (...) {
                value = std::to_string(maxBrightness);
                device::HostPersistence::getInstance().persistHostProperty("Power.brightness", value);
            }
            _dsPowerBrightness = static_cast<dsFPDBrightness_t>(atoi(value.c_str()));

            try {
                value = device::HostPersistence::getInstance().getProperty("Text.brightness");
            } catch (...) {
                value = std::to_string(maxBrightness);
                device::HostPersistence::getInstance().persistHostProperty("Text.brightness", value);
            }
            _dsTextBrightness = static_cast<dsFPDBrightness_t>(atoi(value.c_str()));

#if (dsFPD_BRIGHTNESS_DEFAULT != dsFPD_BRIGHTNESS_MAX)
            if (_dsPowerBrightness == dsFPD_BRIGHTNESS_MAX)
                _dsPowerBrightness = dsFPD_BRIGHTNESS_DEFAULT;
            if (_dsTextBrightness == dsFPD_BRIGHTNESS_MAX)
                _dsTextBrightness = dsFPD_BRIGHTNESS_DEFAULT;
#endif

            std::string colorStr;
            try {
                colorStr = device::HostPersistence::getInstance().getProperty("Power.Color");
            } catch (...) {
                colorStr = "BLUE";
            }
            if      (colorStr == "GREEN")  _dsPowerLedColor = dsFPD_COLOR_GREEN;
            else if (colorStr == "RED")    _dsPowerLedColor = dsFPD_COLOR_RED;
            else if (colorStr == "YELLOW") _dsPowerLedColor = dsFPD_COLOR_YELLOW;
            else if (colorStr == "ORANGE") _dsPowerLedColor = dsFPD_COLOR_ORANGE;
            else                           _dsPowerLedColor = dsFPD_COLOR_BLUE;

            DSLOG_INFO(" Power.brightness=%d Text.brightness=%d Power.Color=%s",
                    _dsPowerBrightness, _dsTextBrightness, colorStr.c_str());
        } catch (...) {
            DSLOG_ERR(" Error reading FPD persistence, using defaults");
        }
        return true;
    }

    // Implementation of all FPD Platform interface methods
    uint32_t SetFPDTime(const FPDTimeFormat timeFormat, const uint32_t minutes, const uint32_t seconds) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        DSLOG_ERR("is DEPRECATED and not IMPLEMENTED");

        return retCode;
    }

    uint32_t SetFPDScroll(const uint32_t scrollHoldDuration, const uint32_t nHorizontalScrollIterations, const uint32_t nVerticalScrollIterations) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        DSLOG_ERR("is DEPRECATED and not IMPLEMENTED");

        return retCode;
    }

    uint32_t SetFPDBlink(const FPDIndicator indicator, const uint32_t blinkDuration, const uint32_t blinkIterations) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        DSLOG_ERR("is DEPRECATED and not IMPLEMENTED");
        
        return retCode;
    }

    uint32_t SetFPDBrightness(const FPDIndicator indicator, const uint32_t brightNess, const bool persist) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" indicator %d, brightNess %d, persist %d", static_cast<int>(indicator), brightNess, persist);
        if (!EnsurePlatInit()) {
            DSLOG_ERR(" FPD HAL not initialised");
            return retCode;
        }
        
        if (static_cast<int>(indicator) < dsFPD_INDICATOR_MAX && brightNess <= dsFPD_BRIGHTNESS_MAX) {
            dsError_t eError = dsSetFPBrightness(static_cast<dsFPDIndicator_t>(indicator), static_cast<dsFPDBrightness_t>(brightNess));
            DSLOG_INFO(" dsSetFPBrightness returned %d", eError);
            if (eError == dsERR_NONE) {
                srvFPDSettings[static_cast<int>(indicator)].brightness = brightNess;
                
                // Update global power brightness when POWER indicator is set
                if (static_cast<int>(indicator) == dsFPD_INDICATOR_POWER) {
                    DSLOG_INFO(" Power Brightness From App is %d", brightNess);
                    if (persist) {
                        _dsPowerBrightness = brightNess;
                        /* Mirror dsFPD.c _dsSetFPBrightness: persist Power.brightness */
                        try {
                            device::HostPersistence::getInstance().persistHostProperty(
                                "Power.brightness", std::to_string(_dsPowerBrightness));
                            DSLOG_INFO(" Persisted Power.brightness=%d", _dsPowerBrightness);
                        } catch (...) {
                            DSLOG_ERR(" Error persisting Power.brightness");
                        }
                    }
                }
                
                retCode = WPEFramework::Core::ERROR_NONE;
            } else {
                DSLOG_ERR(" dsSetFPBrightness failed with error %d", eError);
            }
        } else {
            DSLOG_ERR(" Invalid parameters - indicator %d, brightness %d", static_cast<int>(indicator), brightNess);
        }
        return retCode;
    }

    uint32_t GetFPDBrightness(const FPDIndicator indicator, uint32_t &brightNess, const bool persist) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" indicator %d, persist=%s", static_cast<int>(indicator), persist ? "true" : "false");
        if (!EnsurePlatInit()) {
            DSLOG_ERR(" FPD HAL not initialised");
            return retCode;
        }
        
        if (static_cast<int>(indicator) < dsFPD_INDICATOR_MAX) {
            dsFPDBrightness_t halBrightness = 0;
            dsGetFPBrightness(static_cast<dsFPDIndicator_t>(indicator), &halBrightness);

            brightNess = persist ? static_cast<uint32_t>(_dsPowerBrightness)
                                 : static_cast<uint32_t>(halBrightness);
            DSLOG_INFO(" indicator %d brightness %d (hal=%d _dsPowerBrightness=%d)",
                    static_cast<int>(indicator), brightNess,
                    static_cast<int>(halBrightness), static_cast<int>(_dsPowerBrightness));
            retCode = WPEFramework::Core::ERROR_NONE;
        } else {
            DSLOG_ERR(" Invalid indicator %d", static_cast<int>(indicator));
        }
        return retCode;
    }

    uint32_t SetFPDState(const FPDIndicator indicator, const FPDState state) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" indicator %d, state %d", static_cast<int>(indicator), static_cast<int>(state));
        if (!EnsurePlatInit()) {
            DSLOG_ERR(" FPD HAL not initialised");
            return retCode;
        }
        
        if (static_cast<int>(indicator) < dsFPD_INDICATOR_MAX) {
            dsError_t eError = dsERR_NONE;
            
            // Match RPC layer approach - use dsSetFPBrightness based on state
            if (state == FPDState::DS_FPD_STATE_ON) {
                // Power LED Indicator Brightness is the Global LED brightness for all indicators
                eError = dsSetFPBrightness(static_cast<dsFPDIndicator_t>(indicator), _dsPowerBrightness);
                if (static_cast<int>(indicator) == dsFPD_INDICATOR_POWER) {
                    DSLOG_INFO(" Setting Power LED to ON with Brightness %d", _dsPowerBrightness);
                }
            } else if (state == FPDState::DS_FPD_STATE_OFF) {
                eError = dsSetFPBrightness(static_cast<dsFPDIndicator_t>(indicator), 0);
                if (static_cast<int>(indicator) == dsFPD_INDICATOR_POWER) {
                    DSLOG_INFO(" Setting Power LED to OFF with Brightness 0");
                }
            }
            
            DSLOG_INFO(" dsSetFPBrightness returned %d", eError);
            if (eError == dsERR_NONE) {
                srvFPDSettings[static_cast<int>(indicator)].state = static_cast<dsFPDState_t>(state);
                retCode = WPEFramework::Core::ERROR_NONE;
            } else {
                DSLOG_ERR(" dsSetFPBrightness failed with error %d", eError);
            }
        } else {
            DSLOG_ERR(" Invalid indicator %d", static_cast<int>(indicator));
        }
        return retCode;
    }

    uint32_t GetFPDState(const FPDIndicator indicator, FPDState &state) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" indicator %d", static_cast<int>(indicator));
        if (!EnsurePlatInit()) {
            DSLOG_ERR(" FPD HAL not initialised");
            return retCode;
        }
        
        if (static_cast<int>(indicator) < dsFPD_INDICATOR_MAX) {
            // Match RPC layer approach - read from internal cache instead of hardware call
            state = static_cast<FPDState>(srvFPDSettings[static_cast<int>(indicator)].state);
            DSLOG_INFO(" indicator %d state %d (from cache)", static_cast<int>(indicator), static_cast<int>(state));
            retCode = WPEFramework::Core::ERROR_NONE;
        } else {
            DSLOG_ERR(" Invalid indicator %d", static_cast<int>(indicator));
        }
        return retCode;
    }

    uint32_t GetFPDColor(const FPDIndicator indicator, uint32_t &color) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" indicator %d", static_cast<int>(indicator));
        if (!EnsurePlatInit()) {
            DSLOG_ERR(" FPD HAL not initialised");
            return retCode;
        }
        
        if (static_cast<int>(indicator) < dsFPD_INDICATOR_MAX) {
            dsFPDColor_t halColor = 0;
            dsError_t eError = dsGetFPColor(static_cast<dsFPDIndicator_t>(indicator), &halColor);
            DSLOG_INFO(" dsGetFPColor returned %d", eError);
            if (eError == dsERR_NONE) {
                color = static_cast<uint32_t>(halColor);
                srvFPDSettings[static_cast<int>(indicator)].color = halColor;
                DSLOG_INFO(" indicator %d color %d", static_cast<int>(indicator), color);
                retCode = WPEFramework::Core::ERROR_NONE;
            } else {
                DSLOG_ERR(" dsGetFPColor failed with error %d", eError);
                // Fallback to cached value
                color = srvFPDSettings[static_cast<int>(indicator)].color;
                DSLOG_INFO(" indicator %d color %d (cached)", static_cast<int>(indicator), color);
                retCode = WPEFramework::Core::ERROR_NONE;
            }
        } else {
            DSLOG_ERR(" Invalid indicator %d", static_cast<int>(indicator));
        }
        return retCode;
    }

    uint32_t SetFPDColor(const FPDIndicator indicator, const uint32_t color) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" indicator %d, color %d", static_cast<int>(indicator), color);
        if (!EnsurePlatInit()) {
            DSLOG_ERR(" FPD HAL not initialised");
            return retCode;
        }
        
        if (static_cast<int>(indicator) < dsFPD_INDICATOR_MAX && dsFPDColor_isValid(color)) {
            dsError_t eError = dsSetFPColor(static_cast<dsFPDIndicator_t>(indicator), static_cast<dsFPDColor_t>(color));
            DSLOG_INFO(" dsSetFPColor returned %d", eError);
            if (eError == dsERR_NONE) {
                /* Mask to 24-bit RGB — mirrors _dsSetFPColor in dsFPD.c */
                uint32_t maskedColor = color & 0x00FFFFFF;
                srvFPDSettings[static_cast<int>(indicator)].color = static_cast<dsFPDColor_t>(maskedColor);

                /* Persist Power.Color for POWER indicator
                 * Mirrors dsFPD.c _dsSetFPColor + enumToColor helper. */
                if (static_cast<int>(indicator) == dsFPD_INDICATOR_POWER) {
                    std::lock_guard<std::mutex> lock(fpd_initMutex);
                    _dsPowerLedColor = static_cast<dsFPDColor_t>(maskedColor);
                    try {
                        const char* colorStr = "BLUE";
                        switch (_dsPowerLedColor) {
                            case dsFPD_COLOR_GREEN:  colorStr = "GREEN";  break;
                            case dsFPD_COLOR_RED:    colorStr = "RED";    break;
                            case dsFPD_COLOR_YELLOW: colorStr = "YELLOW"; break;
                            case dsFPD_COLOR_ORANGE: colorStr = "RED";    break; // dsFPD.c enumToColor maps ORANGE→RED
                            default: break;
                        }
                        device::HostPersistence::getInstance().persistHostProperty("Power.Color", colorStr);
                        DSLOG_INFO(" Persisted Power.Color=%s", colorStr);
                    } catch (...) {
                        DSLOG_ERR(" Error persisting Power.Color");
                    }
                }
                retCode = WPEFramework::Core::ERROR_NONE;
            } else {
                DSLOG_ERR(" dsSetFPColor failed with error %d", eError);
            }
        } else {
            DSLOG_ERR(" Invalid parameters - indicator %d, color 0x%x", static_cast<int>(indicator), color);
        }
        return retCode;
    }

    uint32_t SetFPDTextBrightness(const FPDTextDisplay textDisplay, const uint32_t brightNess) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        DSLOG_ERR("is DEPRECATED and not IMPLEMENTED");

        return retCode;
    }

    uint32_t GetFPDTextBrightness(const FPDTextDisplay textDisplay, uint32_t &brightNess) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        DSLOG_ERR("is DEPRECATED and not IMPLEMENTED");

        return retCode;
    }

    uint32_t EnableFPDClockDisplay(const bool enable) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        DSLOG_ERR("is DEPRECATED and not IMPLEMENTED");

        return retCode;
    }

    uint32_t GetFPDTimeFormat(FPDTimeFormat &fpdTimeFormat) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        DSLOG_ERR("is DEPRECATED and not IMPLEMENTED");

        return retCode;
    }

    uint32_t SetFPDTimeFormat(const FPDTimeFormat fpdTimeFormat) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        DSLOG_ERR("is DEPRECATED and not IMPLEMENTED");

        return retCode;
    }

    uint32_t SetFPDMode(const FPDMode fpdMode) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        DSLOG_INFO(" fpdMode %d", static_cast<int>(fpdMode));
        
        dsError_t eError = dsSetFPDMode(static_cast<dsFPDMode_t>(fpdMode));
        DSLOG_INFO(" dsSetFPDMode returned %d", eError);
        if (eError == dsERR_NONE) {
            retCode = WPEFramework::Core::ERROR_NONE;
        } else {
            DSLOG_ERR(" dsSetFPDMode failed with error %d", eError);
        }
        return retCode;
    }

    private:
};
