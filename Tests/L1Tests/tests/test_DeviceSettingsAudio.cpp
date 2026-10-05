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
#include "devicesettings/DsAudioHALMock.h"

using namespace WPEFramework;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;

using AudioPortType = Exchange::IDeviceSettingsAudio::AudioPortType;

// Audio initializes its HAL eagerly in the DeviceSettingsImp constructor
// (dsAudioPortInit), unlike FPD's lazy EnsurePlatInit retry pattern.
class DeviceSettingsAudioTest : public ::testing::Test {
protected:
    NiceMock<DsAudioHalMock>* p_dsAudioHalMock = nullptr;
    Core::ProxyType<Plugin::DeviceSettingsImp> implementation;
    Exchange::IDeviceSettingsAudio* audio = nullptr;

    DeviceSettingsAudioTest()
    {
        p_dsAudioHalMock = new NiceMock<DsAudioHalMock>;
        DsAudioApi::setImpl(p_dsAudioHalMock);
        ON_CALL(*p_dsAudioHalMock, dsAudioPortInit()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsAudioHalMock, dsAudioPortTerm()).WillByDefault(Return(dsERR_NONE));

        implementation = Core::ProxyType<Plugin::DeviceSettingsImp>::Create();
        audio = static_cast<Exchange::IDeviceSettingsAudio*>(
            implementation->QueryInterface(Exchange::IDeviceSettingsAudio::ID));
    }

    ~DeviceSettingsAudioTest() override
    {
        if (audio != nullptr) {
            audio->Release();
            audio = nullptr;
        }
        implementation.Release();

        DsAudioApi::setImpl(nullptr);
        delete p_dsAudioHalMock;
        p_dsAudioHalMock = nullptr;
    }

    int32_t GetHandle()
    {
        EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioPort(::testing::_, ::testing::_, ::testing::_))
            .WillOnce(Invoke([](dsAudioPortType_t, int, intptr_t* handle) {
                *handle = 1;
                return dsERR_NONE;
            }));
        int32_t handle = -1;
        EXPECT_EQ(Core::ERROR_NONE, audio->GetAudioPort(AudioPortType::AUDIO_PORT_TYPE_SPEAKER, 0, handle));
        return handle;
    }
};

TEST_F(DeviceSettingsAudioTest, GetAudioPortReturnsHandle)
{
    ASSERT_NE(nullptr, audio);
    EXPECT_EQ(1, GetHandle());
}

TEST_F(DeviceSettingsAudioTest, SetAudioMuteSuccess)
{
    ASSERT_NE(nullptr, audio);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioMute(handle, true))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioMute(handle, true));
}

TEST_F(DeviceSettingsAudioTest, IsAudioMutedReadsHalValue)
{
    ASSERT_NE(nullptr, audio);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsAudioHalMock, dsIsAudioMute(handle, ::testing::_))
        .Times(1)
        .WillOnce(Invoke([](intptr_t, bool* muted) {
            *muted = true;
            return dsERR_NONE;
        }));

    bool muted = false;
    EXPECT_EQ(Core::ERROR_NONE, audio->IsAudioMuted(handle, muted));
    EXPECT_TRUE(muted);
}

TEST_F(DeviceSettingsAudioTest, SetAudioLevelSuccess)
{
    ASSERT_NE(nullptr, audio);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsAudioHalMock, dsSetAudioLevel(handle, 50.0f))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, audio->SetAudioLevel(handle, 50.0f));
}

TEST_F(DeviceSettingsAudioTest, GetAudioLevelHalFailure)
{
    ASSERT_NE(nullptr, audio);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsAudioHalMock, dsGetAudioLevel(handle, ::testing::_))
        .Times(1)
        .WillOnce(Return(dsERR_GENERAL));

    float level = 0.0f;
    EXPECT_EQ(Core::ERROR_GENERAL, audio->GetAudioLevel(handle, level));
}
