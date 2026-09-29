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
#include <vector>
#include <sstream>
#include <algorithm>
#include <functional>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <strings.h>
#include <map>
#include <mutex>
#include <string>

#include "dHdmiIn.h"
#include "utils/VideoResolutionUtils.h"
#include "dsHdmiIn.h"
#include "dsError.h"
#include "dsHdmiInTypes.h"
#include "dsVideoDeviceTypes.h"
#include "dsUtl.h"
#include "dsTypes.h"

#include "rfcapi.h"

#include <WPEFramework/interfaces/IDeviceSettingsHDMIIn.h>
#include "DeviceSettingsTypes.h"

#ifdef LOG_PRI
#undef LOG_PRI
#endif

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/StrongPointer.h>
#include <utils/String16.h>
#include <com/rdk/hal/hdmiinput/IHDMIInputManager.h>
#include <com/rdk/hal/hdmiinput/IHDMIInput.h>
#include <com/rdk/hal/hdmiinput/IHDMIInputController.h>
#include <com/rdk/hal/hdmiinput/IHDMIInputControllerListener.h>
#include <com/rdk/hal/hdmiinput/BnHDMIInputControllerListener.h>
#include <com/rdk/hal/hdmiinput/IHDMIInputEventListener.h>
#include <com/rdk/hal/hdmiinput/BnHDMIInputEventListener.h>
#include <com/rdk/hal/hdmiinput/Capabilities.h>
#include <com/rdk/hal/hdmiinput/PlatformCapabilities.h>
#include <com/rdk/hal/hdmiinput/SignalState.h>
#include <com/rdk/hal/hdmiinput/State.h>
#include <com/rdk/hal/hdmiinput/HDCPStatus.h>
#include <com/rdk/hal/hdmiinput/HDCPProtocolVersion.h>
#include <com/rdk/hal/hdmiinput/HDMIVersion.h>

using android::sp;
using android::defaultServiceManager;
using android::interface_cast;
using android::String16;
using android::ProcessState;
using namespace com::rdk::hal::hdmiinput;

class dHdmiInAIDLImpl : public hal::dHdmiIn::IPlatform {

    dHdmiInAIDLImpl(const dHdmiInAIDLImpl&) = delete;
private:

    // ---- Per-port AIDL runtime state ----
    struct AidlPortCtx {
        sp<IHDMIInput>                   hdmiInput;
        sp<IHDMIInputController>         controller;
        sp<IHDMIInputControllerListener> ctrlListener;
        sp<IHDMIInputEventListener>      evtListener;
        bool   isOpen{false};
        bool   isStarted{false};
        bool   connected{false};
        int    signalState{-1};
        int    lastVIC{0};
        bool   vrrActive{false};
        double vrrFrameRate{0.0};
        std::vector<uint8_t> spdInfoFrame;
    };

    // ---- Instance state (replacing file-scope statics from dHdmiInImpl) ----
    bool   m_hdmiInInitialized{false};
    bool   m_isDalsEnabled{false};
    dsHdmiInCap_t m_hdmiInCap{};
    bool   m_edidallmsupport[dsHDMI_IN_PORT_MAX]{};
    bool   m_vrrsupport[dsHDMI_IN_PORT_MAX]{};
    bool   m_hdmiPortVrrCaps[dsHDMI_IN_PORT_MAX]{};
    tv_hdmi_edid_version_t m_edidversion[dsHDMI_IN_PORT_MAX]{};

    // ---- Event callbacks (replacing g_Hdmi* file-scope statics) ----
    std::function<void(DeviceSettingsHDMIIn::HDMIInPort, bool)>                                           m_HotPlugCallback;
    std::function<void(DeviceSettingsHDMIIn::HDMIInPort, DeviceSettingsHDMIIn::HDMIInSignalStatus)>       m_SignalStatusCallback;
    std::function<void(DeviceSettingsHDMIIn::HDMIInPort, DeviceSettingsHDMIIn::HDMIVideoPortResolution)>  m_VideoModeUpdateCallback;
    std::function<void(DeviceSettingsHDMIIn::HDMIInPort, bool)>                                           m_AllmStatusCallback;
    std::function<void(DeviceSettingsHDMIIn::HDMIInPort, DeviceSettingsHDMIIn::HDMIInAviContentType)>     m_AviContentTypeCallback;
    std::function<void(int32_t, int32_t)>                                                                 m_AVLatencyCallback;
    std::function<void(DeviceSettingsHDMIIn::HDMIInPort, DeviceSettingsHDMIIn::HDMIInVRRType)>            m_VRRStatusCallback;
    std::function<void(DeviceSettingsHDMIIn::HDMIInPort, bool)>                                           m_StatusCallback;
    std::mutex                                                                                            m_callbackMutex;

