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
#include "devicesettings/DsHostHALMock.h"

using namespace WPEFramework;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;

// Host is the simplest DS component: only GetEDID (real HAL call) and
// GetMS12ConfigType (persistence-only, no HAL call at all) are exposed.
class DeviceSettingsHostTest : public ::testing::Test {
protected:
    NiceMock<DsHostHalMock>* p_dsHostHalMock = nullptr;
    Core::ProxyType<Plugin::DeviceSettingsImp> implementation;
    Exchange::IDeviceSettingsHost* host = nullptr;

    DeviceSettingsHostTest()
    {
        p_dsHostHalMock = new NiceMock<DsHostHalMock>;
        DsHostApi::setImpl(p_dsHostHalMock);
        ON_CALL(*p_dsHostHalMock, dsHostInit()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsHostHalMock, dsHostTerm()).WillByDefault(Return(dsERR_NONE));

        implementation = Core::ProxyType<Plugin::DeviceSettingsImp>::Create();
        host = static_cast<Exchange::IDeviceSettingsHost*>(
            implementation->QueryInterface(Exchange::IDeviceSettingsHost::ID));
    }

    ~DeviceSettingsHostTest() override
    {
        if (host != nullptr) {
            host->Release();
            host = nullptr;
        }
        implementation.Release();

        DsHostApi::setImpl(nullptr);
        delete p_dsHostHalMock;
        p_dsHostHalMock = nullptr;
    }
};

TEST_F(DeviceSettingsHostTest, GetEDIDReadsHalBuffer)
{
    ASSERT_NE(nullptr, host);

    EXPECT_CALL(*p_dsHostHalMock, dsGetHostEDID(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Invoke([](unsigned char* edid, int* length) {
            edid[0] = 0x00;
            edid[1] = 0xFF;
            *length = 2;
            return dsERR_NONE;
        }));

    uint8_t edIdBytes[16] = {0};
    EXPECT_EQ(Core::ERROR_NONE, host->GetEDID(edIdBytes, sizeof(edIdBytes)));
    EXPECT_EQ(0x00, edIdBytes[0]);
    EXPECT_EQ(0xFF, edIdBytes[1]);
}

TEST_F(DeviceSettingsHostTest, GetEDIDHalFailure)
{
    ASSERT_NE(nullptr, host);

    EXPECT_CALL(*p_dsHostHalMock, dsGetHostEDID(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(dsERR_GENERAL));

    uint8_t edIdBytes[16] = {0};
    EXPECT_EQ(Core::ERROR_GENERAL, host->GetEDID(edIdBytes, sizeof(edIdBytes)));
}

// GetMS12ConfigType reads from HostPersistence, never touches the HAL mock.
TEST_F(DeviceSettingsHostTest, GetMS12ConfigTypeDoesNotCallHal)
{
    ASSERT_NE(nullptr, host);

    EXPECT_CALL(*p_dsHostHalMock, dsGetCPUTemperature(::testing::_)).Times(0);

    string ms12Config;
    EXPECT_EQ(Core::ERROR_NONE, host->GetMS12ConfigType(ms12Config));
}
