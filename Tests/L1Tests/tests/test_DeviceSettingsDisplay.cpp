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

#include "DeviceSettingsImplementation.h"
#include "devicesettings/DsDisplayHALMock.h"

using namespace WPEFramework;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;

using DisplayPortType = Exchange::IDeviceSettingsDisplay::DisplayPortType;
using DisplayVideoAspectRatio = Exchange::IDeviceSettingsDisplay::DisplayVideoAspectRatio;
using DisplayAVIContentType = Exchange::IDeviceSettingsDisplay::DisplayAVIContentType;
using DisplayAVIScanInformation = Exchange::IDeviceSettingsDisplay::DisplayAVIScanInformation;

class DeviceSettingsDisplayTest : public ::testing::Test {
protected:
    NiceMock<DsDisplayHalMock>* p_dsDisplayHalMock = nullptr;
    Core::ProxyType<Plugin::DeviceSettingsImp> implementation;
    Exchange::IDeviceSettingsDisplay* display = nullptr;

    DeviceSettingsDisplayTest()
    {
        p_dsDisplayHalMock = new NiceMock<DsDisplayHalMock>;
        DsDisplayApi::setImpl(p_dsDisplayHalMock);
        ON_CALL(*p_dsDisplayHalMock, dsDisplayInit()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsDisplayHalMock, dsDisplayTerm()).WillByDefault(Return(dsERR_NONE));

        implementation = Core::ProxyType<Plugin::DeviceSettingsImp>::Create();
        display = static_cast<Exchange::IDeviceSettingsDisplay*>(
            implementation->QueryInterface(Exchange::IDeviceSettingsDisplay::ID));
    }

    ~DeviceSettingsDisplayTest() override
    {
        if (display != nullptr) {
            display->Release();
            display = nullptr;
        }
        implementation.Release();

        DsDisplayApi::setImpl(nullptr);
        delete p_dsDisplayHalMock;
        p_dsDisplayHalMock = nullptr;
    }

    int32_t GetHandle()
    {
        EXPECT_CALL(*p_dsDisplayHalMock, dsGetDisplay(::testing::_, ::testing::_, ::testing::_))
            .WillOnce(Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
                *handle = 1;
                return dsERR_NONE;
            }));
        int32_t handle = -1;
        EXPECT_EQ(Core::ERROR_NONE,
            display->GetDisplay(DisplayPortType::DS_DISPLAY_PORT_TYPE_HDMI, 0, handle));
        return handle;
    }
};

TEST_F(DeviceSettingsDisplayTest, GetDisplayReturnsHandle)
{
    ASSERT_NE(nullptr, display);
    EXPECT_EQ(1, GetHandle());
}

TEST_F(DeviceSettingsDisplayTest, GetDisplayAspectRatio)
{
    ASSERT_NE(nullptr, display);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetDisplayAspectRatio(handle, ::testing::_))
        .Times(1)
        .WillOnce(Invoke([](intptr_t, dsVideoAspectRatio_t* aspectRatio) {
            *aspectRatio = dsVIDEO_ASPECT_RATIO_4x3;
            return dsERR_NONE;
        }));

    DisplayVideoAspectRatio ratio = DisplayVideoAspectRatio::DS_DISPLAY_ASPECT_RATIO_16X9;
    EXPECT_EQ(Core::ERROR_NONE, display->GetDisplayAspectRatio(handle, ratio));
    EXPECT_EQ(DisplayVideoAspectRatio::DS_DISPLAY_ASPECT_RATIO_4X3, ratio);
}

// SetAllmEnabled follows a check-then-set pattern: dsSetAllmEnabled is only called
// when the requested state differs from the HAL-reported current state.
TEST_F(DeviceSettingsDisplayTest, SetAllmEnabledSkipsHalSetWhenAlreadyInDesiredState)
{
    ASSERT_NE(nullptr, display);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetAllmEnabled(handle, ::testing::_))
        .WillOnce(Invoke([](intptr_t, bool* enabled) {
            *enabled = true;
            return dsERR_NONE;
        }));
    EXPECT_CALL(*p_dsDisplayHalMock, dsSetAllmEnabled(::testing::_, ::testing::_)).Times(0);

    EXPECT_EQ(Core::ERROR_NONE, display->SetAllmEnabled(handle, true));
}

TEST_F(DeviceSettingsDisplayTest, SetAllmEnabledCallsHalSetWhenStateDiffers)
{
    ASSERT_NE(nullptr, display);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetAllmEnabled(handle, ::testing::_))
        .WillOnce(Invoke([](intptr_t, bool* enabled) {
            *enabled = false;
            return dsERR_NONE;
        }));
    EXPECT_CALL(*p_dsDisplayHalMock, dsSetAllmEnabled(handle, true))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, display->SetAllmEnabled(handle, true));
}

TEST_F(DeviceSettingsDisplayTest, GetDisplayEdidBytes)
{
    ASSERT_NE(nullptr, display);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetEDIDBytes(handle, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Invoke([](intptr_t, unsigned char* edid, int* length) {
            edid[0] = 0x01;
            *length = 1;
            return dsERR_NONE;
        }));

    uint8_t edIdBytes[16] = {0};
    EXPECT_EQ(Core::ERROR_NONE, display->GetDisplayEdidBytes(handle, edIdBytes, sizeof(edIdBytes)));
    EXPECT_EQ(0x01, edIdBytes[0]);
}

TEST_F(DeviceSettingsDisplayTest, SetAVIContentType)
{
    ASSERT_NE(nullptr, display);
    int32_t handle = GetHandle();

    // Check-then-set pattern, same as SetAllmEnabled.
    EXPECT_CALL(*p_dsDisplayHalMock, dsGetAVIContentType(handle, ::testing::_))
        .WillOnce(Invoke([](intptr_t, dsAviContentType_t* contentType) {
            *contentType = static_cast<dsAviContentType_t>(DisplayAVIContentType::DS_DISPLAY_AVI_CONTENT_GRAPHICS);
            return dsERR_NONE;
        }));
    EXPECT_CALL(*p_dsDisplayHalMock,
        dsSetAVIContentType(handle, static_cast<dsAviContentType_t>(DisplayAVIContentType::DS_DISPLAY_AVI_CONTENT_GAME)))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, display->SetAVIContentType(handle, DisplayAVIContentType::DS_DISPLAY_AVI_CONTENT_GAME));
}

TEST_F(DeviceSettingsDisplayTest, SetAVIScanInformation)
{
    ASSERT_NE(nullptr, display);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsDisplayHalMock, dsGetAVIScanInformation(handle, ::testing::_))
        .WillOnce(Invoke([](intptr_t, dsAVIScanInformation_t* scanInfo) {
            *scanInfo = static_cast<dsAVIScanInformation_t>(DisplayAVIScanInformation::DS_DISPLAY_AVI_SCAN_NO_DATA);
            return dsERR_NONE;
        }));
    EXPECT_CALL(*p_dsDisplayHalMock,
        dsSetAVIScanInformation(handle, static_cast<dsAVIScanInformation_t>(DisplayAVIScanInformation::DS_DISPLAY_AVI_SCAN_OVERSCAN)))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        display->SetAVIScanInformation(handle, DisplayAVIScanInformation::DS_DISPLAY_AVI_SCAN_OVERSCAN));
}
