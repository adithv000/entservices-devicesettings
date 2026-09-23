/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
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

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <core/Portability.h>
#include <core/Proxy.h>
#include <core/Trace.h>

#include <com/com.h>
#include <core/core.h>
#include <plugins/plugins.h>

#include <interfaces/IDeviceSettingsCompositeIn.h>

#include "dsUtl.h"
#include "dsError.h"
#include "dsCompositeIn.h"

#include "hal/dCompositeIn.h"
#include "hal/dCompositeInImpl.h"
#include "DeviceSettingsTypes.h"

class CompositeIn {
    using IPlatform = hal::dCompositeIn::IPlatform;

    std::shared_ptr<IPlatform> _platform;

public:
    class INotification {
        public:
            virtual ~INotification() = default;
            virtual void OnCompositeInHotPlug(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort port, const bool isConnected) = 0;
            virtual void OnCompositeInSignalStatus(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort port, const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus signalStatus) = 0;
            virtual void OnCompositeInStatus(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort activePort, const bool isPresented) = 0;
            virtual void OnCompositeInVideoModeUpdate(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort activePort, const WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution videoResolution) = 0;
    };

public:
    void Platform_init();
    /** Deferred HAL init — called from DeviceSettingsImp::Configure() */
    void InitialiseHAL() { platform().InitialiseHAL(); }

    // CompositeIn HAL interface methods
    uint32_t GetNrOfCompositeInputs(int32_t &nrCompositeInputs);
    uint32_t GetCompositeInStatus(CompositeInStatus &status);
    uint32_t SelectCompositeInPort(const CompositeInPort port);
    uint32_t ScaleCompositeInVideo(const CompositeInVideoRectangle videoRect);

private:
    CompositeIn(INotification& parent, std::shared_ptr<IPlatform> platform);

    inline IPlatform& platform() const
    {
        return *_platform;
    }

    INotification& _parent;

public:
    static CompositeIn Create(INotification& parent);

    template <typename IMPL, typename... Args>
    static CompositeIn Create(INotification& parent, Args&&... args)
    {
        ENTRY_LOG;
        static_assert(std::is_base_of<IPlatform, IMPL>::value, "Impl must derive from hal::dCompositeIn::IPlatform");
        auto impl = std::shared_ptr<IMPL>(new IMPL(std::forward<Args>(args)...));
        ASSERT(impl != nullptr);
        EXIT_LOG;
        return CompositeIn(parent, std::move(impl));
    }

    // Public event methods - called by HAL callbacks (matches other component pattern)
    void OnCompositeInHotPlug(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort port, const bool isConnected);
    void OnCompositeInSignalStatus(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort port, const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInSignalStatus signalStatus);
    void OnCompositeInStatus(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort activePort, const bool isPresented);
    void OnCompositeInVideoModeUpdate(const WPEFramework::Exchange::IDeviceSettingsCompositeIn::CompositeInPort activePort, const WPEFramework::Exchange::IDeviceSettingsCompositeIn::DisplayVideoPortResolution videoResolution);
    ~CompositeIn() {};

};