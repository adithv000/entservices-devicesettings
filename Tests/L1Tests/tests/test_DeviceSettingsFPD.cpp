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
#include "devicesettings/DsFPDHALMock.h"

using namespace WPEFramework;
using ::testing::NiceMock;
using ::testing::Return;

using FPDIndicator = Exchange::IDeviceSettingsFPD::FPDIndicator;
using FPDState = Exchange::IDeviceSettingsFPD::FPDState;

// Mirrors TestPowerManager's HAL-mock-backed fixture pattern (entservices-powermanager
// Tests/L1Tests/tests/test_PowerManager.cpp), but for the DeviceSettings FPD component:
// the real DeviceSettingsImp/FPD/dFPDImpl stack is exercised end-to-end with only the
// extern "C" dsFPD.h HAL calls intercepted via DsFPDApi::setImpl().
class DeviceSettingsFPDTest : public ::testing::Test {
protected:
    NiceMock<DsFPDHalMock>* p_dsFPDHalMock = nullptr;
    Core::ProxyType<Plugin::DeviceSettingsImp> implementation;
    Exchange::IDeviceSettingsFPD* fpd = nullptr;

    DeviceSettingsFPDTest()
    {
        p_dsFPDHalMock = new NiceMock<DsFPDHalMock>;
        DsFPDApi::setImpl(p_dsFPDHalMock);
        // dFPDImpl::EnsurePlatInit() retries dsFPInit() up to 20 times; default it to
        // success so individual tests don't pay that retry cost.
        ON_CALL(*p_dsFPDHalMock, dsFPInit()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsFPDHalMock, dsFPTerm()).WillByDefault(Return(dsERR_NONE));

        implementation = Core::ProxyType<Plugin::DeviceSettingsImp>::Create();
        fpd = static_cast<Exchange::IDeviceSettingsFPD*>(
            implementation->QueryInterface(Exchange::IDeviceSettingsFPD::ID));
    }

    ~DeviceSettingsFPDTest() override
    {
        if (fpd != nullptr) {
            fpd->Release();
            fpd = nullptr;
        }
        implementation.Release();

        DsFPDApi::setImpl(nullptr);
        delete p_dsFPDHalMock;
        p_dsFPDHalMock = nullptr;
    }
};

TEST_F(DeviceSettingsFPDTest, SetFPDBrightnessSuccess)
{
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPBrightness(dsFPD_INDICATOR_POWER, 50))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, fpd->SetFPDBrightness(FPDIndicator::DS_FPD_INDICATOR_POWER, 50, false));
}

TEST_F(DeviceSettingsFPDTest, SetFPDBrightnessHalFailure)
{
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPBrightness(dsFPD_INDICATOR_POWER, 50))
        .Times(1)
        .WillOnce(Return(dsERR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, fpd->SetFPDBrightness(FPDIndicator::DS_FPD_INDICATOR_POWER, 50, false));
}

TEST_F(DeviceSettingsFPDTest, GetFPDBrightnessReadsHalValue)
{
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsGetFPBrightness(dsFPD_INDICATOR_POWER, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsFPDIndicator_t, dsFPDBrightness_t* brightness) {
            *brightness = 75;
            return dsERR_NONE;
        }));

    uint32_t brightness = 0;
    EXPECT_EQ(Core::ERROR_NONE, fpd->GetFPDBrightness(FPDIndicator::DS_FPD_INDICATOR_POWER, brightness, false));
    EXPECT_EQ(75u, brightness);
}

// dFPDImpl::SetFPDState() maps ON/OFF to dsSetFPBrightness() calls (matching the legacy
// RPC layer's approach), it does not call dsSetFPState() at all.
TEST_F(DeviceSettingsFPDTest, SetFPDStateOnUsesBrightnessCall)
{
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPBrightness(dsFPD_INDICATOR_POWER, ::testing::_))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, fpd->SetFPDState(FPDIndicator::DS_FPD_INDICATOR_POWER, FPDState::DS_FPD_STATE_ON));
}

TEST_F(DeviceSettingsFPDTest, SetFPDStateOffUsesZeroBrightness)
{
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPBrightness(dsFPD_INDICATOR_POWER, 0))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, fpd->SetFPDState(FPDIndicator::DS_FPD_INDICATOR_POWER, FPDState::DS_FPD_STATE_OFF));
}

// dFPDImpl::GetFPDState() reads back from an internal cache populated by the last
// SetFPDState() call rather than issuing a fresh HAL call.
TEST_F(DeviceSettingsFPDTest, GetFPDStateReflectsLastSetState)
{
    ASSERT_NE(nullptr, fpd);

    ON_CALL(*p_dsFPDHalMock, dsSetFPBrightness(::testing::_, ::testing::_)).WillByDefault(Return(dsERR_NONE));
    ASSERT_EQ(Core::ERROR_NONE, fpd->SetFPDState(FPDIndicator::DS_FPD_INDICATOR_POWER, FPDState::DS_FPD_STATE_OFF));

    FPDState state = FPDState::DS_FPD_STATE_MAX;
    EXPECT_CALL(*p_dsFPDHalMock, dsGetFPState(::testing::_, ::testing::_)).Times(0);
    EXPECT_EQ(Core::ERROR_NONE, fpd->GetFPDState(FPDIndicator::DS_FPD_INDICATOR_POWER, state));
    EXPECT_EQ(FPDState::DS_FPD_STATE_OFF, state);
}

TEST_F(DeviceSettingsFPDTest, SetAndGetFPDColor)
{
    ASSERT_NE(nullptr, fpd);

    EXPECT_CALL(*p_dsFPDHalMock, dsSetFPColor(dsFPD_INDICATOR_POWER, dsFPD_COLOR_RED))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));
    EXPECT_EQ(Core::ERROR_NONE, fpd->SetFPDColor(FPDIndicator::DS_FPD_INDICATOR_POWER, dsFPD_COLOR_RED));

    EXPECT_CALL(*p_dsFPDHalMock, dsGetFPColor(dsFPD_INDICATOR_POWER, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](dsFPDIndicator_t, dsFPDColor_t* color) {
            *color = dsFPD_COLOR_RED;
            return dsERR_NONE;
        }));
    uint32_t color = 0;
    EXPECT_EQ(Core::ERROR_NONE, fpd->GetFPDColor(FPDIndicator::DS_FPD_INDICATOR_POWER, color));
    EXPECT_EQ(static_cast<uint32_t>(dsFPD_COLOR_RED), color);
}
