"""
AIDL HDMI Input callback and format-mapping coverage test.

The virtual HDMI controller injects the same events a physical source would
produce, allowing the DeviceSettings AIDL listener paths to be exercised on a
headless vDevice.
"""

import os
import time

import AVInput_Curl as AVInputApis
from AVInput_Helpers import get_device_connected, result_success
from utils import (
    JsonRpcEventListener,
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_curl_command,
    send_vcomponent_payload,
)

PORT = 0
EVENT_TIMEOUT = float(os.environ.get("AVINPUT_EVENT_TIMEOUT", "8"))
SIGNAL_STATUS_BY_STATE = {
    "NO_SIGNAL": AVInputApis.SIGNAL_NO,
    "UNSTABLE": AVInputApis.SIGNAL_UNSTABLE,
    "NOT_SUPPORTED": AVInputApis.SIGNAL_NOT_SUPPORTED,
    "LOCKED": AVInputApis.SIGNAL_STABLE,
}


def _post(command, **params):
    http_code, body = send_vcomponent_payload(command, {"port": PORT, **params})
    log_warning(f"vComponent {command}: HTTP {http_code}, body={body!r}")
    if not 200 <= http_code < 300:
        log_error(f"vComponent rejected {command}: HTTP {http_code}")
        return False
    time.sleep(0.2)
    return True