    template <typename Callback>
    Callback getCallbackCopy(const Callback& callback)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        return callback;
    }

    // ---- AIDL service/port state (replacing s_aidl* file-scope statics) ----
    sp<IHDMIInputManager>      m_aidlHdmiMgr;
    std::mutex                  m_aidlMutex;
    std::map<int, AidlPortCtx>  m_aidlPorts;
    int                         m_aidlActivePort{-1};
    uint8_t                     m_aidlPortCount{0};
    bool                        m_aidlPortArcCapable[dsHDMI_IN_PORT_MAX]{};

    class AidlServiceDeathRecipient : public android::IBinder::DeathRecipient {
    public:
        explicit AidlServiceDeathRecipient(dHdmiInAIDLImpl* impl)
            : m_impl(impl)
        {
        }

        void binderDied(const android::wp<android::IBinder>&) override
        {
            m_impl->onAidlServiceDied();
        }

    private:
        dHdmiInAIDLImpl* m_impl;
    };

    sp<AidlServiceDeathRecipient> m_aidlServiceDeathRecipient;

    // ---- Inner listener: per-port IHDMIInputController callbacks ----
    class CtrlListener : public ::com::rdk::hal::hdmiinput::BnHDMIInputControllerListener {
    public:
        CtrlListener(int portId, dHdmiInAIDLImpl* impl) : m_portId(portId), m_impl(impl) {}

        ::android::binder::Status onConnectionStateChanged(bool connected) override {
            LOGINFO("AIDL HDMI-In hot plug event received: port=%d, connected=%s", m_portId, connected ? "true" : "false");
            {
                std::lock_guard<std::mutex> lk(m_impl->m_aidlMutex);
                auto it = m_impl->m_aidlPorts.find(m_portId);
                if (it != m_impl->m_aidlPorts.end()) {
                    if (!connected || !it->second.connected)
                        it->second.spdInfoFrame.clear();
                    it->second.connected = connected;
                }
            }
            auto callback = m_impl->getCallbackCopy(m_impl->m_HotPlugCallback);
            if (callback)
                callback(
                    static_cast<DeviceSettingsHDMIIn::HDMIInPort>(m_portId), connected);
            return ::android::binder::Status::ok();
        }

        ::android::binder::Status onSignalStateChanged(
                ::com::rdk::hal::hdmiinput::SignalState signalState) override {
            LOGINFO("AIDL HDMI-In signal status event received: port=%d, signalState=%d", m_portId, (int)signalState);
            {
                std::lock_guard<std::mutex> lk(m_impl->m_aidlMutex);
                auto it = m_impl->m_aidlPorts.find(m_portId);
                if (it != m_impl->m_aidlPorts.end()) it->second.signalState = (int)signalState;
            }
            auto callback = m_impl->getCallbackCopy(m_impl->m_SignalStatusCallback);
            if (callback)
                callback(
                    static_cast<DeviceSettingsHDMIIn::HDMIInPort>(m_portId),
                    static_cast<DeviceSettingsHDMIIn::HDMIInSignalStatus>((int)signalState));
            return ::android::binder::Status::ok();
        }

        ::android::binder::Status onVIChanged(::com::rdk::hal::hdmiinput::VIC vic) override {
            LOGINFO("AIDL HDMI-In video mode event received: port=%d, VIC=%d", m_portId, (int)vic);
            {
                std::lock_guard<std::mutex> lk(m_impl->m_aidlMutex);
                auto it = m_impl->m_aidlPorts.find(m_portId);
                if (it != m_impl->m_aidlPorts.end()) it->second.lastVIC = (int)vic;
            }
            auto callback = m_impl->getCallbackCopy(m_impl->m_VideoModeUpdateCallback);
            if (callback) {
                dsVideoPortResolution_t dsRes;
                dHdmiInAIDLImpl::aidlVicToRes(vic, dsRes);
                DeviceSettingsHDMIIn::HDMIVideoPortResolution res;
                res.name             = CreateResolutionStr(dsRes);
                res.pixelResolution  = static_cast<DeviceSettingsHDMIIn::HDMIInVideoResolution>(dsRes.pixelResolution);
                res.aspectRatio      = static_cast<DeviceSettingsHDMIIn::HDMIVideoAspectRatio>(dsRes.aspectRatio);
                res.stereoScopicMode = static_cast<DeviceSettingsHDMIIn::HDMIInVideoStereoScopicMode>(dsRes.stereoScopicMode);
                res.frameRate        = static_cast<DeviceSettingsHDMIIn::HDMIInVideoFrameRate>(dsRes.frameRate);
                res.interlaced       = dsRes.interlaced;
                callback(
                    static_cast<DeviceSettingsHDMIIn::HDMIInPort>(m_portId), res);
            }
            return ::android::binder::Status::ok();
        }

        ::android::binder::Status onVRRChanged(
                bool vrrActive, bool /*mConst*/, bool /*fastV*/, double frameRate) override {
            LOGINFO("AIDL HDMI-In VRR event received: port=%d, active=%s, frameRate=%.2f",
                m_portId, vrrActive ? "true" : "false", frameRate);
            dsVRRType_t vrrType = vrrActive
                ? (frameRate > 0.0 ? dsVRR_AMD_FREESYNC : dsVRR_HDMI_VRR)
                : dsVRR_NONE;
            {
                std::lock_guard<std::mutex> lk(m_impl->m_aidlMutex);
                auto it = m_impl->m_aidlPorts.find(m_portId);
                if (it != m_impl->m_aidlPorts.end()) {
                    it->second.vrrActive    = vrrActive;
                    it->second.vrrFrameRate = frameRate;
                }
            }
            auto callback = m_impl->getCallbackCopy(m_impl->m_VRRStatusCallback);
            if (callback)
                callback(
                    static_cast<DeviceSettingsHDMIIn::HDMIInPort>(m_portId),
                    static_cast<DeviceSettingsHDMIIn::HDMIInVRRType>(vrrType));
            return ::android::binder::Status::ok();
        }

        ::android::binder::Status onAVIInfoFrame(const std::vector<uint8_t>& data) override {
            LOGINFO("AIDL HDMI-In AVI content type event received: port=%d, length=%zu", m_portId, data.size());
            dsAviContentType_t ct = dsAVICONTENT_TYPE_NOT_SIGNALLED;
            if (!dHdmiInAIDLImpl::aidlParseAviContentType(data, &ct))
                return ::android::binder::Status::ok();
            auto callback = m_impl->getCallbackCopy(m_impl->m_AviContentTypeCallback);
            if (callback)
                callback(
                    static_cast<DeviceSettingsHDMIIn::HDMIInPort>(m_portId),
                    static_cast<DeviceSettingsHDMIIn::HDMIInAviContentType>(ct));
            return ::android::binder::Status::ok();
        }

        ::android::binder::Status onAudioInfoFrame(const std::vector<uint8_t>& data) override {
            LOGINFO("AIDL HDMI-In audio info frame event received: port=%d, length=%zu", m_portId, data.size());
            return ::android::binder::Status::ok();
        }
        ::android::binder::Status onSPDInfoFrame(const std::vector<uint8_t>& data) override {
            LOGINFO("AIDL HDMI-In SPD info frame event received: port=%d, length=%zu", m_portId, data.size());
            std::lock_guard<std::mutex> lk(m_impl->m_aidlMutex);
            auto it = m_impl->m_aidlPorts.find(m_portId);
            if (it != m_impl->m_aidlPorts.end())
                it->second.spdInfoFrame = data;
            return ::android::binder::Status::ok();
        }
        ::android::binder::Status onDRMInfoFrame(const std::vector<uint8_t>& data) override {
            LOGINFO("AIDL HDMI-In DRM info frame event received: port=%d, length=%zu", m_portId, data.size());
            return ::android::binder::Status::ok();
        }
        ::android::binder::Status onVendorSpecificInfoFrame(const std::vector<uint8_t>& data) override {
            LOGINFO("AIDL HDMI-In vendor specific info frame event received: port=%d, length=%zu", m_portId, data.size());
            return ::android::binder::Status::ok();
        }
        ::android::binder::Status onHDCPStatusChanged(
                ::com::rdk::hal::hdmiinput::HDCPStatus status,
                ::com::rdk::hal::hdmiinput::HDCPProtocolVersion version) override {
            LOGINFO("AIDL HDMI-In HDCP status event received: port=%d, status=%d, version=%d",
                    m_portId, (int)status, (int)version);
            return ::android::binder::Status::ok();
        }
    private:
        int m_portId;
        dHdmiInAIDLImpl* m_impl;
    };

    // ---- Inner listener: port-level state / EDID change events ----
    class EvtListener : public ::com::rdk::hal::hdmiinput::BnHDMIInputEventListener {
    public:
        EvtListener(int portId, dHdmiInAIDLImpl* impl) : m_portId(portId), m_impl(impl) {}

        ::android::binder::Status onStateChanged(
                ::com::rdk::hal::hdmiinput::State oldState,
                ::com::rdk::hal::hdmiinput::State newState) override {
            LOGINFO("AIDL HDMI-In status event received: port=%d, oldState=%d, newState=%d",
                    m_portId, (int)oldState, (int)newState);
            bool presented = (newState == ::com::rdk::hal::hdmiinput::State::STARTED);
            {
                std::lock_guard<std::mutex> lk(m_impl->m_aidlMutex);
                if (presented) m_impl->m_aidlActivePort = m_portId;
                else if (m_impl->m_aidlActivePort == m_portId) m_impl->m_aidlActivePort = -1;
            }
            auto callback = m_impl->getCallbackCopy(m_impl->m_StatusCallback);
            if (callback)
                callback(
                    static_cast<DeviceSettingsHDMIIn::HDMIInPort>(m_portId), presented);
            return ::android::binder::Status::ok();
        }
        ::android::binder::Status onEDIDChange(const std::vector<uint8_t>& data) override {
            LOGINFO("AIDL HDMI-In EDID change event received: port=%d, length=%zu", m_portId, data.size());
            return ::android::binder::Status::ok();
        }
    private:
        int m_portId;
        dHdmiInAIDLImpl* m_impl;
    };

    // ---- Private AIDL helper methods ----

    void clearAidlStateLocked()
    {
        m_aidlPorts.clear();
        m_aidlHdmiMgr = nullptr;
        m_aidlActivePort = -1;
        m_aidlPortCount = 0;
        m_hdmiInInitialized = false;
    }

    void onAidlServiceDied()
    {
        std::lock_guard<std::mutex> lk(m_aidlMutex);
        LOGERR("IHDMIInputManager Binder died; clearing cached AIDL interfaces");
        clearAidlStateLocked();
    }

    bool isAidlServiceAlive()
    {
        sp<IHDMIInputManager> mgr;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            mgr = m_aidlHdmiMgr;
        }
        return mgr && android::IInterface::asBinder(mgr)->isBinderAlive();
    }

    bool ensureAidlService()
    {
        if (isAidlServiceAlive()) {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            if (!m_aidlPorts.empty()) return true;
        }

        onAidlServiceDied();
        aidlHdmiInInit();

        std::lock_guard<std::mutex> lk(m_aidlMutex);
        return m_aidlHdmiMgr && !m_aidlPorts.empty();
    }

    sp<IHDMIInputManager> getAidlHdmiMgr()
    {
        std::lock_guard<std::mutex> lk(m_aidlMutex);
        if (!m_aidlHdmiMgr || !android::IInterface::asBinder(m_aidlHdmiMgr)->isBinderAlive()) {
            clearAidlStateLocked();
            ProcessState::self()->startThreadPool();
            sp<android::IServiceManager> sm = defaultServiceManager();
            if (sm) {
                m_aidlHdmiMgr = interface_cast<IHDMIInputManager>(
                    sm->getService(String16(IHDMIInputManager::serviceName().c_str())));
                if (m_aidlHdmiMgr) {
                    if (!m_aidlServiceDeathRecipient) {
                        m_aidlServiceDeathRecipient = new AidlServiceDeathRecipient(this);
                    }
                    android::IInterface::asBinder(m_aidlHdmiMgr)->linkToDeath(m_aidlServiceDeathRecipient);
                }
            }
        }
        return m_aidlHdmiMgr;
    }

    static std::string getResolutionStr (dsVideoResolution_t resolution)
    {
        const auto* info = hal::video::getResolutionInfo(resolution);
        std::string resolutionStr = info ? info->resolutionStr : "unknown";

        LOGINFO("ResolutionStr: %s", resolutionStr.c_str());
        return resolutionStr;
    }

    static std::string getFrameRateStr (dsVideoFrameRate_t frameRate)
    {
        const auto* info = hal::video::getFrameRateInfo(frameRate);
        std::string FrameRateStr = info ? info->frameRateStr : "";

        LOGINFO("FrameRateStr: %s", FrameRateStr.c_str());
        return FrameRateStr;
    }

    static std::string CreateResolutionStr (const dsVideoPortResolution_t &resolution)
    {
        std::string resolutionStr = getResolutionStr(resolution.pixelResolution);
        if(resolutionStr.compare("unknown") != 0){
            resolutionStr = getResolutionStr(resolution.pixelResolution);
            const char* interlacedStr = hal::video::getInterlacedStr(resolution.interlaced);
            LOGINFO("InterlacedStr: %s", interlacedStr);
            resolutionStr += interlacedStr;
            resolutionStr += getFrameRateStr(resolution.frameRate);
        }
        LOGINFO("resolutionStr : %s", resolutionStr.c_str());
        return resolutionStr;
    }

    // Map VIC code to dsVideoPortResolution_t for callback conversion.
    static void aidlVicToRes(::com::rdk::hal::hdmiinput::VIC vic, dsVideoPortResolution_t& res)
    {
        memset(&res, 0, sizeof(res));

        res.aspectRatio = dsVIDEO_ASPECT_RATIO_16x9;
        res.stereoScopicMode = dsVIDEO_SSMODE_2D;
        switch (vic) {
            case VIC::VIC0_UNAVAILABLE:
                res.pixelResolution = dsVIDEO_PIXELRES_720x480;
                res.aspectRatio = dsVIDEO_ASPECT_RATIO_4x3;
                res.stereoScopicMode = dsVIDEO_SSMODE_UNKNOWN;
                res.frameRate = dsVIDEO_FRAMERATE_UNKNOWN;
                return;

            case VIC::VIC1_640_480_P_60_4_3:
            case VIC::VIC2_720_480_P_60_4_3:
            case VIC::VIC3_720_480_P_60_16_9:
            case VIC::VIC6_720_1440_480_I_60_4_3:
            case VIC::VIC7_720_1440_480_I_60_16_9:
            case VIC::VIC8_720_1440_240_P_60_4_3:
            case VIC::VIC9_720_1440_240_P_60_16_9:
            case VIC::VIC10_2880_480_I_60_4_3:
            case VIC::VIC11_2880_480_I_60_16_9:
            case VIC::VIC12_2880_240_P_60_4_3:
            case VIC::VIC13_2880_240_P_60_16_9:
            case VIC::VIC14_1440_480_P_60_4_3:
            case VIC::VIC15_1440_480_P_60_16_9:
            case VIC::VIC35_2880_480_P_60_4_3:
            case VIC::VIC36_2880_480_P_60_16_9:
            case VIC::VIC48_720_480_P_120_4_3:
            case VIC::VIC49_720_480_P_120_16_9:
            case VIC::VIC50_720_1440_480_I_120_4_3:
            case VIC::VIC51_720_1440_480_I_120_16_9:
            case VIC::VIC56_720_480_P_240_4_3:
            case VIC::VIC57_720_480_P_240_16_9:
            case VIC::VIC58_720_1440_480_I_240_4_3:
            case VIC::VIC59_720_1440_480_I_240_16_9:
                res.pixelResolution = dsVIDEO_PIXELRES_720x480;
                break;

            case VIC::VIC17_720_576_P_50_4_3:
            case VIC::VIC18_720_576_P_50_16_9:
            case VIC::VIC21_720_1440_576_I_50_4_3:
            case VIC::VIC22_720_1440_576_I_50_16_9:
            case VIC::VIC23_720_1440_288_P_50_4_3:
            case VIC::VIC24_720_1440_288_P_50_16_9:
            case VIC::VIC25_2880_576_I_50_4_3:
            case VIC::VIC26_2880_576_I_50_16_9:
            case VIC::VIC27_2880_288_P_50_4_3:
            case VIC::VIC28_2880_288_P_50_16_9:
            case VIC::VIC29_1440_576_P_50_4_3:
            case VIC::VIC30_1440_576_P_50_16_9:
            case VIC::VIC37_2880_576_P_50_4_3:
            case VIC::VIC38_2880_576_P_50_16_9:
            case VIC::VIC42_720_576_P_100_4_3:
            case VIC::VIC43_720_576_P_100_16_9:
            case VIC::VIC44_720_1440_576_I_100_4_3:
            case VIC::VIC45_720_1440_576_I_100_16_9:
            case VIC::VIC52_720_576_P_200_4_3:
            case VIC::VIC53_720_576_P_200_16_9:
            case VIC::VIC54_720_1440_576_I_200_4_3:
            case VIC::VIC55_720_1440_576_I_200_16_9:
                res.pixelResolution = dsVIDEO_PIXELRES_720x576;
                break;

            case VIC::VIC4_1280_720_P_60_16_9:
            case VIC::VIC19_1280_720_P_50_16_9:
            case VIC::VIC41_1280_720_P_100_16_9:
            case VIC::VIC47_1280_720_P_120_16_9:
            case VIC::VIC60_1280_720_P_24_16_9:
            case VIC::VIC61_1280_720_P_25_16_9:
            case VIC::VIC62_1280_720_P_30_16_9:
            case VIC::VIC65_1280_720_P_24_64_27:
            case VIC::VIC66_1280_720_P_25_64_27:
            case VIC::VIC67_1280_720_P_30_64_27:
            case VIC::VIC68_1280_720_P_50_64_27:
            case VIC::VIC69_1280_720_P_60_64_27:
            case VIC::VIC70_1280_720_P_100_64_27:
            case VIC::VIC71_1280_720_P_120_64_27:
            case VIC::VIC79_1680_720_P_24_64_27:
            case VIC::VIC80_1680_720_P_25_64_27:
            case VIC::VIC81_1680_720_P_30_64_27:
            case VIC::VIC82_1680_720_P_50_64_27:
            case VIC::VIC83_1680_720_P_60_64_27:
            case VIC::VIC84_1680_720_P_100_64_27:
            case VIC::VIC85_1680_720_P_120_64_27:
            case VIC::VIC108_1280_720_P_48_16_9:
            case VIC::VIC109_1280_720_P_48_64_27:
            case VIC::VIC110_1680_720_P_48_64_27:
                res.pixelResolution = dsVIDEO_PIXELRES_1280x720;
                break;

            case VIC::VIC5_1920_1080_I_60_16_9:
            case VIC::VIC16_1920_1080_P_60_16_9:
            case VIC::VIC20_1920_1080_I_50_16_9:
            case VIC::VIC31_1920_1080_P_50_16_9:
            case VIC::VIC32_1920_1080_P_24_16_9:
            case VIC::VIC33_1920_1080_P_25_16_9:
            case VIC::VIC34_1920_1080_P_30_16_9:
            case VIC::VIC39_1920_1080_I_50_16_9:
            case VIC::VIC40_1920_1080_I_100_16_9:
            case VIC::VIC46_1920_1080_I_120_16_9:
            case VIC::VIC63_1920_1080_P_120_16_9:
            case VIC::VIC64_1920_1080_P_100_16_9:
            case VIC::VIC72_1920_1080_P_24_64_27:
            case VIC::VIC73_1920_1080_P_25_64_27:
            case VIC::VIC74_1920_1080_P_30_64_27:
            case VIC::VIC75_1920_1080_P_50_64_27:
            case VIC::VIC76_1920_1080_P_60_64_27:
            case VIC::VIC77_1920_1080_P_100_64_27:
            case VIC::VIC78_1920_1080_P_120_64_27:
            case VIC::VIC86_2560_1080_P_24_64_27:
            case VIC::VIC87_2560_1080_P_25_64_27:
            case VIC::VIC88_2560_1080_P_30_64_27:
            case VIC::VIC89_2560_1080_P_50_64_27:
            case VIC::VIC90_2560_1080_P_60_64_27:
            case VIC::VIC91_2560_1080_P_100_64_27:
            case VIC::VIC92_2560_1080_P_120_64_27:
            case VIC::VIC111_1920_1080_P_48_16_9:
            case VIC::VIC112_1920_1080_P_48_64_27:
            case VIC::VIC113_2560_1080_P_48_64_27:
                res.pixelResolution = dsVIDEO_PIXELRES_1920x1080;
                break;

            case VIC::VIC93_3840_2160_P_24_16_9:
            case VIC::VIC94_3840_2160_P_25_16_9:
            case VIC::VIC95_3840_2160_P_30_16_9:
            case VIC::VIC96_3840_2160_P_50_16_9:
            case VIC::VIC97_3840_2160_P_60_16_9:
            case VIC::VIC103_3840_2160_P_24_64_27:
            case VIC::VIC104_3840_2160_P_25_64_27:
            case VIC::VIC105_3840_2160_P_30_64_27:
            case VIC::VIC106_3840_2160_P_50_64_27:
            case VIC::VIC107_3840_2160_P_60_64_27:
            case VIC::VIC114_3840_2160_P_48_16_9:
            case VIC::VIC116_3840_2160_P_48_64_27:
            case VIC::VIC117_3840_2160_P_100_16_9:
            case VIC::VIC118_3840_2160_P_120_16_9:
            case VIC::VIC119_3840_2160_P_100_64_27:
            case VIC::VIC120_3840_2160_P_120_64_27:
                res.pixelResolution = dsVIDEO_PIXELRES_3840x2160;
                break;

            case VIC::VIC98_4096_2160_P_24_256_135:
            case VIC::VIC99_4096_2160_P_25_256_135:
            case VIC::VIC100_4096_2160_P_30_256_135:
            case VIC::VIC101_4096_2160_P_50_256_135:
            case VIC::VIC102_4096_2160_P_60_256_135:
            case VIC::VIC115_4096_2160_P_48_256_135:
            case VIC::VIC218_4096_2160_P_100_256_135:
            case VIC::VIC219_4096_2160_P_120_256_135:
                res.pixelResolution = dsVIDEO_PIXELRES_4096x2160;
                break;

            case VIC::VIC121_5120_2160_P_24_64_27:
            case VIC::VIC122_5120_2160_P_25_64_27:
            case VIC::VIC123_5120_2160_P_30_64_27:
            case VIC::VIC124_5120_2160_P_48_64_27:
            case VIC::VIC125_5120_2160_P_50_64_27:
            case VIC::VIC126_5120_2160_P_60_64_27:
            case VIC::VIC127_5120_2160_P_100_64_27:
            case VIC::VIC193_5120_2160_P_120_64_27:
            case VIC::VIC194_7680_4320_P_24_16_9:
            case VIC::VIC195_7680_4320_P_25_16_9:
            case VIC::VIC196_7680_4320_P_30_16_9:
            case VIC::VIC197_7680_4320_P_48_16_9:
            case VIC::VIC198_7680_4320_P_50_16_9:
            case VIC::VIC199_7680_4320_P_60_16_9:
            case VIC::VIC200_7680_4320_P_100_16_9:
            case VIC::VIC201_7680_4320_P_120_16_9:
            case VIC::VIC202_7680_4320_P_24_64_27:
            case VIC::VIC203_7680_4320_P_25_64_27:
            case VIC::VIC204_7680_4320_P_30_64_27:
            case VIC::VIC205_7680_4320_P_48_64_27:
            case VIC::VIC206_7680_4320_P_50_64_27:
            case VIC::VIC207_7680_4320_P_60_64_27:
            case VIC::VIC208_7680_4320_P_100_64_27:
            case VIC::VIC209_7680_4320_P_120_64_27:
            case VIC::VIC210_10240_4320_P_24_64_27:
            case VIC::VIC211_10240_4320_P_25_64_27:
            case VIC::VIC212_10240_4320_P_30_64_27:
            case VIC::VIC213_10240_4320_P_48_64_27:
            case VIC::VIC214_10240_4320_P_50_64_27:
            case VIC::VIC215_10240_4320_P_60_64_27:
            case VIC::VIC216_10240_4320_P_100_64_27:
            case VIC::VIC217_10240_4320_P_120_64_27:
                // The legacy HDMI-In API has no 5K, 8K, or 10K resolution enum.
                res.pixelResolution = dsVIDEO_PIXELRES_4096x2160;
                break;
        }

        switch (vic) {
            case VIC::VIC1_640_480_P_60_4_3:
            case VIC::VIC2_720_480_P_60_4_3:
            case VIC::VIC6_720_1440_480_I_60_4_3:
            case VIC::VIC8_720_1440_240_P_60_4_3:
            case VIC::VIC10_2880_480_I_60_4_3:
            case VIC::VIC12_2880_240_P_60_4_3:
            case VIC::VIC14_1440_480_P_60_4_3:
            case VIC::VIC17_720_576_P_50_4_3:
            case VIC::VIC21_720_1440_576_I_50_4_3:
            case VIC::VIC23_720_1440_288_P_50_4_3:
            case VIC::VIC25_2880_576_I_50_4_3:
            case VIC::VIC27_2880_288_P_50_4_3:
            case VIC::VIC29_1440_576_P_50_4_3:
            case VIC::VIC35_2880_480_P_60_4_3:
            case VIC::VIC42_720_576_P_100_4_3:
            case VIC::VIC44_720_1440_576_I_100_4_3:
            case VIC::VIC48_720_480_P_120_4_3:
            case VIC::VIC50_720_1440_480_I_120_4_3:
            case VIC::VIC52_720_576_P_200_4_3:
            case VIC::VIC54_720_1440_576_I_200_4_3:
            case VIC::VIC56_720_480_P_240_4_3:
            case VIC::VIC58_720_1440_480_I_240_4_3:
                res.aspectRatio = dsVIDEO_ASPECT_RATIO_4x3;
                break;
            default:
                break;
        }

        switch (vic) {
            case VIC::VIC5_1920_1080_I_60_16_9:
            case VIC::VIC6_720_1440_480_I_60_4_3:
            case VIC::VIC7_720_1440_480_I_60_16_9:
            case VIC::VIC10_2880_480_I_60_4_3:
            case VIC::VIC11_2880_480_I_60_16_9:
            case VIC::VIC20_1920_1080_I_50_16_9:
            case VIC::VIC21_720_1440_576_I_50_4_3:
            case VIC::VIC22_720_1440_576_I_50_16_9:
            case VIC::VIC25_2880_576_I_50_4_3:
            case VIC::VIC26_2880_576_I_50_16_9:
            case VIC::VIC39_1920_1080_I_50_16_9:
            case VIC::VIC40_1920_1080_I_100_16_9:
            case VIC::VIC44_720_1440_576_I_100_4_3:
            case VIC::VIC45_720_1440_576_I_100_16_9:
            case VIC::VIC46_1920_1080_I_120_16_9:
            case VIC::VIC50_720_1440_480_I_120_4_3:
            case VIC::VIC51_720_1440_480_I_120_16_9:
            case VIC::VIC54_720_1440_576_I_200_4_3:
            case VIC::VIC55_720_1440_576_I_200_16_9:
            case VIC::VIC58_720_1440_480_I_240_4_3:
            case VIC::VIC59_720_1440_480_I_240_16_9:
                res.interlaced = true;
                break;
            default:
                break;
        }

        const int32_t vicId = static_cast<int32_t>(vic);
        if ((vicId >= 1 && vicId <= 16) || (vicId >= 35 && vicId <= 36) ||
            vicId == 69 || vicId == 76 || vicId == 83 || vicId == 90 || vicId == 97 ||
            vicId == 102 || vicId == 107 || vicId == 126 || vicId == 199 ||
            vicId == 207 || vicId == 215) {
            res.frameRate = dsVIDEO_FRAMERATE_60;
        } else if ((vicId >= 17 && vicId <= 31) || (vicId >= 37 && vicId <= 39) ||
                   vicId == 68 || vicId == 75 || vicId == 82 || vicId == 89 || vicId == 96 ||
                   vicId == 101 || vicId == 106 || vicId == 125 || vicId == 198 ||
                   vicId == 206 || vicId == 214) {
            res.frameRate = dsVIDEO_FRAMERATE_50;
        } else if (vicId == 32 || vicId == 60 || vicId == 65 || vicId == 72 || vicId == 79 ||
                   vicId == 86 || vicId == 93 || vicId == 98 || vicId == 103 || vicId == 121 ||
                   vicId == 194 || vicId == 202 || vicId == 210) {
            res.frameRate = dsVIDEO_FRAMERATE_24;
        } else if (vicId == 33 || vicId == 61 || vicId == 66 || vicId == 73 || vicId == 80 ||
                   vicId == 87 || vicId == 94 || vicId == 99 || vicId == 104 || vicId == 122 ||
                   vicId == 195 || vicId == 203 || vicId == 211) {
            res.frameRate = dsVIDEO_FRAMERATE_25;
        } else if (vicId == 34 || vicId == 62 || vicId == 67 || vicId == 74 || vicId == 81 ||
                   vicId == 88 || vicId == 95 || vicId == 100 || vicId == 105 || vicId == 123 ||
                   vicId == 196 || vicId == 204 || vicId == 212) {
            res.frameRate = dsVIDEO_FRAMERATE_30;
        } else if ((vicId >= 40 && vicId <= 45) || vicId == 64 || vicId == 70 || vicId == 77 ||
                   vicId == 84 || vicId == 91 || vicId == 117 || vicId == 119 || vicId == 127 ||
                   vicId == 200 || vicId == 208 || vicId == 216 || vicId == 218) {
            res.frameRate = dsVIDEO_FRAMERATE_100;
        } else if ((vicId >= 46 && vicId <= 51) || vicId == 63 || vicId == 71 || vicId == 78 ||
                   vicId == 85 || vicId == 92 || vicId == 118 || vicId == 120 || vicId == 193 ||
                   vicId == 201 || vicId == 209 || vicId == 217 || vicId == 219) {
            res.frameRate = dsVIDEO_FRAMERATE_120;
        } else if (vicId >= 52 && vicId <= 55) {
            res.frameRate = dsVIDEO_FRAMERATE_200;
        } else if (vicId >= 56 && vicId <= 59) {
            res.frameRate = dsVIDEO_FRAMERATE_240;
        } else if ((vicId >= 108 && vicId <= 116) || vicId == 124 || vicId == 197 ||
                   vicId == 205 || vicId == 213) {
            res.frameRate = dsVIDEO_FRAMERATE_UNKNOWN;
        }
    }

    // Parse AVI InfoFrame bytes to extract content type.
    static bool aidlParseAviContentType(const std::vector<uint8_t>& infoFrame, dsAviContentType_t* contentType)
    {
        static const uint8_t kAviType = 0x82;
        static const size_t  kMinLen  = 9;
        if (!contentType) return false;
        *contentType = dsAVICONTENT_TYPE_NOT_SIGNALLED;
        if (infoFrame.size() < kMinLen || infoFrame[0] != kAviType) return false;
        uint8_t payloadLen = infoFrame[2];
        if (payloadLen < 5 || infoFrame.size() < (size_t)(4 + payloadLen)) return false;
        if (!(infoFrame[6] & 0x80)) return true;
        switch ((infoFrame[8] >> 4) & 0x03) {
            case 0: *contentType = dsAVICONTENT_TYPE_GRAPHICS; break;
            case 1: *contentType = dsAVICONTENT_TYPE_PHOTO;    break;
            case 2: *contentType = dsAVICONTENT_TYPE_CINEMA;   break;
            case 3: *contentType = dsAVICONTENT_TYPE_GAME;     break;
            default: break;
        }
        return true;
    }

    // Infer EDID version from raw EDID bytes (HDMI Forum VSB presence → 2.0).
    static tv_hdmi_edid_version_t aidlGetEdidVersion(const std::vector<uint8_t>& edidVec)
    {
        static const uint8_t kHdr[]          = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        static const uint8_t kHdmiForumOui[] = {0xD8, 0x5D, 0xC4};
        if (edidVec.size() < 128 || memcmp(edidVec.data(), kHdr, sizeof(kHdr)) != 0)
            return HDMI_EDID_VER_MAX;
        uint8_t extCnt = edidVec.size() / 128 - 1; // edidVec[126] contains the extension block count in standard EDID
        if ((edidVec.size() / 128) == 0 || extCnt > (edidVec.size() / 128 - 1))
            return HDMI_EDID_VER_MAX;
        for (uint8_t e = 0; e < extCnt; ++e) {
            size_t base = (size_t)(e + 1) * 128;
            if (base + 128 > edidVec.size() || edidVec[base] != 0x02) continue;
            uint8_t dtdOff = edidVec[base + 2];
            if (dtdOff < 4 || dtdOff > 127) return HDMI_EDID_VER_MAX;
            size_t idx = base + 4, end = base + dtdOff;
            while (idx < end) {
                uint8_t tl = edidVec[idx];
                uint8_t tag = (tl >> 5) & 0x07, len = tl & 0x1F;
                if (idx + 1 + len > end) return HDMI_EDID_VER_MAX;
                if (tag == 0x03 && len >= 3 && memcmp(&edidVec[idx + 1], kHdmiForumOui, 3) == 0)
                    return HDMI_EDID_VER_20;
                idx += 1 + len;
            }
        }
        return HDMI_EDID_VER_14;
    }

    static bool aidlMapEdidVersion(tv_hdmi_edid_version_t legacyVer, HDMIVersion* out)
    {
        if (!out) return false;
        switch (legacyVer) {
            case HDMI_EDID_VER_14: *out = HDMIVersion::HDMI_1_4; return true;
            case HDMI_EDID_VER_20: *out = HDMIVersion::HDMI_2_0; return true;
            default: return false;
        }
    }

    static bool aidlEdidVersionSupported(const Capabilities& caps, HDMIVersion ver)
    {
        return std::find(caps.supportedVersions.begin(), caps.supportedVersions.end(), ver)
               != caps.supportedVersions.end();
    }

    void aidlHdmiInInit()
    {
        sp<IHDMIInputManager> mgr = getAidlHdmiMgr();
        if (!mgr) { LOGERR("IHDMIInputManager unavailable"); return; }

        std::vector<IHDMIInput::Id> portIds;
        if (!mgr->getHDMIInputIds(&portIds).isOk()) { LOGERR("getHDMIInputIds failed"); return; }
        m_aidlPortCount = (uint8_t)portIds.size();

        for (const auto& id : portIds) {
            int portIdx = id.value;
            sp<IHDMIInput> hdmiInput;
            if (!mgr->getHDMIInput(id, &hdmiInput).isOk() || !hdmiInput) {
                LOGERR("getHDMIInput failed for port %d", portIdx);
                continue;
            }
            AidlPortCtx ctx;
            ctx.hdmiInput = hdmiInput;

            Capabilities caps;
            if (hdmiInput->getCapabilities(&caps).isOk() && portIdx < dsHDMI_IN_PORT_MAX) {
                m_aidlPortArcCapable[portIdx] = caps.supportsARC;
                m_hdmiPortVrrCaps[portIdx]    = caps.supportsVRR;
            }

            ctx.evtListener = sp<EvtListener>::make(portIdx, this);
            bool regOk = false;
            hdmiInput->registerEventListener(ctx.evtListener, &regOk);

            ctx.ctrlListener = sp<CtrlListener>::make(portIdx, this);
            sp<IHDMIInputController> ctrl;
            if (hdmiInput->open(ctx.ctrlListener, &ctrl).isOk() && ctrl) {
                ctx.controller = ctrl;
                ctx.isOpen     = true;
            }

            std::lock_guard<std::mutex> lk(m_aidlMutex);
            m_aidlPorts[portIdx] = std::move(ctx);
            LOGINFO("AIDL port %d initialised", portIdx);
        }
    }

    void aidlTerm()
    {
        std::lock_guard<std::mutex> lk(m_aidlMutex);
        if (m_aidlHdmiMgr && m_aidlServiceDeathRecipient) {
            android::IInterface::asBinder(m_aidlHdmiMgr)->unlinkToDeath(m_aidlServiceDeathRecipient);
        }
        for (auto& kv : m_aidlPorts) {
            AidlPortCtx& ctx = kv.second;
            if (ctx.isStarted && ctx.controller) { ctx.controller->stop(); ctx.isStarted = false; }
            if (ctx.isOpen && ctx.hdmiInput && ctx.controller) {
                bool ok = false;
                ctx.hdmiInput->close(ctx.controller, &ok);
                ctx.isOpen = false;
            }
            if (ctx.hdmiInput && ctx.evtListener) {
                bool ok = false;
                ctx.hdmiInput->unregisterEventListener(ctx.evtListener, &ok);
            }
        }
        clearAidlStateLocked();
    }

    void getDynamicAutoLatencyConfig()
    {
        RFC_ParamData_t param = {0};
        WDMP_STATUS status = getRFCParameter((char*)"dssrv", TVSETTINGS_DALS_RFC_PARAM, &param);
        LOGINFO("DALS Feature Enable = [ %s ]", param.value);
        if (WDMP_SUCCESS == status && (strncasecmp(param.value, "true", 4) == 0)) {
            m_isDalsEnabled = true;
            LOGINFO("Value of isDalsEnabled = [ %d ]", m_isDalsEnabled);
        } else {
            LOGERR("Fetching RFC for DALS failed or DALS is disabled: %d", status);
        }
    }

    bool getHdmiInPortPersistValue(const std::string& propertyName, int /*portIndex*/)
    {
        try {
            std::string value = device::HostPersistence::getInstance().getProperty(propertyName, "TRUE");
            bool support = (value == "TRUE");
            LOGINFO("Port property %s: Value: %s, Parsed: %d", propertyName.c_str(), value.c_str(), support);
            return support;
        } catch (...) {
            LOGERR("Port property %s: Exception getting property, using default TRUE", propertyName.c_str());
            return true;
        }
    }

    void updateEdidAllmBitValuesInPersistence(dsHdmiInPort_t iHdmiPort, bool allmSupport)
    {
        switch (iHdmiPort) {
            case dsHDMI_IN_PORT_0:
                device::HostPersistence::getInstance().persistHostProperty("HDMI0.edidallmEnable", allmSupport ? "TRUE" : "FALSE");
                LOGINFO("Port HDMI0: Persist EDID Allm Bit: %d", allmSupport); break;
            case dsHDMI_IN_PORT_1:
                device::HostPersistence::getInstance().persistHostProperty("HDMI1.edidallmEnable", allmSupport ? "TRUE" : "FALSE");
                LOGINFO("Port HDMI1: Persist EDID Allm Bit: %d", allmSupport); break;
            case dsHDMI_IN_PORT_2:
                device::HostPersistence::getInstance().persistHostProperty("HDMI2.edidallmEnable", allmSupport ? "TRUE" : "FALSE");
                LOGINFO("Port HDMI2: Persist EDID Allm Bit: %d", allmSupport); break;
            case dsHDMI_IN_PORT_3:
                device::HostPersistence::getInstance().persistHostProperty("HDMI3.edidallmEnable", allmSupport ? "TRUE" : "FALSE");
                LOGINFO("Port HDMI3: Persist EDID Allm Bit: %d", allmSupport); break;
            default:
                LOGWARN("Invalid HDMI port %d for ALLM persistence update", iHdmiPort); break;
        }
    }

