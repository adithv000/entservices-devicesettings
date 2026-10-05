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
#include "devicesettings/DsVideoPortHALMock.h"

using namespace WPEFramework;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;

using VideoPortType = Exchange::IDeviceSettingsVideoPort::VideoPort;

// dVideoPortImpl initializes its HAL eagerly in the constructor (dsVideoPortInit).
class DeviceSettingsVideoPortTest : public ::testing::Test {
protected:
    NiceMock<DsVideoPortHalMock>* p_dsVideoPortHalMock = nullptr;
    Core::ProxyType<Plugin::DeviceSettingsImp> implementation;
    Exchange::IDeviceSettingsVideoPort* videoPort = nullptr;

    DeviceSettingsVideoPortTest()
    {
        p_dsVideoPortHalMock = new NiceMock<DsVideoPortHalMock>;
        DsVideoPortApi::setImpl(p_dsVideoPortHalMock);
        ON_CALL(*p_dsVideoPortHalMock, dsVideoPortInit()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsVideoPortHalMock, dsVideoPortTerm()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsVideoPortHalMock, dsIsDisplayConnected(::testing::_, ::testing::_))
            .WillByDefault(Invoke([](intptr_t, bool* connected) {
                *connected = true;
                return dsERR_NONE;
            }));

        implementation = Core::ProxyType<Plugin::DeviceSettingsImp>::Create();
        videoPort = static_cast<Exchange::IDeviceSettingsVideoPort*>(
            implementation->QueryInterface(Exchange::IDeviceSettingsVideoPort::ID));
    }

    ~DeviceSettingsVideoPortTest() override
    {
        if (videoPort != nullptr) {
            videoPort->Release();
            videoPort = nullptr;
        }
        implementation.Release();

        DsVideoPortApi::setImpl(nullptr);
        delete p_dsVideoPortHalMock;
        p_dsVideoPortHalMock = nullptr;
    }

    int32_t GetHandle()
    {
        EXPECT_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
            .WillOnce(Invoke([](dsVideoPortType_t, int, intptr_t* handle) {
                *handle = 1;
                return dsERR_NONE;
            }));
        int32_t handle = -1;
        EXPECT_EQ(Core::ERROR_NONE, videoPort->GetVideoPort(VideoPortType::DS_VIDEO_PORT_TYPE_HDMI, 0, handle));
        return handle;
    }
};

TEST_F(DeviceSettingsVideoPortTest, GetVideoPortReturnsHandle)
{
    ASSERT_NE(nullptr, videoPort);
    EXPECT_EQ(1, GetHandle());
}

TEST_F(DeviceSettingsVideoPortTest, IsVideoPortEnabledReadsHalValue)
{
    ASSERT_NE(nullptr, videoPort);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsVideoPortHalMock, dsIsVideoPortEnabled(handle, ::testing::_))
        .Times(1)
        .WillOnce(Invoke([](intptr_t, bool* enabled) {
            *enabled = true;
            return dsERR_NONE;
        }));

    bool enabled = false;
    EXPECT_EQ(Core::ERROR_NONE, videoPort->IsVideoPortEnabled(handle, enabled));
    EXPECT_TRUE(enabled);
}

TEST_F(DeviceSettingsVideoPortTest, EnableVideoPortSuccess)
{
    ASSERT_NE(nullptr, videoPort);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsVideoPortHalMock, dsEnableVideoPort(handle, true))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, videoPort->EnableVideoPort(handle, true));
}

TEST_F(DeviceSettingsVideoPortTest, EnableVideoPortHalFailure)
{
    ASSERT_NE(nullptr, videoPort);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsVideoPortHalMock, dsEnableVideoPort(handle, ::testing::_))
        .Times(1)
        .WillOnce(Return(dsERR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, videoPort->EnableVideoPort(handle, true));
}
