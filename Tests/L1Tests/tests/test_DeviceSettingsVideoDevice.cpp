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
#include "devicesettings/DsVideoDeviceHALMock.h"

using namespace WPEFramework;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;

using VideoZoom = Exchange::IDeviceSettingsVideoDevice::VideoZoom;

class DeviceSettingsVideoDeviceTest : public ::testing::Test {
protected:
    NiceMock<DsVideoDeviceHalMock>* p_dsVideoDeviceHalMock = nullptr;
    Core::ProxyType<Plugin::DeviceSettingsImp> implementation;
    Exchange::IDeviceSettingsVideoDevice* videoDevice = nullptr;

    DeviceSettingsVideoDeviceTest()
    {
        p_dsVideoDeviceHalMock = new NiceMock<DsVideoDeviceHalMock>;
        DsVideoDeviceApi::setImpl(p_dsVideoDeviceHalMock);
        ON_CALL(*p_dsVideoDeviceHalMock, dsVideoDeviceInit()).WillByDefault(Return(dsERR_NONE));
        ON_CALL(*p_dsVideoDeviceHalMock, dsVideoDeviceTerm()).WillByDefault(Return(dsERR_NONE));

        implementation = Core::ProxyType<Plugin::DeviceSettingsImp>::Create();
        videoDevice = static_cast<Exchange::IDeviceSettingsVideoDevice*>(
            implementation->QueryInterface(Exchange::IDeviceSettingsVideoDevice::ID));
    }

    ~DeviceSettingsVideoDeviceTest() override
    {
        if (videoDevice != nullptr) {
            videoDevice->Release();
            videoDevice = nullptr;
        }
        implementation.Release();

        DsVideoDeviceApi::setImpl(nullptr);
        delete p_dsVideoDeviceHalMock;
        p_dsVideoDeviceHalMock = nullptr;
    }

    int32_t GetHandle()
    {
        EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetVideoDevice(::testing::_, ::testing::_))
            .WillOnce(Invoke([](int, intptr_t* handle) {
                *handle = 1;
                return dsERR_NONE;
            }));
        int32_t handle = -1;
        EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetVideoDeviceHandle(0, handle));
        return handle;
    }
};

TEST_F(DeviceSettingsVideoDeviceTest, GetVideoDeviceHandleSuccess)
{
    ASSERT_NE(nullptr, videoDevice);
    EXPECT_EQ(1, GetHandle());
}

TEST_F(DeviceSettingsVideoDeviceTest, SetVideoDeviceDFCCallsHal)
{
    ASSERT_NE(nullptr, videoDevice);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsSetDFC(handle, ::testing::_))
        .Times(1)
        .WillOnce(Return(dsERR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, videoDevice->SetVideoDeviceDFC(handle, VideoZoom::DS_VIDEO_DEVICE_ZOOM_FULL));
}

// GetVideoDeviceDFC is cache-only: it returns the value cached by the most recent
// SetVideoDeviceDFC call and never queries the HAL (dsGetDFC is never invoked).
TEST_F(DeviceSettingsVideoDeviceTest, GetVideoDeviceDFCReflectsLastSetValue)
{
    ASSERT_NE(nullptr, videoDevice);
    int32_t handle = GetHandle();

    ON_CALL(*p_dsVideoDeviceHalMock, dsSetDFC(::testing::_, ::testing::_)).WillByDefault(Return(dsERR_NONE));
    ASSERT_EQ(Core::ERROR_NONE, videoDevice->SetVideoDeviceDFC(handle, VideoZoom::DS_VIDEO_DEVICE_ZOOM_FULL));

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetDFC(::testing::_, ::testing::_)).Times(0);

    VideoZoom zoom = VideoZoom::DS_VIDEO_DEVICE_ZOOM_UNKNOWN;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetVideoDeviceDFC(handle, zoom));
    EXPECT_EQ(VideoZoom::DS_VIDEO_DEVICE_ZOOM_FULL, zoom);
}

TEST_F(DeviceSettingsVideoDeviceTest, GetHDRCapabilitiesReadsHalValue)
{
    ASSERT_NE(nullptr, videoDevice);
    int32_t handle = GetHandle();

    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetHDRCapabilities(handle, ::testing::_))
        .Times(1)
        .WillOnce(Invoke([](intptr_t, int* capabilities) {
            *capabilities = 0x04;
            return dsERR_NONE;
        }));

    int32_t capabilities = 0;
    EXPECT_EQ(Core::ERROR_NONE, videoDevice->GetHDRCapabilities(handle, capabilities));
    EXPECT_EQ(0x04, capabilities);
}
