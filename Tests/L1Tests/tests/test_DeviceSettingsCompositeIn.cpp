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
#include "devicesettings/DsCompositeInHALMock.h"

using namespace WPEFramework;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;

using CompositeInPort = Exchange::IDeviceSettingsCompositeIn::CompositeInPort;
using CompositeInStatus = Exchange::IDeviceSettingsCompositeIn::CompositeInStatus;

// NOTE: dCompositeInImpl::InitialiseHAL() only calls dsCompositeInInit() on TV
// profiles (searchRdkProfile() reads /etc/device.properties, which is empty in
// the CI test image, so profileType is NOT_FOUND there). The per-call methods
// below resolve their HAL symbols independently of that guard, so they still
// route to this mock regardless of profile - hence no dsCompositeInInit
// expectation is set here.
class DeviceSettingsCompositeInTest : public ::testing::Test {
protected:
    NiceMock<DsCompositeInHalMock>* p_dsCompositeInHalMock = nullptr;
    Core::ProxyType<Plugin::DeviceSettingsImp> implementation;
    Exchange::IDeviceSettingsCompositeIn* compositeIn = nullptr;

    DeviceSettingsCompositeInTest()
    {
        p_dsCompositeInHalMock = new NiceMock<DsCompositeInHalMock>;
        DsCompositeInApi::setImpl(p_dsCompositeInHalMock);
        ON_CALL(*p_dsCompositeInHalMock, dsCompositeInInit()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsCompositeInHalMock, dsCompositeInTerm()).WillByDefault(Return(dsERR_NONE));

        implementation = Core::ProxyType<Plugin::DeviceSettingsImp>::Create();
        compositeIn = static_cast<Exchange::IDeviceSettingsCompositeIn*>(
            implementation->QueryInterface(Exchange::IDeviceSettingsCompositeIn::ID));
    }

    ~DeviceSettingsCompositeInTest() override
    {
        if (compositeIn != nullptr) {
            compositeIn->Release();
            compositeIn = nullptr;
        }
        implementation.Release();

        DsCompositeInApi::setImpl(nullptr);
        delete p_dsCompositeInHalMock;
        p_dsCompositeInHalMock = nullptr;
    }
};

TEST_F(DeviceSettingsCompositeInTest, GetNrOfCompositeInputs)
{
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock, dsCompositeInGetNumberOfInputs(::testing::_))
        .Times(1)
        .WillOnce(Invoke([](uint8_t* nrInputs) {
            *nrInputs = 2;
            return dsERR_NONE;
        }));

    int32_t nrCompositeInputs = 0;
    EXPECT_EQ(Core::ERROR_NONE, compositeIn->GetNrOfCompositeInputs(nrCompositeInputs));
    EXPECT_EQ(2, nrCompositeInputs);
}

TEST_F(DeviceSettingsCompositeInTest, GetCompositeInStatus)
{
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock, dsCompositeInGetStatus(::testing::_))
        .Times(1)
        .WillOnce(Invoke([](dsCompositeInStatus_t* status) {
            status->activePort = static_cast<dsCompositeInPort_t>(CompositeInPort::DS_COMPOSITE_IN_PORT_0);
            status->isPresented = true;
            return dsERR_NONE;
        }));

    CompositeInStatus status{};
    EXPECT_EQ(Core::ERROR_NONE, compositeIn->GetCompositeInStatus(status));
    EXPECT_EQ(CompositeInPort::DS_COMPOSITE_IN_PORT_0, status.activePort);
    EXPECT_TRUE(status.isPresented);
}

TEST_F(DeviceSettingsCompositeInTest, SelectCompositeInPortSuccess)
{
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock, dsCompositeInSelectPort(static_cast<dsCompositeInPort_t>(CompositeInPort::DS_COMPOSITE_IN_PORT_1)))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, compositeIn->SelectCompositeInPort(CompositeInPort::DS_COMPOSITE_IN_PORT_1));
}

TEST_F(DeviceSettingsCompositeInTest, SelectCompositeInPortHalFailure)
{
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock, dsCompositeInSelectPort(::testing::_))
        .Times(1)
        .WillOnce(Return(dsERR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, compositeIn->SelectCompositeInPort(CompositeInPort::DS_COMPOSITE_IN_PORT_0));
}

TEST_F(DeviceSettingsCompositeInTest, ScaleCompositeInVideo)
{
    ASSERT_NE(nullptr, compositeIn);

    EXPECT_CALL(*p_dsCompositeInHalMock, dsCompositeInScaleVideo(0, 0, 1280, 720))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    Exchange::IDeviceSettingsCompositeIn::VideoRectangle rect{};
    rect.x = 0;
    rect.y = 0;
    rect.width = 1280;
    rect.height = 720;
    EXPECT_EQ(Core::ERROR_NONE, compositeIn->ScaleCompositeInVideo(rect));
}
