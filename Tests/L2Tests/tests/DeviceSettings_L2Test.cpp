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