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

#include "dsCompositeIn.h"
#include "dsError.h"
#include "dsUtl.h"
#include "dsTypes.h"

#include "rfcapi.h"

#include <core/Portability.h>
#include <WPEFramework/interfaces/IDeviceSettingsCompositeIn.h>
#include "DeviceSettingsTypes.h"

#include <cstdint>

using namespace WPEFramework;

namespace hal {
namespace dCompositeIn {

    class IPlatform {

    public:
        virtual ~IPlatform() {} 
        virtual void InitialiseHAL() = 0;
        virtual void DeInitialiseHAL() = 0;
        virtual void setAllCallbacks(const CallbackBundle& bundle) = 0;
        virtual void getPersistenceValue() = 0;

        // CompositeIn Platform interface methods - all pure virtual
        virtual uint32_t GetNrOfCompositeInputs(int32_t& nrCompositeInputs) = 0;
        virtual uint32_t GetCompositeInStatus(CompositeInStatus& status) = 0;
        virtual uint32_t SelectCompositeInPort(const CompositeInPort port) = 0;
        virtual uint32_t ScaleCompositeInVideo(const CompositeInVideoRectangle videoRect) = 0;

    };
} // namespace dCompositeIn
} // namespace hal