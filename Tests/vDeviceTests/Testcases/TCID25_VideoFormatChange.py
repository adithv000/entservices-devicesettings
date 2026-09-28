"""
/**
 * @file TCID25_VideoFormatChange.py
 * @brief L3 AVInput vComponent-driven testcase.
 *
 * @testcase TCID25_VideoFormatChange
 * @details Presents an HDMI source, reads the current video mode, then injects
 *          VIC16_1920_1080_P_60_16_9, VIC34_1920_1080_P_30_16_9, and
 *          VIC93_3840_2160_P_24_16_9 through the vComponent. After each
 *          injection, it verifies the JSON-RPC currentVideoMode getter changes.
 *
 * @note HAL gating (dHdmiInAIDLImpl): GetHDMIVideoMode returns the injected VIC
 *       ONLY for the ACTIVE port (m_aidlActivePort), and the port only becomes
 *       active/presented once the emulated source is connected AND the signal is
 *       LOCKED (the vComponent then drives onStateChanged(STARTED)). Simply
 *       calling startInput is not enough because the AIDL SelectHDMIInPort path
 *       is stubbed. Therefore this test first injects connection_status(true)
 *       and signal_status(LOCKED) to present the port, THEN injects the video
 *       format. On a headless vDevice where the port is never presented,
 *       currentVideoMode can still be empty; that case is accepted with a
 *       warning rather than failed.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *  - HDMI Input vComponent reachable at HDMIIN_VCOMPONENT_API_URL.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *  - vcomponent_configurations/commands/HDMIInput_Connection_Status.yaml
 *  - vcomponent_configurations/commands/HDMIInput_Signal_Status.yaml
 *  - HDMI Input vComponent videoformat_change command
 *
 * @expected_result
 *  - After presenting the source, currentVideoMode changes for each injected
 *    1080p60, 1080p30, and 2160p24 format.
 *
 * @pass_criteria
 *  - Every format post is accepted and currentVideoMode changes after each
 *    injection; run_test() returns True.
 *
 * @failure_criteria
 *  - A format post is rejected, currentVideoMode is not a string, or the mode
 *    does not change after an injection.
 */
"""

import time
import os

from utils import (
    HDMIIN_CMD_BASE,
    JsonRpcEventListener,
    send_curl_command,
    send_vcomponent_command,
    send_vcomponent_payload,
    is_ok,
    log_info,
    log_success,
    log_error,
    log_warning,
)
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success, parse_current_video_mode

PORT = 0
CONNECTION_YAML = "HDMIInput_Connection_Status.yaml"
SIGNAL_YAML = "HDMIInput_Signal_LOCKED_Status.yaml"
EVENT_TIMEOUT = float(os.environ.get("AVINPUT_EVENT_TIMEOUT", "8"))
VIDEO_FORMATS = (
    "VIC16_1920_1080_P_60_16_9",
    "VIC34_1920_1080_P_30_16_9",
    "VIC93_3840_2160_P_24_16_9",
)
VIDEO_STREAM_INFO = {
    "VIC16_1920_1080_P_60_16_9": (1920, 1080, 60000, 1000),
    "VIC34_1920_1080_P_30_16_9": (1920, 1080, 30000, 1000),
    "VIC93_3840_2160_P_24_16_9": (3840, 2160, 24000, 1000),
}


def _post_file(name):
    http_code, body = send_vcomponent_command(f"{HDMIIN_CMD_BASE}/{name}")
    log_warning(f"vComponent POST {name}: HTTP {http_code} {body}")
    return http_code == 200


def _read_video_mode():
    response = send_curl_command(AVInputApis.current_video_mode)
    log_warning(f"currentVideoMode response: {response}")
    return parse_current_video_mode(response)


