# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2026 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.8)

set(PLUGIN_NAME L1TestsDS)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(${NAMESPACE}Plugins REQUIRED)

set(TEST_SRC
    tests/test_DeviceSettings.cpp
    tests/test_DeviceSettingsFPD.cpp
)

set(TEST_LIB
    ${NAMESPACE}Plugins::${NAMESPACE}Plugins
    ${NAMESPACE}DeviceSettingsImp
)

add_library(${MODULE_NAME} SHARED ${TEST_SRC})

target_include_directories(${MODULE_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/plugin
        ${CMAKE_SOURCE_DIR}/../rdk-halif-device_settings/include
        ${CMAKE_SOURCE_DIR}/../devicesettings/ds/include
        ${CMAKE_SOURCE_DIR}/../entservices-testframework/Tests/mocks
        ${CMAKE_SOURCE_DIR}/../entservices-testframework/Tests/mocks/devicesettings
        ${CMAKE_SOURCE_DIR}/../entservices-testframework/Tests/mocks/thunder)

target_link_libraries(${MODULE_NAME} PRIVATE ${TEST_LIB})

set_source_files_properties(
    tests/test_DeviceSettings.cpp
    tests/test_DeviceSettingsFPD.cpp
    PROPERTIES COMPILE_FLAGS "-fexceptions")

install(TARGETS ${MODULE_NAME} DESTINATION lib)
write_config(PLUGINS ${PLUGIN_NAME})