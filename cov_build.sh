#!/bin/bash
set -x
set -e

GITHUB_WORKSPACE="${PWD}"
ls -la "${GITHUB_WORKSPACE}"

echo "building entservices-devicesettings"

if ! pkg-config --exists glib-2.0; then
    echo "glib-2.0 development files are missing; run build_dependencies.sh first"
    exit 1
fi

if [ ! -d "$GITHUB_WORKSPACE/install/usr/include/wpeframework/helpers" ]; then
    echo "WPEFramework helpers headers are missing; run build_dependencies.sh first"
    exit 1
fi

cd "${GITHUB_WORKSPACE}"
cmake -G Ninja -S "$GITHUB_WORKSPACE" -B build/entservices-devicesettings \
    -DUSE_THUNDER_R4=ON \
    -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
    -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_IARMBus=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_Udev=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_RFC=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_RBus=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_DS=ON \
    -DCOMCAST_CONFIG=OFF \
    -DRDK_SERVICES_COVERITY=ON \
    -DDS_FOUND=ON \
    -DHIDE_NON_EXTERNAL_SYMBOLS=OFF \
    -DWPEFrameworkHelpers_INCLUDE_DIRS="$GITHUB_WORKSPACE/install/usr/include/wpeframework/helpers" \
    -DPLUGIN_DEVICESETTINGS=ON \
    -DCMAKE_CXX_FLAGS="-DEXCEPTIONS_ENABLE=ON \
    -fprofile-arcs \
    -ftest-coverage \
    -I ${GITHUB_WORKSPACE}/install/usr/include \
    -I ${GITHUB_WORKSPACE}/install/usr/include/WPEFramework \
    -I ${GITHUB_WORKSPACE}/devicesettings/rpc/include \
    -I ${GITHUB_WORKSPACE}/devicesettings/ds/include \
    -I ${GITHUB_WORKSPACE}/rdk-halif-device_settings/include \
    -I ${GITHUB_WORKSPACE}/entservices-testframework/Tests/headers \
    -I ${GITHUB_WORKSPACE}/entservices-testframework/Tests/headers/audiocapturemgr \
    -I ${GITHUB_WORKSPACE}/entservices-testframework/Tests/headers/rdk/ds \
    -I ${GITHUB_WORKSPACE}/entservices-testframework/Tests/headers/rdk/iarmbus \
    -I ${GITHUB_WORKSPACE}/entservices-testframework/Tests/headers/rdk/iarmmgrs-hal \
    -I ${GITHUB_WORKSPACE}/entservices-testframework/Tests/headers/ccec/drivers \
    -I ${GITHUB_WORKSPACE}/entservices-testframework/Tests/headers/network \
    -I ${GITHUB_WORKSPACE}/entservices-testframework/Tests \
    -I ${GITHUB_WORKSPACE}/Thunder/Source \
    -I ${GITHUB_WORKSPACE}/Thunder/Source/core \
    -Wall -Wno-unused-result -Wno-deprecated-declarations -Wno-error=format \
    --coverage \
    -Wl,-wrap,system -Wl,-wrap,popen -Wl,-wrap,syslog -Wl,-wrap,v_secure_system -Wl,-wrap,v_secure_popen -Wl,-wrap,v_secure_pclose -Wl,-wrap,unlink \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/Rfc.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/RBus.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/Telemetry.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/Udev.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/maintenanceMGR.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/pkg.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/secure_wrappermock.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/wpa_ctrl_mock.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/gdialservice.h \
    -include ${GITHUB_WORKSPACE}/entservices-testframework/Tests/mocks/MotionDetection.h \
    -DENABLE_TELEMETRY_LOGGING \
    -DUSE_IARMBUS \
    -DENABLE_SYSTEM_GET_STORE_DEMO_LINK \
    -DENABLE_DEEP_SLEEP \
    -DENABLE_SET_WAKEUP_SRC_CONFIG \
    -DENABLE_THERMAL_PROTECTION \
    -DUSE_DRM_SCREENCAPTURE \
    -DHAS_API_SYSTEM \
    -DHAS_API_POWERSTATE \
    -DHAS_RBUS \
    -DCLOCK_BRIGHTNESS_ENABLED \
    -DUSE_DS \
    -DENABLE_DEVICE_MANUFACTURER_INFO \
    -DTHUNDER_VERSION=4 -DTHUNDER_VERSION_MAJOR=4 -DTHUNDER_VERSION_MINOR=4" \

cmake --build build/entservices-devicesettings --target install
echo "======================================================================================"
exit 0