public:
    dHdmiInAIDLImpl()
    {
        LOGINFO("dHdmiInAIDLImpl Constructor");
        InitialiseHAL();
    }

    virtual ~dHdmiInAIDLImpl()
    {
        LOGINFO("dHdmiInAIDLImpl Destructor");
        DeInitialiseHAL();
    }

    void InitialiseHAL()
    {
        getDynamicAutoLatencyConfig();
        profileType = searchRdkProfile();
        LOGINFO("profileType %d", profileType);

        if (TV == profileType) {
            aidlHdmiInInit();
            LOGINFO("dHdmiInAIDLImpl: AIDL init complete, %d ports found", m_aidlPortCount);
        }
    }

    void DeInitialiseHAL()
    {
        LOGINFO("profileType %d", profileType);
        if (TV == profileType) {
            aidlTerm();
        }
    }

    void setAllCallbacks(const CallbackBundle bundle) override
    {
        ENTRY_LOG;
        LOGINFO("setAllCallbacks: profileType %d", profileType);
        if (!m_hdmiInInitialized && !m_aidlPorts.empty()) {
            if (TV == profileType) {
                // AIDL listeners registered in aidlHdmiInInit; just store the callbacks.
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                if (bundle.OnHDMIInHotPlugEvent)         m_HotPlugCallback         = bundle.OnHDMIInHotPlugEvent;
                if (bundle.OnHDMIInSignalStatusEvent)    m_SignalStatusCallback    = bundle.OnHDMIInSignalStatusEvent;
                if (bundle.OnHDMIInStatusEvent)          m_StatusCallback          = bundle.OnHDMIInStatusEvent;
                if (bundle.OnHDMIInVideoModeUpdateEvent) m_VideoModeUpdateCallback = bundle.OnHDMIInVideoModeUpdateEvent;
                if (bundle.OnHDMIInAllmStatusEvent)      m_AllmStatusCallback      = bundle.OnHDMIInAllmStatusEvent;
                if (bundle.OnHDMIInAVIContentTypeEvent)  m_AviContentTypeCallback  = bundle.OnHDMIInAVIContentTypeEvent;
                if (bundle.OnHDMIInAVLatencyEvent)       m_AVLatencyCallback       = bundle.OnHDMIInAVLatencyEvent;
                if (bundle.OnHDMIInVRRStatusEvent)       m_VRRStatusCallback       = bundle.OnHDMIInVRRStatusEvent;
            }
        }
        EXIT_LOG;
    }

    void getPersistenceValue() override
    {
        if (!m_hdmiInInitialized && !m_aidlPorts.empty()) {
            int itr = 0;

            for (itr = 0; itr < dsHDMI_IN_PORT_MAX; itr++) {
                m_hdmiInCap.isPortArcCapable[itr] = m_aidlPortArcCapable[itr];
            }

            m_edidallmsupport[dsHDMI_IN_PORT_0] = getHdmiInPortPersistValue("HDMI0.edidallmEnable", dsHDMI_IN_PORT_0);
            m_edidallmsupport[dsHDMI_IN_PORT_1] = getHdmiInPortPersistValue("HDMI1.edidallmEnable", dsHDMI_IN_PORT_1);
            m_edidallmsupport[dsHDMI_IN_PORT_2] = getHdmiInPortPersistValue("HDMI2.edidallmEnable", dsHDMI_IN_PORT_2);
            m_edidallmsupport[dsHDMI_IN_PORT_3] = getHdmiInPortPersistValue("HDMI3.edidallmEnable", dsHDMI_IN_PORT_3);

            m_vrrsupport[dsHDMI_IN_PORT_0] = getHdmiInPortPersistValue("HDMI0.vrrEnable", dsHDMI_IN_PORT_0);
            m_vrrsupport[dsHDMI_IN_PORT_1] = getHdmiInPortPersistValue("HDMI1.vrrEnable", dsHDMI_IN_PORT_1);
            m_vrrsupport[dsHDMI_IN_PORT_2] = getHdmiInPortPersistValue("HDMI2.vrrEnable", dsHDMI_IN_PORT_2);
            m_vrrsupport[dsHDMI_IN_PORT_3] = getHdmiInPortPersistValue("HDMI3.vrrEnable", dsHDMI_IN_PORT_3);

            auto loadEdidVer = [&](dsHdmiInPort_t port, const std::string& key) {
                try {
                    std::string val = device::HostPersistence::getInstance().getProperty(key);
                    m_edidversion[port] = static_cast<tv_hdmi_edid_version_t>(atoi(val.c_str()));
                } catch (...) {
                    try {
                        std::string val = device::HostPersistence::getInstance().getDefaultProperty(key);
                        m_edidversion[port] = static_cast<tv_hdmi_edid_version_t>(atoi(val.c_str()));
                    } catch (...) {
                        LOGERR("Port %s: Exception getting EDID version, defaulting to 2.0", key.c_str());
                        m_edidversion[port] = HDMI_EDID_VER_20;
                    }
                }
            };
            loadEdidVer(dsHDMI_IN_PORT_0, "HDMI0.edidversion");
            loadEdidVer(dsHDMI_IN_PORT_1, "HDMI1.edidversion");
            loadEdidVer(dsHDMI_IN_PORT_2, "HDMI2.edidversion");

            for (itr = 0; itr < dsHDMI_IN_PORT_MAX; itr++) {
                LOGINFO("Port HDMI%d: VRR capability: %d", itr, m_hdmiPortVrrCaps[itr]);
            }
            for (itr = 0; itr < dsHDMI_IN_PORT_MAX; itr++) {
                if (SetHDMIEdidVersion(static_cast<HDMIInPort>(itr), static_cast<HDMIInEdidVersion>(m_edidversion[itr])) == WPEFramework::Core::ERROR_NONE) {
                    LOGINFO("Port HDMI%d: Initialized EDID Version: %d", itr, m_edidversion[itr]);
                }
            }
            m_hdmiInInitialized = true;
        }
        LOGINFO("Set Callbacks");
    }

    virtual uint32_t GetHDMIInNumberOfInputs(int32_t& count) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        count = static_cast<int32_t>(m_aidlPortCount);
        LOGINFO("GetHDMIInNumberOfInputs: count=%d (AIDL)", count);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDMIInStatus(HDMIInStatus& hdmiStatus, IHDMIInPortConnectionStatusIterator*& portConnectionStatus) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        std::vector<DeviceSettingsHDMIIn::HDMIPortConnectionStatus> portStatuses;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            hdmiStatus.activePort  = static_cast<HDMIInPort>(m_aidlActivePort);
            hdmiStatus.isPresented = (m_aidlActivePort >= 0);
            for (int p = 0; p < dsHDMI_IN_PORT_MAX; p++) {
                DeviceSettingsHDMIIn::HDMIPortConnectionStatus ps;
                auto it = m_aidlPorts.find(p);
                ps.isPortConnected = (it != m_aidlPorts.end()) ? it->second.connected : false;
                portStatuses.push_back(ps);
            }
        }
        portConnectionStatus = WPEFramework::Core::Service<WPEFramework::RPC::IteratorType<IHDMIInPortConnectionStatusIterator>>::Create<IHDMIInPortConnectionStatusIterator>(portStatuses);
        LOGINFO("GetHDMIInStatus (AIDL): activePort=%d isPresented=%s", m_aidlActivePort, hdmiStatus.isPresented ? "true" : "false");
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SelectHDMIInPort(const HDMIInPort port, const bool requestAudioMix, const bool topMostPlane, const HDMIVideoPlaneType videoPlaneType) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;

        const int portId = static_cast<int>(port);
        sp<IHDMIInput> hdmiInput;
        sp<IHDMIInputController> controller;
        sp<IHDMIInputControllerListener> controllerListener;
        sp<IHDMIInputController> activeController;
        int activePort = -1;
        bool alreadyStarted = false;

        if (portId == -1) {
            sp<IHDMIInputController> activeController;
            {
                std::lock_guard<std::mutex> lk(m_aidlMutex);
                activePort = m_aidlActivePort;
                auto active = m_aidlPorts.find(activePort);
                if (active != m_aidlPorts.end() && active->second.isStarted)
                    activeController = active->second.controller;
            }

            if (activeController && !activeController->stop().isOk()) {
                LOGERR("SelectHDMIInPort: stop failed while deselecting active port %d", activePort);
                return WPEFramework::Core::ERROR_GENERAL;
            }

            {
                std::lock_guard<std::mutex> lk(m_aidlMutex);
                auto active = m_aidlPorts.find(activePort);
                if (active != m_aidlPorts.end()) active->second.isStarted = false;
                if (m_aidlActivePort == activePort) m_aidlActivePort = -1;
            }
            LOGINFO("SelectHDMIInPort: deselected active port %d; no replacement port started", activePort);
            return WPEFramework::Core::ERROR_NONE;
        }

        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            auto selected = m_aidlPorts.find(portId);
            if (selected == m_aidlPorts.end() || !selected->second.hdmiInput) {
                LOGERR("SelectHDMIInPort: invalid port %d", portId);
                return WPEFramework::Core::ERROR_GENERAL;
            }

            hdmiInput = selected->second.hdmiInput;
            controller = selected->second.controller;
            controllerListener = selected->second.ctrlListener;
            alreadyStarted = selected->second.isStarted;
            activePort = m_aidlActivePort;
            if (activePort != portId) {
                auto active = m_aidlPorts.find(activePort);
                if (active != m_aidlPorts.end() && active->second.isStarted)
                    activeController = active->second.controller;
            }
        }

        if (activeController && !activeController->stop().isOk()) {
            LOGERR("SelectHDMIInPort: stop failed for active port %d", activePort);
            return WPEFramework::Core::ERROR_GENERAL;
        }

        if (!alreadyStarted && !controller->start().isOk()) {
            LOGERR("SelectHDMIInPort: start failed for port %d", portId);
            return WPEFramework::Core::ERROR_GENERAL;
        }

        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            if (activePort != portId) {
                auto active = m_aidlPorts.find(activePort);
                if (active != m_aidlPorts.end()) active->second.isStarted = false;
            }
            auto selected = m_aidlPorts.find(portId);
            if (selected != m_aidlPorts.end()) selected->second.isStarted = true;
            m_aidlActivePort = portId;
        }

        LOGINFO("SelectHDMIInPort: port=%d selected; requestAudioMix=%s, topMostPlane=%s, videoPlaneType=%d are not supported by the HDMI-input AIDL interface",
                portId, requestAudioMix ? "true" : "false", topMostPlane ? "true" : "false", videoPlaneType);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t ScaleHDMIInVideo(const HDMIInVideoRectangle videoPosition) override
    {
        LOGINFO("ScaleHDMIInVideo: on hold pending PlaneControl (AIDL)");
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    uint32_t SelectHDMIZoomMode(const HDMIInVideoZoom zoomMode) override
    {
        LOGINFO("SelectHDMIZoomMode: on hold pending PlaneControl (AIDL)");
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    uint32_t GetSupportedGameFeaturesList(IHDMIInGameFeatureListIterator*& gameFeatureList) override
    {
        if (!ensureAidlService()) { gameFeatureList = nullptr; return WPEFramework::Core::ERROR_UNAVAILABLE; }
        std::vector<sp<IHDMIInput>> inputs;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            for (const auto& kv : m_aidlPorts)
                if (kv.second.hdmiInput) inputs.push_back(kv.second.hdmiInput);
        }
        if (inputs.empty()) { gameFeatureList = nullptr; return WPEFramework::Core::ERROR_GENERAL; }

        bool supAllm = false, supVrr = false, supFreeSync = false;
        for (const auto& hi : inputs) {
            Capabilities caps;
            if (!hi->getCapabilities(&caps).isOk()) { gameFeatureList = nullptr; return WPEFramework::Core::ERROR_GENERAL; }
            supAllm     = supAllm     || caps.supportsALLM;
            supVrr      = supVrr      || caps.supportsVRR;
            supFreeSync = supFreeSync || caps.supportsFreeSync;
        }
        FreeSync freeSyncTier = FreeSync::UNSUPPORTED;
        if (supFreeSync) {
            sp<IHDMIInputManager> mgr = getAidlHdmiMgr();
            if (mgr) {
                PlatformCapabilities pCaps;
                if (mgr->getCapabilities(&pCaps).isOk()) freeSyncTier = pCaps.freeSync;
            }
        }
        std::vector<std::string> features;
        if (supAllm)     features.emplace_back("ALLM");
        if (supVrr)      features.emplace_back("VRR_HDMI");
        if (supFreeSync) {
            switch (freeSyncTier) {
                case FreeSync::FREESYNC_PREMIUM:     features.emplace_back("VRR_AMD_FREESYNC_PREMIUM"); break;
                case FreeSync::FREESYNC_PREMIUM_PRO: features.emplace_back("VRR_AMD_FREESYNC_PREMIUM_PRO"); break;
                default:                             features.emplace_back("VRR_AMD_FREESYNC"); break;
            }
        }
        LOGINFO("GetSupportedGameFeaturesList count=%zu (AIDL)", features.size());

        std::vector<DeviceSettingsHDMIIn::HDMIInGameFeatureList> featuresList;
        for (const auto& f : features) {
            DeviceSettingsHDMIIn::HDMIInGameFeatureList gf;
            gf.gameFeature = f;
            featuresList.push_back(gf);
        }
        gameFeatureList = WPEFramework::Core::Service<WPEFramework::RPC::IteratorType<IHDMIInGameFeatureListIterator>>
                              ::Create<IHDMIInGameFeatureListIterator>(featuresList);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDMIInAVLatency(uint32_t& videoLatency, uint32_t& audioLatency) override
    {
        audioLatency = 0;
        videoLatency = 0;
        LOGINFO("GetHDMIInAVLatency: returning 0/0 (AIDL, no PlaneControl)");
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDMIInAllmStatus(const HDMIInPort port, bool& allmStatus) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        sp<IHDMIInput> hi;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            auto it = m_aidlPorts.find((int)hdmiPort);
            if (it == m_aidlPorts.end() || !it->second.hdmiInput) return WPEFramework::Core::ERROR_GENERAL;
            hi = it->second.hdmiInput;
        }
        Capabilities caps;
        if (!hi->getCapabilities(&caps).isOk()) return WPEFramework::Core::ERROR_GENERAL;
        allmStatus = caps.supportsALLM;
        LOGINFO("GetHDMIInAllmStatus: port=%d, allmStatus=%s (AIDL)", (int)hdmiPort, allmStatus ? "true" : "false");
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDMIInEdid2AllmSupport(const HDMIInPort port, bool& allmSupport) override
    {
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        if (hdmiPort < dsHDMI_IN_PORT_MAX) {
            allmSupport = m_edidallmsupport[hdmiPort];
            LOGINFO("GetHDMIInEdid2AllmSupport: port=%d, allmSupport=%s", (int)hdmiPort, allmSupport ? "true" : "false");
            return WPEFramework::Core::ERROR_NONE;
        }
        return WPEFramework::Core::ERROR_GENERAL;
    }

    uint32_t SetHDMIInEdid2AllmSupport(const HDMIInPort port, bool allmSupport) override
    {
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        if (hdmiPort < dsHDMI_IN_PORT_MAX) {
            LOGINFO("SetHDMIInEdid2AllmSupport: checking edidversion of port %d: %d", (int)hdmiPort, m_edidversion[hdmiPort]);
            if (m_edidversion[hdmiPort] == HDMI_EDID_VER_20) {
                updateEdidAllmBitValuesInPersistence(hdmiPort, allmSupport);
                m_edidallmsupport[hdmiPort] = allmSupport;
                LOGINFO("SetHDMIInEdid2AllmSupport: port=%d, allmSupport=%s (AIDL)", (int)hdmiPort, allmSupport ? "true" : "false");
                return WPEFramework::Core::ERROR_NONE;
            }
            LOGINFO("EDID version is not 2.0, cannot set ALLM support for port %d", (int)hdmiPort);
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }
        return WPEFramework::Core::ERROR_GENERAL;
    }

    uint32_t GetEdidBytes(const HDMIInPort port, const uint16_t edidBytesLength, uint8_t edidBytes[]) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        sp<IHDMIInput> hi;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            auto it = m_aidlPorts.find((int)hdmiPort);
            if (it == m_aidlPorts.end() || !it->second.hdmiInput) return WPEFramework::Core::ERROR_GENERAL;
            hi = it->second.hdmiInput;
        }
        std::vector<uint8_t> edidVec;
        bool ok = false;
        if (!hi->getEDID(&edidVec, &ok).isOk() || !ok || edidVec.empty()) {
            LOGERR("GetEdidBytes: getEDID failed for port %d", (int)hdmiPort);
            return WPEFramework::Core::ERROR_GENERAL;
        }
        int copyLen = std::min((int)edidVec.size(), (int)edidBytesLength);
        memcpy(edidBytes, edidVec.data(), copyLen);
        LOGINFO("GetEdidBytes: port=%d, len=%d (AIDL)", (int)hdmiPort, copyLen);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDMISPDInformation(const HDMIInPort port, const uint16_t spdBytesLength, uint8_t spdBytes[]) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        sp<IHDMIInput> hi;
        std::vector<uint8_t> spdVec;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            auto it = m_aidlPorts.find((int)hdmiPort);
            if (it == m_aidlPorts.end() || !it->second.hdmiInput) return WPEFramework::Core::ERROR_GENERAL;
            hi = it->second.hdmiInput;
            spdVec = it->second.spdInfoFrame;
        }

        if (spdVec.empty()) {
            if (!hi->getSPDInfoFrame(&spdVec).isOk() || spdVec.empty()) {
                LOGERR("GetHDMISPDInformation: getSPDInfoFrame failed for port %d", (int)hdmiPort);
                return WPEFramework::Core::ERROR_GENERAL;
            }
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            auto it = m_aidlPorts.find((int)hdmiPort);
            if (it != m_aidlPorts.end())
                it->second.spdInfoFrame = spdVec;
        }

        memset(spdBytes, 0, spdBytesLength);
        size_t copyLen = std::min(spdVec.size(), (size_t)spdBytesLength);
        memcpy(spdBytes, spdVec.data(), copyLen);
        LOGINFO("GetHDMISPDInformation: port=%d, len=%zu (AIDL)", (int)hdmiPort, copyLen);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDMIEdidVersion(const HDMIInPort port, HDMIInEdidVersion& edidVersion) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        sp<IHDMIInput> hi;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            auto it = m_aidlPorts.find((int)hdmiPort);
            if (it == m_aidlPorts.end() || !it->second.hdmiInput) return WPEFramework::Core::ERROR_GENERAL;
            hi = it->second.hdmiInput;
        }
        std::vector<uint8_t> edidVec;
        bool ok = false;
        if (!hi->getEDID(&edidVec, &ok).isOk() || !ok || edidVec.size() < 128)
            return WPEFramework::Core::ERROR_GENERAL;
        edidVersion = static_cast<HDMIInEdidVersion>(aidlGetEdidVersion(edidVec));
        LOGINFO("GetHDMIEdidVersion: port=%d, version=%d (AIDL)", (int)hdmiPort, (int)edidVersion);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetHDMIEdidVersion(const HDMIInPort port, const HDMIInEdidVersion edidVersion) override
    {
        dsHdmiInPort_t hdmiPort    = static_cast<dsHdmiInPort_t>(port);
        tv_hdmi_edid_version_t ver = static_cast<tv_hdmi_edid_version_t>(edidVersion);
        LOGINFO("SetHDMIEdidVersion: requested port=%d, EDID version=%d", (int)hdmiPort, (int)ver);

        if (!ensureAidlService()) {
            LOGERR("SetHDMIEdidVersion: AIDL service unavailable for port=%d, version=%d", (int)hdmiPort, (int)ver);
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        HDMIVersion aidlVersion;
        if (!aidlMapEdidVersion(ver, &aidlVersion)) {
            LOGERR("SetHDMIEdidVersion: unsupported legacy EDID version=%d for port=%d", (int)ver, (int)hdmiPort);
            return WPEFramework::Core::ERROR_INVALID_PARAMETER;
        }

        sp<IHDMIInput> hi;
        sp<IHDMIInputController> ctrl;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            auto it = m_aidlPorts.find((int)hdmiPort);
            if (it == m_aidlPorts.end() || !it->second.hdmiInput || !it->second.controller) {
                LOGERR("SetHDMIEdidVersion: missing HDMI input or controller for port=%d", (int)hdmiPort);
                return WPEFramework::Core::ERROR_GENERAL;
            }
            hi   = it->second.hdmiInput;
            ctrl = it->second.controller;
        }
        Capabilities caps;
        const bool capabilitiesOk = hi->getCapabilities(&caps).isOk();
        if (!capabilitiesOk) {
            LOGERR("SetHDMIEdidVersion: getCapabilities failed for port=%d, requested version=%d", (int)hdmiPort, (int)ver);
            return WPEFramework::Core::ERROR_GENERAL;
        }
        if (!aidlEdidVersionSupported(caps, aidlVersion)) {
            LOGERR("SetHDMIEdidVersion: EDID version=%d is not supported by port=%d", (int)ver, (int)hdmiPort);
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        std::vector<uint8_t> edidVec;
        bool ok = false;
        const bool defaultEdidCallOk = hi->getDefaultEDID(aidlVersion, &edidVec, &ok).isOk();
        if (!defaultEdidCallOk || !ok || edidVec.size() < 128) {
            LOGERR("SetHDMIEdidVersion: getDefaultEDID failed for port=%d, version=%d (callOk=%s, resultOk=%s, size=%zu)",
                   (int)hdmiPort, (int)ver, defaultEdidCallOk ? "true" : "false", ok ? "true" : "false", edidVec.size());
            return WPEFramework::Core::ERROR_GENERAL;
        }
        ok = false;
        const bool setEdidCallOk = ctrl->setEDID(edidVec, &ok).isOk();
        if (!setEdidCallOk || !ok) {
            LOGERR("SetHDMIEdidVersion: setEDID failed for port=%d, version=%d (callOk=%s, resultOk=%s, size=%zu)",
                   (int)hdmiPort, (int)ver, setEdidCallOk ? "true" : "false", ok ? "true" : "false", edidVec.size());
            return WPEFramework::Core::ERROR_GENERAL;
        }

        int port_no = (int)hdmiPort;
        if (port_no >= 0 && port_no < dsHDMI_IN_PORT_MAX) {
            std::string key = "HDMI" + std::to_string(port_no) + ".edidversion";
            device::HostPersistence::getInstance().persistHostProperty(key, std::to_string(static_cast<int>(ver)));
            m_edidversion[port_no] = ver;
            if (ver < HDMI_EDID_VER_20) {
                LOGINFO("EDID version for port %d is less than 2.0, disabling ALLM support", port_no);
                m_edidallmsupport[port_no] = false;
                device::HostPersistence::getInstance().persistHostProperty("HDMI" + std::to_string(port_no) + ".edidallmsupport", "false");
            }
        }
        LOGINFO("SetHDMIEdidVersion: port=%d, version=%d (AIDL)", (int)hdmiPort, ver);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDMIVideoMode(HDMIVideoPortResolution& videoPortResolution) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        int vic = 0;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            if (m_aidlActivePort >= 0) {
                auto it = m_aidlPorts.find(m_aidlActivePort);
                if (it != m_aidlPorts.end()) vic = it->second.lastVIC;
            }
        }
        dsVideoPortResolution_t dsRes;
        aidlVicToRes((::com::rdk::hal::hdmiinput::VIC)vic, dsRes);
        videoPortResolution.name             = CreateResolutionStr(dsRes);
        videoPortResolution.pixelResolution  = static_cast<HDMIInVideoResolution>(dsRes.pixelResolution);
        videoPortResolution.aspectRatio      = static_cast<HDMIVideoAspectRatio>(dsRes.aspectRatio);
        videoPortResolution.stereoScopicMode = static_cast<HDMIInVideoStereoScopicMode>(dsRes.stereoScopicMode);
        videoPortResolution.frameRate        = static_cast<HDMIInVideoFrameRate>(dsRes.frameRate);
        videoPortResolution.interlaced       = dsRes.interlaced;
        LOGINFO("GetHDMIVideoMode (AIDL): VIC=%d", vic);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetHDMIVersion(const HDMIInPort port, HDMIInCapabilityVersion& capabilityVersion) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        sp<IHDMIInput> hi;
        {
            std::lock_guard<std::mutex> lk(m_aidlMutex);
            auto it = m_aidlPorts.find((int)hdmiPort);
            if (it == m_aidlPorts.end() || !it->second.hdmiInput) return WPEFramework::Core::ERROR_GENERAL;
            hi = it->second.hdmiInput;
        }
        Capabilities caps;
        if (!hi->getCapabilities(&caps).isOk()) return WPEFramework::Core::ERROR_GENERAL;
        dsHdmiMaxCapabilityVersion_t capversion = HDMI_COMPATIBILITY_VERSION_14;
        for (const auto& v : caps.supportedVersions) {
            if (v == HDMIVersion::HDMI_2_1 && capversion < HDMI_COMPATIBILITY_VERSION_21)
                capversion = HDMI_COMPATIBILITY_VERSION_21;
            else if (v == HDMIVersion::HDMI_2_0 && capversion < HDMI_COMPATIBILITY_VERSION_20)
                capversion = HDMI_COMPATIBILITY_VERSION_20;
        }
        capabilityVersion = static_cast<HDMIInCapabilityVersion>(capversion);
        LOGINFO("GetHDMIVersion: port=%d, version=%d (AIDL)", (int)hdmiPort, capversion);
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetVRRSupport(const HDMIInPort port, const bool vrrSupport) override
    {
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        if (hdmiPort < dsHDMI_IN_PORT_MAX) {
            LOGINFO("SetVRRSupport: checking edidversion of port %d: %d", (int)hdmiPort, m_edidversion[hdmiPort]);
            if (m_edidversion[hdmiPort] == HDMI_EDID_VER_20) {
                updateVRRBitValuesInPersistence(hdmiPort, vrrSupport);
                m_vrrsupport[hdmiPort] = vrrSupport;
                LOGINFO("SetVRRSupport: port=%d, vrrSupport=%d (AIDL)", (int)hdmiPort, vrrSupport);
                return WPEFramework::Core::ERROR_NONE;
            }
            LOGINFO("EDID version is not 2.0, cannot set VRR support for port %d", (int)hdmiPort);
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }
        return WPEFramework::Core::ERROR_GENERAL;
    }

    uint32_t GetVRRSupport(const HDMIInPort port, bool& vrrSupport) override
    {
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        if (hdmiPort < dsHDMI_IN_PORT_MAX) {
            vrrSupport = m_vrrsupport[hdmiPort];
            LOGINFO("GetVRRSupport: port=%d, vrrSupport=%d", (int)hdmiPort, vrrSupport);
            return WPEFramework::Core::ERROR_NONE;
        }
        return WPEFramework::Core::ERROR_GENERAL;
    }

    uint32_t GetVRRStatus(const HDMIInPort port, HDMIInVRRStatus& vrrStatus) override
    {
        if (!ensureAidlService()) return WPEFramework::Core::ERROR_UNAVAILABLE;
        dsHdmiInPort_t hdmiPort = static_cast<dsHdmiInPort_t>(port);
        std::lock_guard<std::mutex> lk(m_aidlMutex);
        auto it = m_aidlPorts.find((int)hdmiPort);
        if (it == m_aidlPorts.end()) return WPEFramework::Core::ERROR_GENERAL;
        const AidlPortCtx& ctx = it->second;
        dsVRRType_t vrrType = ctx.vrrActive
            ? (ctx.vrrFrameRate > 0.0 ? dsVRR_AMD_FREESYNC : dsVRR_HDMI_VRR)
            : dsVRR_NONE;
        vrrStatus.vrrType = static_cast<HDMIInVRRType>(vrrType);
        vrrStatus.vrrFreeSyncFramerateHz = ctx.vrrActive ? ctx.vrrFrameRate : 0.0;
        LOGINFO("GetVRRStatus: port=%d, vrrType=%d (AIDL)", (int)hdmiPort, vrrType);
        return WPEFramework::Core::ERROR_NONE;
    }

    static bool IsHdmiInAIDLServiceAvailable()
    {
        LOGINFO("dHdmiInAIDLImpl: Checking AIDL HAL availability");
        try {
            ProcessState::self()->startThreadPool();
            sp<android::IServiceManager> sm = defaultServiceManager();
            if (!sm) return false;
            // checkService is non-blocking, unlike getService which waits with timeout.
            sp<android::IBinder> binder = sm->checkService(
                String16(IHDMIInputManager::serviceName().c_str()));
            bool available = (binder != nullptr);
            LOGINFO("dHdmiInAIDLImpl: AIDL service %s", available ? "available" : "not available");
            return available;
        } catch (...) {
            LOGERR("dHdmiInAIDLImpl: Exception checking AIDL availability");
            return false;
        }
    }
};
