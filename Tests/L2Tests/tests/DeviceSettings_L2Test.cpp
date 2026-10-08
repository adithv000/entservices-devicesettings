/*
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2026 RDK Management
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

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "L2Tests.h"
#include "L2TestsMock.h"
#include <interfaces/IDeviceSettingsFPD.h>
#include <interfaces/IDeviceSettingsHost.h>
#include <interfaces/IDeviceSettingsDisplay.h>
#include <interfaces/IDeviceSettingsCompositeIn.h>
#include <interfaces/IDeviceSettingsAudio.h>
#include <interfaces/IDeviceSettingsVideoPort.h>
#include <interfaces/IDeviceSettingsVideoDevice.h>
#include <interfaces/IDeviceSettingsHDMIIn.h>
#include <interfaces/IDeviceSettings.h>

#include <mutex>
#include <condition_variable>
#include <fstream>
#include <cstring>

#define TEST_LOG(x, ...)                                                                                                                         \
    fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); \
    fflush(stderr);

using ::testing::NiceMock;
using namespace WPEFramework;

class DeviceSettings_L2Test : public L2TestMocks {
protected:
    PluginHost::IShell* m_controller_DeviceSettings;
    Exchange::IDeviceSettings* m_deviceSettingsPlugin;

public:
    DeviceSettings_L2Test();
    ~DeviceSettings_L2Test() override;

    uint32_t CreateDeviceSettingsInterfaceObject();
};

DeviceSettings_L2Test::DeviceSettings_L2Test()
    : L2TestMocks()
    , m_controller_DeviceSettings(nullptr)
    , m_deviceSettingsPlugin(nullptr)
{
    uint32_t status = Core::ERROR_GENERAL;

    status = ActivateService("org.rdk.DeviceSettings");
    EXPECT_EQ(Core::ERROR_NONE, status);
}

DeviceSettings_L2Test::~DeviceSettings_L2Test()
{
    if (m_deviceSettingsPlugin != nullptr) {
        m_deviceSettingsPlugin->Release();
        m_deviceSettingsPlugin = nullptr;
    }

    if (m_controller_DeviceSettings != nullptr) {
        m_controller_DeviceSettings->Release();
        m_controller_DeviceSettings = nullptr;
    }

    uint32_t status = DeactivateService("org.rdk.DeviceSettings");
    EXPECT_EQ(Core::ERROR_NONE, status);
}

uint32_t DeviceSettings_L2Test::CreateDeviceSettingsInterfaceObject()
{
    uint32_t return_value = Core::ERROR_GENERAL;
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> DeviceSettings_Engine;
    Core::ProxyType<RPC::CommunicatorClient> DeviceSettings_Client;

    TEST_LOG("Creating DeviceSettings_Engine");
    DeviceSettings_Engine = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    DeviceSettings_Client = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(DeviceSettings_Engine));

    TEST_LOG("Creating DeviceSettings_Engine Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    DeviceSettings_Engine->Announcements(DeviceSettings_Client->Announcement());
#endif
    if (!DeviceSettings_Client.IsValid()) {
        TEST_LOG("Invalid DeviceSettings_Client");
    } else {
        m_controller_DeviceSettings = DeviceSettings_Client->Open<PluginHost::IShell>(_T("org.rdk.DeviceSettings"), ~0, 3000);
        if (m_controller_DeviceSettings) {
            m_deviceSettingsPlugin = m_controller_DeviceSettings->QueryInterface<Exchange::IDeviceSettings>();
            return_value = Core::ERROR_NONE;
        }
    }
    return return_value;
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_MethodTest)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);
}

// Exercises the real activated org.rdk.DeviceSettings plugin end-to-end for the FPD
// component, with only the extern "C" dsFPD.h HAL calls intercepted via p_dsFPDHalMock
// (wired up by L2TestsMock). Mirrors entservices-powermanager's PowerManager_L2Test.cpp
// HAL-mock-backed style.
TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDSetBrightness)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPBrightness(dsFPD_INDICATOR_POWER, 50))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        fpd->SetFPDBrightness(Exchange::IDeviceSettingsFPD::DS_FPD_INDICATOR_POWER, 50, false));

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDGetBrightness)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsGetFPBrightness(dsFPD_INDICATOR_POWER, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsFPDIndicator_t, dsFPDBrightness_t* brightness) {
            *brightness = 75;
            return dsERR_NONE;
        }));

    uint32_t brightness = 0;
    EXPECT_EQ(Core::ERROR_NONE,
        fpd->GetFPDBrightness(Exchange::IDeviceSettingsFPD::DS_FPD_INDICATOR_POWER, brightness, false));
    EXPECT_EQ(75u, brightness);

    fpd->Release();
}

// SetFPDState maps to dsSetFPBrightness (ON uses cached power brightness, OFF uses 0),
// not dsSetFPState - verified from dFPDImpl.h.
TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDSetAndGetState)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPBrightness(dsFPD_INDICATOR_POWER, 0))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        fpd->SetFPDState(Exchange::IDeviceSettingsFPD::DS_FPD_INDICATOR_POWER, Exchange::IDeviceSettingsFPD::DS_FPD_STATE_OFF));

    Exchange::IDeviceSettingsFPD::FPDState state = Exchange::IDeviceSettingsFPD::DS_FPD_STATE_MAX;
    EXPECT_EQ(Core::ERROR_NONE,
        fpd->GetFPDState(Exchange::IDeviceSettingsFPD::DS_FPD_INDICATOR_POWER, state));
    EXPECT_EQ(Exchange::IDeviceSettingsFPD::DS_FPD_STATE_OFF, state);

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDSetAndGetColor)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPColor(dsFPD_INDICATOR_POWER, dsFPD_COLOR_RED))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        fpd->SetFPDColor(Exchange::IDeviceSettingsFPD::DS_FPD_INDICATOR_POWER, dsFPD_COLOR_RED));

    EXPECT_CALL(*p_dsFPDHalMock, dsGetFPColor(dsFPD_INDICATOR_POWER, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsFPDIndicator_t, dsFPDColor_t* color) {
            *color = dsFPD_COLOR_RED;
            return dsERR_NONE;
        }));
    uint32_t color = 0;
    EXPECT_EQ(Core::ERROR_NONE,
        fpd->GetFPDColor(Exchange::IDeviceSettingsFPD::DS_FPD_INDICATOR_POWER, color));
    EXPECT_EQ(static_cast<uint32_t>(dsFPD_COLOR_RED), color);

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDSetBlink)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPBlink(dsFPD_INDICATOR_POWER, 500u, 10u))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        fpd->SetFPDBlink(Exchange::IDeviceSettingsFPD::DS_FPD_INDICATOR_POWER, 500, 10));

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDSetScroll)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPScroll(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, fpd->SetFPDScroll(1000, 2, 2));

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDSetTime)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPTime(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        fpd->SetFPDTime(Exchange::IDeviceSettingsFPD::DS_FPD_TIMEFORMAT_12_HOUR, 10, 30));

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDSetAndGetTimeFormat)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPTimeFormat(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, fpd->SetFPDTimeFormat(Exchange::IDeviceSettingsFPD::DS_FPD_TIMEFORMAT_24_HOUR));

    EXPECT_CALL(*p_dsFPDHalMock, dsGetFPTimeFormat(::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsFPDTimeFormat_t* fmt) {
            *fmt = static_cast<dsFPDTimeFormat_t>(Exchange::IDeviceSettingsFPD::DS_FPD_TIMEFORMAT_24_HOUR);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsFPD::FPDTimeFormat fpdFmt = Exchange::IDeviceSettingsFPD::DS_FPD_TIMEFORMAT_12_HOUR;
    EXPECT_EQ(Core::ERROR_NONE, fpd->GetFPDTimeFormat(fpdFmt));
    EXPECT_EQ(Exchange::IDeviceSettingsFPD::DS_FPD_TIMEFORMAT_24_HOUR, fpdFmt);

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDEnableClockDisplay)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsFPEnableCLockDisplay(1))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, fpd->EnableFPDClockDisplay(true));

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDSetMode)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPDMode(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, fpd->SetFPDMode(Exchange::IDeviceSettingsFPD::DS_FPD_MODE_CLOCK));

    fpd->Release();
}

// SetFPDTextBrightness/GetFPDTextBrightness are documented as deprecated and
// unimplemented in dFPDImpl.h - they always return ERROR_GENERAL, no HAL call made.
TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_FPDTextBrightnessIsUnimplemented)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsFPD* fpd = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsFPD>();
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPTextBrightness(::testing::_, ::testing::_)).Times(0);
    EXPECT_EQ(Core::ERROR_GENERAL,
        fpd->SetFPDTextBrightness(Exchange::IDeviceSettingsFPD::DS_FPD_TEXTDISPLAY_TEXT, 50));

    uint32_t brightness = 0;
    EXPECT_CALL(*p_dsFPDHalMock, dsGetFPTextBrightness(::testing::_, ::testing::_)).Times(0);
    EXPECT_EQ(Core::ERROR_GENERAL,
        fpd->GetFPDTextBrightness(Exchange::IDeviceSettingsFPD::DS_FPD_TEXTDISPLAY_TEXT, brightness));

    fpd->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_HostGetEDID)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsHost* host = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsHost>();
    ASSERT_NE(nullptr, host);

    EXPECT_CALL(*p_dsHostHalMock, dsGetHostEDID(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](unsigned char* edid, int* length) {
            edid[0] = 0xAA;
            *length = 1;
            return dsERR_NONE;
        }));

    uint8_t edIdBytes[16] = {0};
    EXPECT_EQ(Core::ERROR_NONE, host->GetEDID(edIdBytes, sizeof(edIdBytes)));
    EXPECT_EQ(0xAA, edIdBytes[0]);

    host->Release();
}

// GetMS12ConfigType reads from HostPersistence directly - no HAL call is made.
TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_HostGetMS12ConfigType)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsHost* host = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsHost>();
    ASSERT_NE(nullptr, host);

    EXPECT_CALL(*p_dsHostHalMock, dsGetCPUTemperature(::testing::_)).Times(0);

    string ms12Config;
    EXPECT_EQ(Core::ERROR_NONE, host->GetMS12ConfigType(ms12Config));

    host->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_DisplayGetDisplayAspectRatio)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsDisplay* display = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsDisplay>();
    ASSERT_NE(nullptr, display);

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetDisplay(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        display->GetDisplay(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetDisplayAspectRatio(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsVideoAspectRatio_t* aspectRatio) {
            *aspectRatio = dsVIDEO_ASPECT_RATIO_4x3;
            return dsERR_NONE;
        }));

    Exchange::IDeviceSettingsDisplay::DisplayVideoAspectRatio ratio =
        Exchange::IDeviceSettingsDisplay::DS_DISPLAY_ASPECT_RATIO_16X9;
    EXPECT_EQ(Core::ERROR_NONE, display->GetDisplayAspectRatio(handle, ratio));
    EXPECT_EQ(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_ASPECT_RATIO_4X3, ratio);

    display->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_DisplayGetDisplayEdidBytes)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsDisplay* display = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsDisplay>();
    ASSERT_NE(nullptr, display);

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetDisplay(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        display->GetDisplay(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetEDIDBytes(handle, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, unsigned char* edid, int* length) {
            edid[0] = 0x01;
            *length = 1;
            return dsERR_NONE;
        }));

    uint8_t edIdBytes[16] = {0};
    EXPECT_EQ(Core::ERROR_NONE, display->GetDisplayEdidBytes(handle, edIdBytes, sizeof(edIdBytes)));
    EXPECT_EQ(0x01, edIdBytes[0]);

    display->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_DisplaySetAllmEnabled)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsDisplay* display = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsDisplay>();
    ASSERT_NE(nullptr, display);

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetDisplay(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        display->GetDisplay(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetAllmEnabled(handle, ::testing::_))
        .WillOnce(::testing::Invoke([](intptr_t, bool* enabled) {
            *enabled = false;
            return dsERR_NONE;
        }));
    EXPECT_CALL(*p_dsDisplayHalMock, dsSetAllmEnabled(handle, true))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, display->SetAllmEnabled(handle, true));

    display->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_DisplaySetAVIContentType)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsDisplay* display = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsDisplay>();
    ASSERT_NE(nullptr, display);

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetDisplay(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        display->GetDisplay(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_PORT_TYPE_HDMI, 0, handle));

    // SetAVIContentType/SetAVIScanInformation follow the same check-then-set pattern as SetAllmEnabled.
    EXPECT_CALL(*p_dsDisplayHalMock, dsGetAVIContentType(handle, ::testing::_))
        .WillOnce(::testing::Invoke([](intptr_t, dsAviContentType_t* contentType) {
            *contentType = static_cast<dsAviContentType_t>(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_AVI_CONTENT_GRAPHICS);
            return dsERR_NONE;
        }));
    EXPECT_CALL(*p_dsDisplayHalMock, dsSetAVIContentType(handle, static_cast<dsAviContentType_t>(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_AVI_CONTENT_GAME)))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        display->SetAVIContentType(handle, Exchange::IDeviceSettingsDisplay::DS_DISPLAY_AVI_CONTENT_GAME));

    display->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_DisplaySetAVIScanInformation)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsDisplay* display = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsDisplay>();
    ASSERT_NE(nullptr, display);

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetDisplay(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        display->GetDisplay(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetAVIScanInformation(handle, ::testing::_))
        .WillOnce(::testing::Invoke([](intptr_t, dsAVIScanInformation_t* scanInfo) {
            *scanInfo = static_cast<dsAVIScanInformation_t>(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_AVI_SCAN_NO_DATA);
            return dsERR_NONE;
        }));
    EXPECT_CALL(*p_dsDisplayHalMock, dsSetAVIScanInformation(handle, static_cast<dsAVIScanInformation_t>(Exchange::IDeviceSettingsDisplay::DS_DISPLAY_AVI_SCAN_OVERSCAN)))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        display->SetAVIScanInformation(handle, Exchange::IDeviceSettingsDisplay::DS_DISPLAY_AVI_SCAN_OVERSCAN));

    display->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_CompositeInGetNrOfInputs)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsCompositeIn* compositeIn =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsCompositeIn>();
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock, dsCompositeInGetNumberOfInputs(::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](uint8_t* nrInputs) {
            *nrInputs = 2;
            return dsERR_NONE;
        }));

    int32_t nrCompositeInputs = 0;
    EXPECT_EQ(Core::ERROR_NONE, compositeIn->GetNrOfCompositeInputs(nrCompositeInputs));
    EXPECT_EQ(2, nrCompositeInputs);

    compositeIn->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_CompositeInGetStatus)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsCompositeIn* compositeIn =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsCompositeIn>();
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock, dsCompositeInGetStatus(::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsCompositeInStatus_t* status) {
            status->activePort = static_cast<dsCompositeInPort_t>(Exchange::IDeviceSettingsCompositeIn::DS_COMPOSITE_IN_PORT_0);
            status->isPresented = true;
            return dsERR_NONE;
        }));

    Exchange::IDeviceSettingsCompositeIn::CompositeInStatus status{};
    EXPECT_EQ(Core::ERROR_NONE, compositeIn->GetCompositeInStatus(status));
    EXPECT_EQ(Exchange::IDeviceSettingsCompositeIn::DS_COMPOSITE_IN_PORT_0, status.activePort);
    EXPECT_TRUE(status.isPresented);

    compositeIn->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_CompositeInSelectPort)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsCompositeIn* compositeIn =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsCompositeIn>();
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock,
        dsCompositeInSelectPort(static_cast<dsCompositeInPort_t>(Exchange::IDeviceSettingsCompositeIn::DS_COMPOSITE_IN_PORT_1)))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        compositeIn->SelectCompositeInPort(Exchange::IDeviceSettingsCompositeIn::DS_COMPOSITE_IN_PORT_1));

    compositeIn->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_CompositeInScaleVideo)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsCompositeIn* compositeIn =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsCompositeIn>();
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock, dsCompositeInScaleVideo(0, 0, 1280, 720))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    Exchange::IDeviceSettingsCompositeIn::VideoRectangle rect{};
    rect.x = 0;
    rect.y = 0;
    rect.width = 1280;
    rect.height = 720;
    EXPECT_EQ(Core::ERROR_NONE, compositeIn->ScaleCompositeInVideo(rect));

    compositeIn->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioSetMute)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioMute(handle, true))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioMute(handle, true));

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioIsAudioMuted)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsIsAudioMute(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* muted) {
            *muted = true;
            return dsERR_NONE;
        }));

    bool muted = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->IsAudioMuted(handle, muted));
    EXPECT_TRUE(muted);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioDelayOffset)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioDelayOffset(handle, 15u))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioDelayOffset(handle, 15));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioDelayOffset(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, uint32_t* offset) {
            *offset = 15;
            return dsERR_NONE;
        }));
    uint32_t delayOffset = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioDelayOffset(handle, delayOffset));
    EXPECT_EQ(15u, delayOffset);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioLevelAndGain)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioLevel(handle, 50.0f)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioLevel(handle, 50.0f));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioLevel(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, float* level) { *level = 50.0f; return dsERR_NONE; }));
    float level = 0.0f;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioLevel(handle, level));
    EXPECT_FLOAT_EQ(50.0f, level);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioGain(handle, 10.0f)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioGain(handle, 10.0f));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioGain(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, float* gain) { *gain = 10.0f; return dsERR_NONE; }));
    float gain = 0.0f;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioGain(handle, gain));
    EXPECT_FLOAT_EQ(10.0f, gain);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioFormatAndCapabilities)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioFormat(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsAudioFormat_t* format) {
            *format = static_cast<dsAudioFormat_t>(Exchange::IDeviceSettingsAudio::AUDIO_FORMAT_PCM);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsAudio::AudioFormat audioFormat = Exchange::IDeviceSettingsAudio::AUDIO_FORMAT_NONE;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioFormat(handle, audioFormat));
    EXPECT_EQ(Exchange::IDeviceSettingsAudio::AUDIO_FORMAT_PCM, audioFormat);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioCapabilities(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* capabilities) { *capabilities = 0x02; return dsERR_NONE; }));
    int32_t capabilities = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioCapabilities(handle, capabilities));
    EXPECT_EQ(0x02, capabilities);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetMS12Capabilities(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* capabilities) { *capabilities = 0x01; return dsERR_NONE; }));
    int32_t ms12Capabilities = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioMS12Capabilities(handle, ms12Capabilities));
    EXPECT_EQ(0x01, ms12Capabilities);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioStereoModeAndAuto)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    // persist=false routes straight to the HAL; persist=true reads from persistence instead (not exercised here).
    EXPECT_CALL(*p_dsAudioHalMock, dsSetStereoMode(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetStereoMode(handle, Exchange::IDeviceSettingsAudio::AUDIO_STEREO_SURROUND, false));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetStereoMode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsAudioStereoMode_t* mode) {
            *mode = static_cast<dsAudioStereoMode_t>(Exchange::IDeviceSettingsAudio::AUDIO_STEREO_SURROUND);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsAudio::StereoMode stereoMode = Exchange::IDeviceSettingsAudio::AUDIO_STEREO_UNKNOWN;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetStereoMode(handle, stereoMode, false));
    EXPECT_EQ(Exchange::IDeviceSettingsAudio::AUDIO_STEREO_SURROUND, stereoMode);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetStereoAuto(handle, 1)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetStereoAuto(handle, 1, false));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetStereoAuto(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* mode) { *mode = 1; return dsERR_NONE; }));
    int32_t stereoAutoMode = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetStereoAuto(handle, stereoAutoMode));
    EXPECT_EQ(1, stereoAutoMode);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioMixingAndFaderAndLanguage)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAssociatedAudioMixing(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAssociatedAudioMixing(handle, true));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAssociatedAudioMixing(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* mixing) { *mixing = true; return dsERR_NONE; }));
    bool mixing = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAssociatedAudioMixing(handle, mixing));
    EXPECT_TRUE(mixing);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetFaderControl(handle, 5)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioFaderControl(handle, 5));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetFaderControl(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* balance) { *balance = 5; return dsERR_NONE; }));
    int32_t balance = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioFaderControl(handle, balance));
    EXPECT_EQ(5, balance);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetPrimaryLanguage(handle, ::testing::StrEq("eng"))).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioPrimaryLanguage(handle, "eng"));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetPrimaryLanguage(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, char* lang) { strcpy(lang, "eng"); return dsERR_NONE; }));
    string primaryLang;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioPrimaryLanguage(handle, primaryLang));
    EXPECT_EQ("eng", primaryLang);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetSecondaryLanguage(handle, ::testing::StrEq("spa"))).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioSecondaryLanguage(handle, "spa"));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetSecondaryLanguage(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, char* lang) { strcpy(lang, "spa"); return dsERR_NONE; }));
    string secondaryLang;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioSecondaryLanguage(handle, secondaryLang));
    EXPECT_EQ("spa", secondaryLang);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioOutputConnectedAndAtmos)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsAudioOutIsConnected(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* connected) { *connected = true; return dsERR_NONE; }));
    bool isConnected = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->IsAudioOutputConnected(handle, isConnected));
    EXPECT_TRUE(isConnected);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetSinkDeviceAtmosCapability(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsATMOSCapability_t* capability) {
            *capability = static_cast<dsATMOSCapability_t>(Exchange::IDeviceSettingsAudio::AUDIO_DOLBY_ATMOS_METADATA);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsAudio::DolbyAtmosCapability atmosCapability = Exchange::IDeviceSettingsAudio::AUDIO_DOLBY_ATMOS_NOT_SUPPORTED;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioSinkDeviceAtmosCapability(handle, atmosCapability));
    EXPECT_EQ(Exchange::IDeviceSettingsAudio::AUDIO_DOLBY_ATMOS_METADATA, atmosCapability);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioAtmosOutputMode(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioAtmosOutputMode(handle, true));

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioPortEnableAndARC)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_HDMIARC, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsEnableAudioPort(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->EnableAudioPort(handle, true));

    EXPECT_CALL(*p_dsAudioHalMock, dsIsAudioPortEnabled(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* enabled) { *enabled = true; return dsERR_NONE; }));
    bool enabled = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->IsAudioPortEnabled(handle, enabled));
    EXPECT_TRUE(enabled);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetSupportedARCTypes(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* types) { *types = Exchange::IDeviceSettingsAudio::AUDIO_ARCTYPE_EARC; return dsERR_NONE; }));
    int32_t types = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetSupportedARCTypes(handle, types));
    EXPECT_EQ(Exchange::IDeviceSettingsAudio::AUDIO_ARCTYPE_EARC, types);

    EXPECT_CALL(*p_dsAudioHalMock, dsAudioEnableARC(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    Exchange::IDeviceSettingsAudio::AudioARCStatus arcStatus{};
    arcStatus.arcType = Exchange::IDeviceSettingsAudio::AUDIO_ARCTYPE_EARC;
    arcStatus.status = true;
    EXPECT_EQ(Core::ERROR_NONE, audio->EnableARC(handle, arcStatus));

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioDecodeStatusAndLEConfigAndDelay)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsIsAudioMSDecode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* has) { *has = true; return dsERR_NONE; }));
    bool hasMs11Decode = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->IsAudioMSDecoded(handle, hasMs11Decode));
    EXPECT_TRUE(hasMs11Decode);

    EXPECT_CALL(*p_dsAudioHalMock, dsIsAudioMS12Decode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* has) { *has = true; return dsERR_NONE; }));
    bool hasMs12Decode = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->IsAudioMS12Decoded(handle, hasMs12Decode));
    EXPECT_TRUE(hasMs12Decode);

    EXPECT_CALL(*p_dsAudioHalMock, dsEnableLEConfig(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->EnableAudioLEConfig(handle, true));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetLEConfig(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* enable) { *enable = true; return dsERR_NONE; }));
    bool leEnabled = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioLEConfig(handle, leEnabled));
    EXPECT_TRUE(leEnabled);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioDelay(handle, 20u)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioDelay(handle, 20));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioDelay(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, uint32_t* delay) { *delay = 20; return dsERR_NONE; }));
    uint32_t audioDelay = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioDelay(handle, audioDelay));
    EXPECT_EQ(20u, audioDelay);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioDRCAndDialogAndDolbyVolume)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetDialogEnhancement(handle, 5)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioDialogEnhancement(handle, 5));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetDialogEnhancement(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* level) { *level = 5; return dsERR_NONE; }));
    int32_t dialogLevel = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioDialogEnhancement(handle, dialogLevel));
    EXPECT_EQ(5, dialogLevel);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetDolbyVolumeMode(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioDolbyVolumeMode(handle, true));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetDolbyVolumeMode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* mode) { *mode = true; return dsERR_NONE; }));
    bool dolbyMode = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioDolbyVolumeMode(handle, dolbyMode));
    EXPECT_TRUE(dolbyMode);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetDRCMode(handle, 1)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioDRCMode(handle, 1));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetDRCMode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* mode) { *mode = 1; return dsERR_NONE; }));
    int32_t drcMode = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioDRCMode(handle, drcMode));
    EXPECT_EQ(1, drcMode);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioEqualizerAndSteering)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetIntelligentEqualizerMode(handle, 2)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioIntelligentEqualizerMode(handle, 2));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetIntelligentEqualizerMode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* mode) { *mode = 2; return dsERR_NONE; }));
    int32_t ieqMode = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioIntelligentEqualizerMode(handle, ieqMode));
    EXPECT_EQ(2, ieqMode);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetGraphicEqualizerMode(handle, 1)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioGraphicEqualizerMode(handle, 1));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetGraphicEqualizerMode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* mode) { *mode = 1; return dsERR_NONE; }));
    int32_t geqMode = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioGraphicEqualizerMode(handle, geqMode));
    EXPECT_EQ(1, geqMode);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetMISteering(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioMISteering(handle, true));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetMISteering(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* enable) { *enable = true; return dsERR_NONE; }));
    bool miSteering = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioMISteering(handle, miSteering));
    EXPECT_TRUE(miSteering);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioBassSurroundAndVolumeLeveller)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetBassEnhancer(handle, 10)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioBassEnhancer(handle, 10));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetBassEnhancer(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* boost) { *boost = 10; return dsERR_NONE; }));
    int32_t boost = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioBassEnhancer(handle, boost));
    EXPECT_EQ(10, boost);

    EXPECT_CALL(*p_dsAudioHalMock, dsEnableSurroundDecoder(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->EnableAudioSurroundDecoder(handle, true));

    EXPECT_CALL(*p_dsAudioHalMock, dsIsSurroundDecoderEnabled(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* enabled) { *enabled = true; return dsERR_NONE; }));
    bool surroundEnabled = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->IsAudioSurroundDecoderEnabled(handle, surroundEnabled));
    EXPECT_TRUE(surroundEnabled);

    // VolumeLeveller/SurroundVirtualizer are simple {mode, level/boost} structs; the exact HAL
    // struct layout isn't verified, so only the call itself (not its argument) is asserted.
    EXPECT_CALL(*p_dsAudioHalMock, dsSetVolumeLeveller(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    Exchange::IDeviceSettingsAudio::VolumeLeveller leveller{};
    leveller.mode = 1;
    leveller.level = 5;
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioVolumeLeveller(handle, leveller));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetVolumeLeveller(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsVolumeLeveller_t* leveller) {
            leveller->mode = 1;
            leveller->level = 5;
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsAudio::VolumeLeveller gotLeveller{};
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioVolumeLeveller(handle, gotLeveller));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetSurroundVirtualizer(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    Exchange::IDeviceSettingsAudio::SurroundVirtualizer virtualizer{};
    virtualizer.mode = 1;
    virtualizer.boost = 20;
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioSurroundVirtualizer(handle, virtualizer));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetSurroundVirtualizer(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    Exchange::IDeviceSettingsAudio::SurroundVirtualizer gotVirtualizer{};
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioSurroundVirtualizer(handle, gotVirtualizer));

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_AudioMS12ProfileAndMixerLevelsAndCompression)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsAudio* audio = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsAudio>();
    ASSERT_NE(nullptr, audio);

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsAudioPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        audio->GetAudioPort(Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, handle));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetMS12AudioProfile(handle, ::testing::StrEq("Movie"))).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioMS12Profile(handle, "Movie"));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetMS12AudioProfile(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, char* profile) { strcpy(profile, "Movie"); return dsERR_NONE; }));
    string profile;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioMS12Profile(handle, profile));
    EXPECT_EQ("Movie", profile);

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioMixerLevels(handle, ::testing::_, 50)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioMixerLevels(handle, Exchange::IDeviceSettingsAudio::AUDIO_INPUT_PRIMARY, 50));

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioCompression(handle, 2)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioCompression(handle, static_cast<int32_t>(2)));

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioCompression(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* level) { *level = 2; return dsERR_NONE; }));
    int32_t compressionLevel = 0;
    EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioCompression(handle, compressionLevel));
    EXPECT_EQ(2, compressionLevel);

    audio->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoPortEnable)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsVideoPort* videoPort =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
    ASSERT_NE(nullptr, videoPort);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->GetVideoPort(Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsEnableVideoPort(handle, true))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, videoPort->EnableVideoPort(handle, true));

    videoPort->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_IsVideoPortEnabled)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsVideoPort* videoPort =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
    ASSERT_NE(nullptr, videoPort);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->GetVideoPort(Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsIsVideoPortEnabled(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* enabled) {
            *enabled = true;
            return dsERR_NONE;
        }));

    bool enabled = false;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->IsVideoPortEnabled(handle, enabled));
    EXPECT_TRUE(enabled);

    videoPort->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoPortConnectionAndActiveState)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsVideoPort* videoPort =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
    ASSERT_NE(nullptr, videoPort);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->GetVideoPort(Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsIsDisplayConnected(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* connected) { *connected = true; return dsERR_NONE; }));
    bool connected = false;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->IsVideoPortDisplayConnected(handle, connected));
    EXPECT_TRUE(connected);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsIsVideoPortActive(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* active) { *active = true; return dsERR_NONE; }));
    bool active = false;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->IsVideoPortActive(handle, active));
    EXPECT_TRUE(active);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsIsDisplaySurround(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* surround) { *surround = true; return dsERR_NONE; }));
    bool surround = false;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->IsVideoPortDisplaySurround(handle, surround));
    EXPECT_TRUE(surround);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetSurroundMode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* surroundMode) {
            *surroundMode = Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_SURROUNDMODE_DD;
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::VideoPortSurroundMode surroundMode =
        Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_SURROUNDMODE_NONE;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetVideoPortDisplaySurroundMode(handle, surroundMode));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_SURROUNDMODE_DD, surroundMode);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetIgnoreEDIDStatus(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* status) { *status = false; return dsERR_NONE; }));
    bool ignoreEdid = true;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->getIgnoreEDIDStatus(handle, ignoreEdid));
    EXPECT_FALSE(ignoreEdid);

    videoPort->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoPortColorProperties)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsVideoPort* videoPort =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
    ASSERT_NE(nullptr, videoPort);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->GetVideoPort(Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetColorDepth(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, unsigned int* colorDepth) { *colorDepth = 0x02; return dsERR_NONE; }));
    uint32_t colorDepth = 0;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetColorDepth(handle, colorDepth));
    EXPECT_EQ(0x02u, colorDepth);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsColorDepthCapabilities(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, unsigned int* capabilities) { *capabilities = 0x03; return dsERR_NONE; }));
    uint32_t colorDepthCaps = 0;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetColorDepthCapabilities(handle, colorDepthCaps));
    EXPECT_EQ(0x03u, colorDepthCaps);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetColorSpace(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsDisplayColorSpace_t* colorSpace) {
            *colorSpace = static_cast<dsDisplayColorSpace_t>(Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_COLORSPACE_YCBCR422);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::DisplayColorSpace colorSpace =
        Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_COLORSPACE_RGB;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetColorSpace(handle, colorSpace));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_COLORSPACE_YCBCR422, colorSpace);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetQuantizationRange(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsDisplayQuantizationRange_t* range) {
            *range = static_cast<dsDisplayQuantizationRange_t>(Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_QUANTIZATIONRANGE_FULL);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::DisplayQuantizationRange quantRange =
        Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_QUANTIZATIONRANGE_UNKNOWN;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetQuantizationRange(handle, quantRange));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_QUANTIZATIONRANGE_FULL, quantRange);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetMatrixCoefficients(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsDisplayMatrixCoefficients_t* matrix) {
            *matrix = static_cast<dsDisplayMatrixCoefficients_t>(Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_MATRIXCOEFFICIENT_BT_709);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::DisplayMatrixCoefficients matrixCoefficients =
        Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetMatrixCoefficients(handle, matrixCoefficients));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_MATRIXCOEFFICIENT_BT_709, matrixCoefficients);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoEOTF(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsHDRStandard_t* eotf) {
            *eotf = static_cast<dsHDRStandard_t>(Exchange::IDeviceSettingsVideoPort::DS_HDRSTANDARD_HDR10);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::HDRStandard eotf = Exchange::IDeviceSettingsVideoPort::DS_HDRSTANDARD_NONE;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetVideoEOTF(handle, eotf));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_HDRSTANDARD_HDR10, eotf);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsSetBackgroundColor(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->SetBackgroundColor(handle, Exchange::IDeviceSettingsVideoPort::DS_VIDEO_BGCOLOR_BLACK));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsSetForceHDRMode(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->SetForceHDRMode(handle, Exchange::IDeviceSettingsVideoPort::DS_HDRSTANDARD_HDR10));

    videoPort->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoPortHDCP)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsVideoPort* videoPort =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
    ASSERT_NE(nullptr, videoPort);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->GetVideoPort(Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetHDCPStatus(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsHdcpStatus_t* status) {
            *status = static_cast<dsHdcpStatus_t>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_STATUS_AUTHENTICATED);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::HDCPStatus hdcpStatus = Exchange::IDeviceSettingsVideoPort::DS_HDCP_STATUS_UNPOWERED;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetHDCPStatusOnVideoPort(handle, hdcpStatus));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_HDCP_STATUS_AUTHENTICATED, hdcpStatus);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetHDCPProtocol(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsHdcpProtocolVersion_t* version) {
            *version = static_cast<dsHdcpProtocolVersion_t>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::HDCPProtocolVersion hdcpVersion = Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetHDCPProtocolVersionOnVideoPort(handle, hdcpVersion));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X, hdcpVersion);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetHDCPReceiverProtocol(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsHdcpProtocolVersion_t* version) {
            *version = static_cast<dsHdcpProtocolVersion_t>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::HDCPProtocolVersion hdcpRxVersion = Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetHDCPReceiverProtocolVersionOnVideoPort(handle, hdcpRxVersion));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X, hdcpRxVersion);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetHDCPCurrentProtocol(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsHdcpProtocolVersion_t* version) {
            *version = static_cast<dsHdcpProtocolVersion_t>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::HDCPProtocolVersion hdcpCurVersion = Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetHDCPCurrentProtocolVersionOnVideoPort(handle, hdcpCurVersion));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X, hdcpCurVersion);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsIsHDCPEnabled(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* enabled) { *enabled = true; return dsERR_NONE; }));
    bool hdcpEnabled = false;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->IsHDCPEnabledOnVideoPort(handle, hdcpEnabled));
    EXPECT_TRUE(hdcpEnabled);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsEnableHDCP(handle, true, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));
    uint8_t hdcpKey[4] = {0x1, 0x2, 0x3, 0x4};
    EXPECT_EQ(Core::ERROR_NONE, videoPort->EnableHDCPOnVideoPort(handle, true, hdcpKey, sizeof(hdcpKey)));

    videoPort->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoPortHDRAndForceDisable4K)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsVideoPort* videoPort =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
    ASSERT_NE(nullptr, videoPort);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->GetVideoPort(Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsSetForceDisable4KSupport(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, videoPort->SetForceDisable4K(handle, true));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetForceDisable4KSupport(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* disable) { *disable = true; return dsERR_NONE; }));
    bool disabled = false;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetForceDisable4K(handle, disabled));
    EXPECT_TRUE(disabled);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetTVHDRCapabilities(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* capabilities) { *capabilities = 0x01; return dsERR_NONE; }));
    int32_t hdrCapabilities = 0;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetTVHDRCapabilities(handle, hdrCapabilities));
    EXPECT_EQ(0x01, hdrCapabilities);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsSupportedTvResolutions(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* resolutions) { *resolutions = 0x02; return dsERR_NONE; }));
    int32_t resolutions = 0;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetTVSupportedResolutions(handle, resolutions));
    EXPECT_EQ(0x02, resolutions);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsIsOutputHDR(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, bool* isHDR) { *isHDR = true; return dsERR_NONE; }));
    bool isHDR = false;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->IsVideoPortOutputHDR(handle, isHDR));
    EXPECT_TRUE(isHDR);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsResetOutputToSDR()).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, videoPort->ResetVideoPortOutputToSDR());

    videoPort->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoPortPreferencesAndColorDepthPersist)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsVideoPort* videoPort =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoPort>();
    ASSERT_NE(nullptr, videoPort);

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](dsVideoPortType_t, int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->GetVideoPort(Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, handle));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsSetHdmiPreference(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->SetHDMIPreference(handle, Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetHdmiPreference(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsHdcpProtocolVersion_t* version) {
            *version = static_cast<dsHdcpProtocolVersion_t>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::HDCPProtocolVersion hdmiPref = Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetHDMIPreference(handle, hdmiPref));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X, hdmiPref);

    // persist=false routes straight to the HAL for preferred color depth.
    EXPECT_CALL(*p_dsVideoPortHalMock, dsSetPreferredColorDepth(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        videoPort->SetPreferredColorDepth(handle, Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_COLORDEPTH_10BIT, false));

    EXPECT_CALL(*p_dsVideoPortHalMock, dsGetPreferredColorDepth(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, dsDisplayColorDepth_t* colorDepth) {
            *colorDepth = static_cast<dsDisplayColorDepth_t>(Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_COLORDEPTH_10BIT);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsVideoPort::DisplayColorDepth preferredColorDepth =
        Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_COLORDEPTH_UNKNOWN;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->GetPreferredColorDepth(handle, preferredColorDepth, false));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoPort::DS_DISPLAY_COLORDEPTH_10BIT, preferredColorDepth);

    videoPort->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoDeviceDFC)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsVideoDevice* videoDevice =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoDevice>();
    ASSERT_NE(nullptr, videoDevice);

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetVideoDevice(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](int, intptr_t* handle) {
            *handle = 1;
            return dsERR_NONE;
        }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetVideoDeviceHandle(0, handle));

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsSetDFC(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        videoDevice->SetVideoDeviceDFC(handle, Exchange::IDeviceSettingsVideoDevice::DS_VIDEO_DEVICE_ZOOM_FULL));

    videoDevice->Release();
}

// GetVideoDeviceDFC is cache-only: it returns the value cached by the most recent
// SetVideoDeviceDFC call and never queries the HAL (dsGetDFC is never invoked).
TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoDeviceGetDFCReflectsLastSet)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsVideoDevice* videoDevice =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoDevice>();
    ASSERT_NE(nullptr, videoDevice);

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetVideoDevice(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetVideoDeviceHandle(0, handle));

    ON_CALL(*p_dsVideoDeviceHalMock, dsSetDFC(::testing::_, ::testing::_)).WillByDefault(::testing::Return(dsERR_NONE));
    ASSERT_EQ(Core::ERROR_NONE,
        videoDevice->SetVideoDeviceDFC(handle, Exchange::IDeviceSettingsVideoDevice::DS_VIDEO_DEVICE_ZOOM_FULL));

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetDFC(::testing::_, ::testing::_)).Times(0);
    Exchange::IDeviceSettingsVideoDevice::VideoZoom zoom = Exchange::IDeviceSettingsVideoDevice::DS_VIDEO_DEVICE_ZOOM_UNKNOWN;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetVideoDeviceDFC(handle, zoom));
    EXPECT_EQ(Exchange::IDeviceSettingsVideoDevice::DS_VIDEO_DEVICE_ZOOM_FULL, zoom);

    videoDevice->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoDeviceHDRAndCodecFormats)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsVideoDevice* videoDevice =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoDevice>();
    ASSERT_NE(nullptr, videoDevice);

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetVideoDevice(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetVideoDeviceHandle(0, handle));

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetHDRCapabilities(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* capabilities) { *capabilities = 0x04; return dsERR_NONE; }));
    int32_t hdrCapabilities = 0;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetHDRCapabilities(handle, hdrCapabilities));
    EXPECT_EQ(0x04, hdrCapabilities);

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetSupportedVideoCodingFormats(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, unsigned int* formats) { *formats = 0x02; return dsERR_NONE; }));
    int32_t supportedFormats = 0;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetSupportedVideoCodingFormats(handle, supportedFormats));
    EXPECT_EQ(0x02, supportedFormats);

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsForceDisableHDRSupport(handle, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->DisableHDR(handle, true));

    videoDevice->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_VideoDeviceFRFAndFrameRate)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsVideoDevice* videoDevice =
        m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsVideoDevice>();
    ASSERT_NE(nullptr, videoDevice);

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetVideoDevice(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([](int, intptr_t* handle) { *handle = 1; return dsERR_NONE; }));
    int32_t handle = -1;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetVideoDeviceHandle(0, handle));

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsSetFRFMode(handle, 1)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->SetFRFMode(handle, 1));

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetFRFMode(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, int* mode) { *mode = 1; return dsERR_NONE; }));
    int32_t frfMode = 0;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetFRFMode(handle, frfMode));
    EXPECT_EQ(1, frfMode);

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsSetDisplayframerate(handle, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->SetDisplayFrameRate(handle, "60"));

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetCurrentDisplayframerate(handle, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](intptr_t, char* framerate) { strcpy(framerate, "60"); return dsERR_NONE; }));
    string framerate;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetCurrentDisplayFrameRate(handle, framerate));
    EXPECT_EQ("60", framerate);

    videoDevice->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_HdmiInNumberOfInputs)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    ASSERT_NE(nullptr, m_deviceSettingsPlugin);

    Exchange::IDeviceSettingsHDMIIn* hdmiIn = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsHDMIIn>();
    ASSERT_NE(nullptr, hdmiIn);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInGetNumberOfInputs(::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](uint8_t* count) {
            *count = 3;
            return dsERR_NONE;
        }));

    int32_t count = 0;
    EXPECT_EQ(Core::ERROR_NONE, hdmiIn->GetHDMIInNumberOfInputs(count));
    EXPECT_EQ(3, count);

    hdmiIn->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_HdmiInSelectPortAndZoom)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsHDMIIn* hdmiIn = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsHDMIIn>();
    ASSERT_NE(nullptr, hdmiIn);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInSelectPort(::testing::_, true, ::testing::_, false))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->SelectHDMIInPort(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, true, false,
            Exchange::IDeviceSettingsHDMIIn::DS_HDMIIN_VIDEOPLANE_PRIMARY));

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInScaleVideo(0, 0, 1280, 720)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    Exchange::IDeviceSettingsHDMIIn::HDMIInVideoRectangle rect{};
    rect.x = 0;
    rect.y = 0;
    rect.width = 1280;
    rect.height = 720;
    EXPECT_EQ(Core::ERROR_NONE, hdmiIn->ScaleHDMIInVideo(rect));

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInSelectZoomMode(::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->SelectHDMIZoomMode(Exchange::IDeviceSettingsHDMIIn::DS_HDMIIN_VIDEO_ZOOM_FULL));

    hdmiIn->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_HdmiInAllmAndLatencyAndVRR)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsHDMIIn* hdmiIn = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsHDMIIn>();
    ASSERT_NE(nullptr, hdmiIn);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsGetAllmStatus(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsHdmiInPort_t, bool* status) { *status = true; return dsERR_NONE; }));
    bool allmStatus = false;
    EXPECT_EQ(Core::ERROR_NONE, hdmiIn->GetHDMIInAllmStatus(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, allmStatus));
    EXPECT_TRUE(allmStatus);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsGetAVLatency(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](int* audioLatency, int* videoLatency) {
            *audioLatency = 10;
            *videoLatency = 20;
            return dsERR_NONE;
        }));
    uint32_t videoLatency = 0, audioLatency = 0;
    EXPECT_EQ(Core::ERROR_NONE, hdmiIn->GetHDMIInAVLatency(videoLatency, audioLatency));
    EXPECT_EQ(20u, videoLatency);
    EXPECT_EQ(10u, audioLatency);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsSetEdid2AllmSupport(::testing::_, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->SetHDMIInEdid2AllmSupport(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, true));

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInSetVRRSupport(::testing::_, true)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->SetVRRSupport(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, true));

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInGetVRRSupport(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsHdmiInPort_t, bool* vrrSupport) { *vrrSupport = true; return dsERR_NONE; }));
    bool vrrSupport = false;
    EXPECT_EQ(Core::ERROR_NONE, hdmiIn->GetVRRSupport(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, vrrSupport));
    EXPECT_TRUE(vrrSupport);

    hdmiIn->Release();
}

TEST_F(DeviceSettings_L2Test, DeviceSettings_L2_HdmiInEdidAndVersion)
{
    EXPECT_EQ(Core::ERROR_NONE, CreateDeviceSettingsInterfaceObject());
    Exchange::IDeviceSettingsHDMIIn* hdmiIn = m_deviceSettingsPlugin->QueryInterface<Exchange::IDeviceSettingsHDMIIn>();
    ASSERT_NE(nullptr, hdmiIn);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsGetEDIDBytesInfo(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsHdmiInPort_t, unsigned char* edid, int* length) {
            edid[0] = 0x01;
            *length = 1;
            return dsERR_NONE;
        }));
    uint8_t edidBytes[16] = {0};
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->GetEdidBytes(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, sizeof(edidBytes), edidBytes));
    EXPECT_EQ(0x01, edidBytes[0]);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsGetHDMISPDInfo(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsHdmiInPort_t, unsigned char* data) { data[0] = 0x02; return dsERR_NONE; }));
    uint8_t spdBytes[16] = {0};
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->GetHDMISPDInformation(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, sizeof(spdBytes), spdBytes));
    EXPECT_EQ(0x02, spdBytes[0]);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsSetEdidVersion(::testing::_, ::testing::_)).Times(1).WillOnce(::testing::Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->SetHDMIEdidVersion(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, Exchange::IDeviceSettingsHDMIIn::HDMI_EDID_VER_20));

    EXPECT_CALL(*p_dsHdmiInHalMock, dsGetEdidVersion(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsHdmiInPort_t, tv_hdmi_edid_version_t* version) {
            *version = static_cast<tv_hdmi_edid_version_t>(Exchange::IDeviceSettingsHDMIIn::HDMI_EDID_VER_20);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsHDMIIn::HDMIInEdidVersion edidVersion = Exchange::IDeviceSettingsHDMIIn::HDMI_EDID_VER_14;
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->GetHDMIEdidVersion(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, edidVersion));
    EXPECT_EQ(Exchange::IDeviceSettingsHDMIIn::HDMI_EDID_VER_20, edidVersion);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsGetHdmiVersion(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsHdmiInPort_t, dsHdmiMaxCapabilityVersion_t* version) {
            *version = static_cast<dsHdmiMaxCapabilityVersion_t>(Exchange::IDeviceSettingsHDMIIn::HDMI_COMPATIBILITY_VERSION_21);
            return dsERR_NONE;
        }));
    Exchange::IDeviceSettingsHDMIIn::HDMIInCapabilityVersion capabilityVersion =
        Exchange::IDeviceSettingsHDMIIn::HDMI_COMPATIBILITY_VERSION_14;
    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->GetHDMIVersion(Exchange::IDeviceSettingsHDMIIn::DS_HDMI_IN_PORT_0, capabilityVersion));
    EXPECT_EQ(Exchange::IDeviceSettingsHDMIIn::HDMI_COMPATIBILITY_VERSION_21, capabilityVersion);

    hdmiIn->Release();
}