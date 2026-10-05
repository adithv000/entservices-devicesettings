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
#include "devicesettings/DsHdmiInHALMock.h"

using namespace WPEFramework;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;

using HDMIInPort = Exchange::IDeviceSettingsHDMIIn::HDMIInPort;
using HDMIVideoPlaneType = Exchange::IDeviceSettingsHDMIIn::HDMIVideoPlaneType;

// NOTE: dHdmiInImpl::InitialiseHAL() only calls dsHdmiInInit() on TV profiles
// (searchRdkProfile() reads /etc/device.properties, which is empty in the CI
// test image, so profileType is NOT_FOUND there). Per-call methods below still
// resolve their HAL symbols independently of that guard, so they route to this
// mock regardless of profile.
class DeviceSettingsHdmiInTest : public ::testing::Test {
protected:
    NiceMock<DsHdmiInHalMock>* p_dsHdmiInHalMock = nullptr;
    Core::ProxyType<Plugin::DeviceSettingsImp> implementation;
    Exchange::IDeviceSettingsHDMIIn* hdmiIn = nullptr;

    DeviceSettingsHdmiInTest()
    {
        p_dsHdmiInHalMock = new NiceMock<DsHdmiInHalMock>;
        DsHdmiInApi::setImpl(p_dsHdmiInHalMock);
        ON_CALL(*p_dsHdmiInHalMock, dsHdmiInInit()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsHdmiInHalMock, dsHdmiInTerm()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsHdmiInHalMock, dsHdmiInGetNumberOfInputs(::testing::_))
            .WillByDefault(Invoke([](uint8_t* count) {
                *count = 3;
                return dsERR_NONE;
            }));

        implementation = Core::ProxyType<Plugin::DeviceSettingsImp>::Create();
        hdmiIn = static_cast<Exchange::IDeviceSettingsHDMIIn*>(
            implementation->QueryInterface(Exchange::IDeviceSettingsHDMIIn::ID));
    }

    ~DeviceSettingsHdmiInTest() override
    {
        if (hdmiIn != nullptr) {
            hdmiIn->Release();
            hdmiIn = nullptr;
        }
        implementation.Release();

        DsHdmiInApi::setImpl(nullptr);
        delete p_dsHdmiInHalMock;
        p_dsHdmiInHalMock = nullptr;
    }
};

TEST_F(DeviceSettingsHdmiInTest, GetHDMIInNumberOfInputs)
{
    ASSERT_NE(nullptr, hdmiIn);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInGetNumberOfInputs(::testing::_))
        .Times(1)
        .WillOnce(Invoke([](uint8_t* count) {
            *count = 3;
            return dsERR_NONE;
        }));

    int32_t count = 0;
    EXPECT_EQ(Core::ERROR_NONE, hdmiIn->GetHDMIInNumberOfInputs(count));
    EXPECT_EQ(3, count);
}

TEST_F(DeviceSettingsHdmiInTest, GetHDMIInAllmStatusReadsHalValue)
{
    ASSERT_NE(nullptr, hdmiIn);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsGetAllmStatus(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Invoke([](dsHdmiInPort_t, bool* status) {
            *status = true;
            return dsERR_NONE;
        }));

    bool allmStatus = false;
    EXPECT_EQ(Core::ERROR_NONE, hdmiIn->GetHDMIInAllmStatus(HDMIInPort::DS_HDMI_IN_PORT_0, allmStatus));
    EXPECT_TRUE(allmStatus);
}

TEST_F(DeviceSettingsHdmiInTest, SelectHDMIInPortSuccess)
{
    ASSERT_NE(nullptr, hdmiIn);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInSelectPort(::testing::_, true, ::testing::_, false))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE,
        hdmiIn->SelectHDMIInPort(HDMIInPort::DS_HDMI_IN_PORT_0, true, false,
            HDMIVideoPlaneType::DS_HDMIIN_VIDEOPLANE_PRIMARY));
}

TEST_F(DeviceSettingsHdmiInTest, SelectHDMIInPortHalFailure)
{
    ASSERT_NE(nullptr, hdmiIn);

    EXPECT_CALL(*p_dsHdmiInHalMock, dsHdmiInSelectPort(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(dsERR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL,
        hdmiIn->SelectHDMIInPort(HDMIInPort::DS_HDMI_IN_PORT_0, true, false,
            HDMIVideoPlaneType::DS_HDMIIN_VIDEOPLANE_PRIMARY));
}