def _avi_frame(content_type=None):
    frame = [0x82, 0x02, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    if content_type is not None:
        frame[6] = 0x80
        frame[8] = content_type << 4
    return frame


def _connect_listeners():
    listeners = {
        event: JsonRpcEventListener(
            AVInputApis.CALLSIGN,
            event,
            f"ID_TCID22_{event}",
            timeout=EVENT_TIMEOUT,
        )
        for event in (
            "onDevicesChanged",
            "onSignalChanged",
            "onInputStatusChanged",
            "aviContentTypeUpdate",
        )
    }
    for event, listener in listeners.items():
        if not listener.connect():
            log_error(f"TCID22_AidlEventCoverage Failed ({event} registration rejected)")
            for registered_listener in listeners.values():
                registered_listener.close()
            return None
    return listeners


def _expect_event(listener, event, predicate, description):
    notification = listener.wait_for_event(predicate, timeout=EVENT_TIMEOUT)
    if notification is None:
        log_error(f"{event} not received: {description}")
        return False
    log_success(f"Captured {event}: {notification}")
    return True


def run_test():
    listeners = _connect_listeners()
    if listeners is None:
        return False

    try:
        start_response = send_curl_command(AVInputApis.start_input(PORT))
        if not result_success(start_response):
            log_error(f"Unable to start HDMI input before AIDL stimulus: {start_response}")
            return False
        if not _expect_event(
            listeners["onInputStatusChanged"],
            "onInputStatusChanged",
            lambda params: (
                str(params.get("id")) == str(PORT)
                and params.get("status") == "started"
            ),
            "port=0, status=started",
        ):
            return False

        stimuli = [
        ("connection_status", {"connected": True}),
        ("signal_status", {"state": "NO_SIGNAL"}),
        ("signal_status", {"state": "UNSTABLE"}),
        ("signal_status", {"state": "NOT_SUPPORTED"}),
        ("signal_status", {"state": "LOCKED"}),
    ]

        # One VIC from every resolution/frame-rate branch, including 4:3 and interlaced.
        for vic in (
        "VIC1_640_480_P_60_4_3",
        "VIC17_720_576_P_50_4_3",
        "VIC4_1280_720_P_60_16_9",
        "VIC5_1920_1080_I_60_16_9",
        "VIC93_3840_2160_P_24_16_9",
        "VIC98_4096_2160_P_24_256_135",
        "VIC121_5120_2160_P_24_64_27",
        "VIC40_1920_1080_I_100_16_9",
        "VIC46_1920_1080_I_120_16_9",
        "VIC52_720_576_P_200_4_3",
        "VIC56_720_480_P_240_4_3",
        "VIC108_1280_720_P_48_16_9",
        "VIC61_1280_720_P_25_16_9",
        "VIC62_1280_720_P_30_16_9",
        "VIC205_7680_4320_P_48_64_27",
        ):
            stimuli.append(("videoformat_change", {"format": vic}))

        stimuli.extend([
        ("vrr_status", {
            "vrrActive": False, "M_CONST": False,
            "fastVActive": False, "frameRate": 0.0,
        }),
        ("vrr_status", {
            "vrrActive": True, "M_CONST": True,
            "fastVActive": False, "frameRate": 0.0,
        }),
        ("vrr_status", {
            "vrrActive": True, "M_CONST": True,
            "fastVActive": True, "frameRate": 120.0,
        }),
        ("aviinfo_frame", {"data": [0x82, 0x02]}),
        ("aviinfo_frame", {"data": [0x81, 0x02, 0x05] + [0x00] * 6}),
        ("aviinfo_frame", {"data": [0x82, 0x02, 0x04] + [0x00] * 6}),
        ("aviinfo_frame", {"data": [0x82, 0x02, 0x0D] + [0x00] * 6}),
        ("aviinfo_frame", {"data": _avi_frame()}),
        ])

        for content_type in range(4):
            stimuli.append(("aviinfo_frame", {"data": _avi_frame(content_type)}))

        stimuli.extend([
        ("audioinfo_frame", {
            "data": [0x84, 0x01, 0x0A, 0x70, 0x01, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
        }),
        ("spdinfo_frame", {
            "data": [0x83, 0x01, 0x19, 0x8E] + [0x00] * 24,
        }),
        ("drminfo_frame", {
            "data": [0x87, 0x01, 0x1A, 0x9A] + [0x00] * 26,
        }),
        ("vsifinfo_frame", {
            "data": [0x81, 0x01, 0x0D, 0xA7] + [0x00] * 13,
        }),
        ("hdcp_status", {
            "state": "AUTHENTICATED", "version": "VERSION_2_X",
        }),
        ])

        log_info(f"Injecting {len(stimuli)} AIDL HDMI events")
        for command, params in stimuli:
            if not _post(command, **params):
                return False
            if command == "connection_status":
                if not _expect_event(
                    listeners["onDevicesChanged"],
                    "onDevicesChanged",
                    lambda event_params: get_device_connected(
                        event_params.get("devices"), PORT
                    ) is params["connected"],
                    f"port=0, connected={params['connected']}",
                ):
                    return False
            elif command == "signal_status":
                expected_status = SIGNAL_STATUS_BY_STATE[params["state"]]
                if not _expect_event(
                    listeners["onSignalChanged"],
                    "onSignalChanged",
                    lambda event_params: (
                        str(event_params.get("id")) == str(PORT)
                        and event_params.get("signalStatus") == expected_status
                    ),
                    f"port=0, signalStatus={expected_status}",
                ):
                    return False
            elif command == "aviinfo_frame" and params["data"] == _avi_frame():
                if not _expect_event(
                    listeners["aviContentTypeUpdate"],
                    "aviContentTypeUpdate",
                    lambda event_params: (
                        str(event_params.get("id")) == str(PORT)
                        and event_params.get("aviContentType")
                        == AVInputApis.AVI_CONTENT_INVALID
                    ),
                    f"port=0, aviContentType={AVInputApis.AVI_CONTENT_INVALID}",
                ):
                    return False

        send_curl_command(AVInputApis.current_video_mode)
        send_curl_command(AVInputApis.get_vrr_frame_rate(PORT))
        for label, command in (
            ("getSPD", AVInputApis.get_spd(PORT)),
            ("getRawSPD", AVInputApis.get_raw_spd(PORT)),
        ):
            response = send_curl_command(command)
            if not is_ok(response):
                log_error(f"{label} failed after SPD InfoFrame injection: {response}")
                return False

        stop_response = send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        if not result_success(stop_response):
            log_error(f"Unable to stop HDMI input before state readback: {stop_response}")
            return False
        if not _expect_event(
            listeners["onInputStatusChanged"],
            "onInputStatusChanged",
            lambda params: (
                str(params.get("id")) == str(PORT)
                and params.get("status") == "stopped"
            ),
            "port=0, status=stopped",
        ):
            return False
        start_response = send_curl_command(AVInputApis.start_input(PORT))
        if not result_success(start_response):
            log_error(f"Unable to restart HDMI input for state readback: {start_response}")
            return False
        if not _expect_event(
            listeners["onInputStatusChanged"],
            "onInputStatusChanged",
            lambda params: (
                str(params.get("id")) == str(PORT)
                and params.get("status") == "started"
            ),
            "port=0, status=started after restart",
        ):
            return False
        if not is_ok(send_curl_command(AVInputApis.current_video_mode)):
            log_error("currentVideoMode failed after HDMI input restart")
            return False
    finally:
        _post("connection_status", connected=False)
        _post("signal_status", state="NO_SIGNAL")
        _post(
            "vrr_status",
            vrrActive=False,
            M_CONST=False,
            fastVActive=False,
            frameRate=0.0,
        )
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        for listener in listeners.values():
            listener.close()

    log_success("TCID22_AidlEventCoverage Passed")
    return True