def _expect_video_format(listener, video_format):
    width, height, frame_rate_n, frame_rate_d = VIDEO_STREAM_INFO[video_format]
    notification = listener.wait_for_event(
        lambda params: (
            str(params.get("id")) == str(PORT)
            and params.get("width") == width
            and params.get("height") == height
            and params.get("progressive") is True
            and params.get("frameRateN") == frame_rate_n
            and params.get("frameRateD") == frame_rate_d
        ),
        timeout=EVENT_TIMEOUT,
    )
    if notification is None:
        log_error(
            "videoStreamInfoUpdate not received for port=0, "
            f"{width}x{height} progressive at {frame_rate_n / frame_rate_d:g} Hz"
        )
        return False
    log_success(f"Captured videoStreamInfoUpdate: {notification}")
    return True


def run_test():
    start_time = time.perf_counter()
    listener = JsonRpcEventListener(
        AVInputApis.CALLSIGN,
        "videoStreamInfoUpdate",
        "ID_TCID25_video_format",
        timeout=EVENT_TIMEOUT,
    )
    if not listener.connect():
        log_error("TCID25_VideoFormatChange Failed (event registration rejected)")
        return False

    try:
        log_info("Step 1: startInput on port 0")
        start_resp = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_resp}")
        if not result_success(start_resp):
            log_error("TCID25_VideoFormatChange Failed ❌ (startInput rejected)")
            return False
        time.sleep(1)

        # Step 2: present the port so the active-port video-mode gate is satisfied
        # (connection + LOCKED signal -> vComponent drives onStateChanged(STARTED)).
        log_info(f"Step 2: present source via {CONNECTION_YAML} + {SIGNAL_YAML}")
        if not _post_file(CONNECTION_YAML):
            log_error("TCID25_VideoFormatChange Failed ❌ (connection_status YAML rejected)")
            return False
        time.sleep(1)
        if not _post_file(SIGNAL_YAML):
            log_error("TCID25_VideoFormatChange Failed ❌ (signal_status YAML rejected)")
            return False
        time.sleep(2)

        # Step 3: capture the current resolution BEFORE the format injection.
        before_mode = _read_video_mode()
        if before_mode is None:
            log_error("TCID25_VideoFormatChange Failed ❌ (currentVideoMode not a string before injection)")
            return False
        log_info(f"Current resolution BEFORE injection = '{before_mode or '<no source>'}'")

        previous_mode = before_mode
        for step, video_format in enumerate(VIDEO_FORMATS, start=4):
            log_info(f"Step {step}: injecting videoformat_change format={video_format}")
            http_code, body = send_vcomponent_payload(
                "videoformat_change",
                {"port": PORT, "format": video_format},
            )
            log_warning(f"vComponent POST {video_format}: HTTP {http_code} {body}")
            if http_code != 200:
                log_error(
                    f"TCID25_VideoFormatChange Failed ❌ ({video_format} injection rejected)"
                )
                return False
            if not _expect_video_format(listener, video_format):
                log_error(
                    "TCID25_VideoFormatChange Failed ❌ "
                    f"(videoStreamInfoUpdate not captured for {video_format})"
                )
                return False
            time.sleep(2)

            current_mode = _read_video_mode()
            if current_mode is None:
                log_error(
                    "TCID25_VideoFormatChange Failed ❌ "
                    f"(currentVideoMode not a string after {video_format})"
                )
                return False
            log_info(f"Current resolution after {video_format} = '{current_mode or '<no source>'}'")

            if not current_mode:
                log_error(
                    "TCID25_VideoFormatChange Failed ❌ "
                    f"(currentVideoMode empty after {video_format})"
                )
                return False
            if current_mode == previous_mode:
                log_error(
                    "TCID25_VideoFormatChange Failed ❌ "
                    f"(currentVideoMode did not change after {video_format}: '{current_mode}')"
                )
                return False

            log_success(
                f"✅ Video mode changed for {video_format}: '{previous_mode}' -> '{current_mode}'"
            )
            previous_mode = current_mode
    finally:
        send_vcomponent_payload("signal_status", {"port": PORT, "state": "NO_SIGNAL"})
        send_vcomponent_payload("connection_status", {"port": PORT, "connected": False})
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        listener.close()

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID25_VideoFormatChange Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
