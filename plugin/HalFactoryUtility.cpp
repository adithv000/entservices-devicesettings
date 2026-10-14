/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
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

#include <string>
#include <unordered_map>

#include <binder/IServiceManager.h>

#include "Module.h"
#include <core/Portability.h>

#include "HalFactoryUtility.h"
#include "UtilsLogging.h"

std::unordered_map<std::string, HalFactoryUtility::BackendType> HalFactoryUtility::mBackendType;

bool HalFactoryUtility::isAidlServiceAvailable(const android::String16 &expectedServiceName)
{
    const std::string serviceName = android::String8(expectedServiceName).string();
    auto backendTypeIt = mBackendType.find(serviceName);
    if (backendTypeIt != mBackendType.end()) {
        return backendTypeIt->second == BackendType::AIDL;
    }

    backendTypeIt = mBackendType.emplace(serviceName, BackendType::UNKNOWN).first;
    const android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    const bool available = serviceManager != nullptr && serviceManager->checkService(expectedServiceName) != nullptr;
    backendTypeIt->second = available ? BackendType::AIDL : BackendType::LEGACY;
    return available;
}
