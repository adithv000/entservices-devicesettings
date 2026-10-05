#!/bin/bash
set -x
set -e

THUNDER_TOOLS_COMMIT_SHA="d5dd83c7c19c49c7f25c558c126500bd2d64f7a4"
THUNDER_COMMIT_SHA="2c0fcc5529e7da734be558ca6efa05d934dcce31"

GITHUB_WORKSPACE="${PWD}"
ls -la "${GITHUB_WORKSPACE}"
cd "${GITHUB_WORKSPACE}"

apt update
apt install -y libsqlite3-dev libcurl4-openssl-dev valgrind lcov clang libsystemd-dev libboost-all-dev libwebsocketpp-dev meson libcunit1 libcunit1-dev curl protobuf-compiler-grpc libgrpc-dev libgrpc++-dev libunwind-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libdrm-dev libglib2.0-dev pkg-config
pip install jsonref

if [ ! -d "trower-base64" ]; then
    git clone https://github.com/xmidt-org/trower-base64.git
fi
cd trower-base64
meson setup --warnlevel 3 --werror build
ninja -C build
ninja -C build install
cd ..

git clone --branch R4_4-RDK https://github.com/rdkcentral/ThunderTools.git
cd ThunderTools
git checkout $THUNDER_TOOLS_COMMIT_SHA
cd ..

git clone --branch R4_4-RDK https://github.com/rdkcentral/Thunder.git
cd Thunder
git checkout $THUNDER_COMMIT_SHA
cd ..

git clone --branch develop https://github.com/rdkcentral/entservices-apis.git
git clone --branch feature/RDKEMW-25013 https://github.com/rdkcentral/entservices-testframework.git
git clone --branch main https://github.com/rdkcentral/rdk-halif-device_settings.git
git clone --branch main https://github.com/rdkcentral/devicesettings.git
git clone --branch develop https://github.com/rdkcentral/iarmbus.git
git clone https://github.com/rdkcentral/iarmmgrs.git
git clone --branch develop https://github.com/rdkcentral/entservices-helpers.git

# Keep backward-compatible parent path expected by some test CMake files.
if [ ! -e "$GITHUB_WORKSPACE/../entservices-helpers" ]; then
    ln -s "$GITHUB_WORKSPACE/entservices-helpers" "$GITHUB_WORKSPACE/../entservices-helpers"
fi

# Ensure mock iarmmgrs-hal headers exist in testframework for CI builds.
mkdir -p "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/rdk/iarmmgrs-hal"
touch "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/rdk/iarmmgrs-hal/sysMgr.h"
touch "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/rdk/iarmmgrs-hal/mfrMgr.h"

# Generate minimal mock headers before building entservices-helpers.
mkdir -p "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/rdk/iarmbus"
touch "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/rdk/iarmbus/libIARM.h"
touch "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/rdk/iarmbus/libIBus.h"
touch "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/iarm.h"
touch "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/telemetry_busmessage_sender.h"

echo "======================================================================================"
echo "building thunderTools"
cd ThunderTools
cd -

cmake -G Ninja -S ThunderTools -B build/ThunderTools \
    -DEXCEPTIONS_ENABLE=ON \
    -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
    -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
    -DGENERIC_CMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake"

cmake --build build/ThunderTools --target install

echo "======================================================================================"
echo "building thunder"
cd Thunder
cd -

cmake -G Ninja -S Thunder -B build/Thunder \
    -DMESSAGING=ON \
    -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
    -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
    -DGENERIC_CMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
    -DBUILD_TYPE=Debug \
    -DBINDING=127.0.0.1 \
    -DPORT=55555 \
    -DEXCEPTIONS_ENABLE=ON

cmake --build build/Thunder --target install

echo "======================================================================================"
echo "building entservices-apis"
cd entservices-apis
rm -rf jsonrpc/DTV.json
cd ..

cmake -G Ninja -S entservices-apis -B build/entservices-apis \
    -DEXCEPTIONS_ENABLE=ON \
    -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
    -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake"

cmake --build build/entservices-apis --target install

echo "======================================================================================"
echo "building entservices-helpers"

cmake -G Ninja -S entservices-helpers -B build/entservices-helpers \
    -DEXCEPTIONS_ENABLE=ON \
    -DCOMCAST_CONFIG=OFF \
    -DPLUGIN_HELPERS=ON \
    -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
    -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
    "-DCMAKE_CXX_FLAGS=-I$GITHUB_WORKSPACE/entservices-testframework/Tests/mocks -I$GITHUB_WORKSPACE/entservices-testframework/Tests/headers -I$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/rdk/iarmbus -include $GITHUB_WORKSPACE/entservices-testframework/Tests/mocks/Iarm.h"

cmake --build build/entservices-helpers --target install

mkdir -p "$GITHUB_WORKSPACE/install/usr/include/WPEFramework/interfaces"
find "$GITHUB_WORKSPACE/entservices-apis/apis/DeviceSettings" -name "IDeviceSettings*.h" -exec cp {} "$GITHUB_WORKSPACE/install/usr/include/WPEFramework/interfaces/" \; 2>/dev/null || true

cp -r "$GITHUB_WORKSPACE/rdk-halif-device_settings/include/." "$GITHUB_WORKSPACE/install/usr/include/"
cp -r "$GITHUB_WORKSPACE/devicesettings/rpc/include/." "$GITHUB_WORKSPACE/install/usr/include/"
cp -r "$GITHUB_WORKSPACE/devicesettings/ds/include/." "$GITHUB_WORKSPACE/install/usr/include/"

# Real IARM headers from iarmbus repo
cp -r "$GITHUB_WORKSPACE/iarmbus/core/include/." "$GITHUB_WORKSPACE/install/usr/include/"

# Create stub headers for external dependencies with no public repos
touch "$GITHUB_WORKSPACE/install/usr/include/rfcapi.h"
touch "$GITHUB_WORKSPACE/install/usr/include/mfrMgr.h"
touch "$GITHUB_WORKSPACE/install/usr/include/secure_wrapper.h"

# Copy real iarmmgrs public headers used by DeviceSettings.
cp "$GITHUB_WORKSPACE/iarmmgrs/sysmgr/include/sysMgr.h" "$GITHUB_WORKSPACE/install/usr/include/"
cp -r "$GITHUB_WORKSPACE/iarmmgrs/mfr/include/." "$GITHUB_WORKSPACE/install/usr/include/"

# Copy external stubs from testframework (no public repos available)
find "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers" -maxdepth 1 -type f -name "*.h" -exec cp {} "$GITHUB_WORKSPACE/install/usr/include/" \; 2>/dev/null || true
find "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/ccec" -maxdepth 1 -type f -name "*.h" -exec cp {} "$GITHUB_WORKSPACE/install/usr/include/" \; 2>/dev/null || true
find "$GITHUB_WORKSPACE/entservices-testframework/Tests/headers/rdk/iarmmgrs-hal" -maxdepth 1 -type f -name "*.h" -exec cp {} "$GITHUB_WORKSPACE/install/usr/include/" \; 2>/dev/null || true

# Ensure real iarmmgrs headers take precedence after external stub copies.
cp "$GITHUB_WORKSPACE/iarmmgrs/sysmgr/include/sysMgr.h" "$GITHUB_WORKSPACE/install/usr/include/"
cp -r "$GITHUB_WORKSPACE/iarmmgrs/mfr/include/." "$GITHUB_WORKSPACE/install/usr/include/"

echo "======================================================================================"
echo "device-settings repository dependencies are ready"