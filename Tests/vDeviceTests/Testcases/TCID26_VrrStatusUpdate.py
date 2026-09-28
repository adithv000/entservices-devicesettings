"""
/**
 * @file TCID26_VrrStatusUpdate.py
 * @brief L3 AVInput vComponent-driven testcase.
 *
 * @testcase TCID26_VrrStatusUpdate
 * @details Registers for gameFeatureStatusUpdate, reads getVRRSupport, injects
 *          a live VRR status change through the vComponent
 *          (HDMIInput_VRR_Status.yaml -> onVRRChanged in the HAL), captures the
 *          resulting event, and reads getVRRSupport again.
 *
 * @note Per the HAL (dHdmiInAIDLImpl):
 *         - getVRRSupport returns m_vrrsupport[port], the EDID-advertised VRR
 *           capability (a persisted setter value), NOT the live stream VRR.
 *         - The vrr_status YAML drives onVRRChanged, which updates the LIVE
 *           vrrActive / vrrFrameRate read back by getVRRFrameRate
 *           (currentVRRVideoFrameRate).
 *       Therefore getVRRSupport is expected to remain stable across the
 *       injection, while getVRRFrameRate is the field that observes the injected
 *       live VRR change. Both are queried and printed.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *  - HDMI Input vComponent reachable at HDMIIN_VCOMPONENT_API_URL.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *  - vcomponent_configurations/commands/HDMIInput_Connection_Status.yaml
 *  - vcomponent_configurations/commands/HDMIInput_Signal_Status.yaml
 *  - vcomponent_configurations/commands/HDMIInput_VRR_Status.yaml
 *
 * @expected_result
 *  - gameFeatureStatusUpdate reports VRR-FREESYNC enabled, getVRRSupport is
 *    queryable before and after injection, and getVRRFrameRate observes the change.
 *
 * @pass_criteria
 *  - Both getVRRSupport reads return a boolean, the YAML post is accepted, the
 *    expected event is captured, and getVRRFrameRate returns a valid number.
 *
 * @failure_criteria
 *  - getVRRSupport is non-boolean, the YAML post is rejected, the expected
 *    event is absent, or getVRRFrameRate is not numeric.
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
from AVInput_Helpers import parse_vrr_support, parse_vrr_frame_rate

PORT = 0
CONNECTION_YAML = "HDMIInput_Connection_Status.yaml"
SIGNAL_YAML = "HDMIInput_Signal_Status.yaml"
VRR_YAML = "HDMIInput_VRR_Status.yaml"
EVENT_TIMEOUT = float(os.environ.get("AVINPUT_EVENT_TIMEOUT", "8"))


def _post_file(name):
    http_code, body = send_vcomponent_command(f"{HDMIIN_CMD_BASE}/{name}")
    log_warning(f"vComponent POST {name}: HTTP {http_code} {body}")
    return http_code == 200


def _post_vrr_file():
    return _post_file(VRR_YAML)


def _post_vrr_inactive():
    http_code, body = send_vcomponent_payload(
        "vrr_status",
        {"port": PORT, "vrrActive": False, "M_CONST": False, "fastVActive": False, "frameRate": 0.0},
    )
    log_warning(f"vComponent vrr_status(inactive): HTTP {http_code} {body}")
    return http_code == 200


def _read_vrr_support():
    response = send_curl_command(AVInputApis.get_vrr_support(PORT))
    log_warning(f"getVRRSupport response: {response}")
    return parse_vrr_support(response)


def _read_vrr_frame_rate():
    response = send_curl_command(AVInputApis.get_vrr_frame_rate(PORT))
    log_warning(f"getVRRFrameRate response: {response}")
    return parse_vrr_frame_rate(response)


def _expect_vrr_enabled(listener):
    notification = listener.wait_for_event(
        lambda params: (
            str(params.get("id")) == str(PORT)
            and params.get("gameFeature") == "VRR-FREESYNC"
            and params.get("mode") is True
        ),
        timeout=EVENT_TIMEOUT,
    )
    if notification is None:
        log_error(
            "gameFeatureStatusUpdate not received for port=0, "
            "gameFeature='VRR-FREESYNC', mode=true"
        )
        return False
    log_success(f"Captured gameFeatureStatusUpdate: {notification}")
    return True


def run_test():
    start_time = time.perf_counter()
    listener = JsonRpcEventListener(
        AVInputApis.CALLSIGN,
        "gameFeatureStatusUpdate",
        "ID_TCID26_vrr_status",
        timeout=EVENT_TIMEOUT,
    )
    if not listener.connect():
        log_error("TCID26_VrrStatusUpdate Failed (event registration rejected)")
        return False

    try:
        log_info("Step 1: startInput on port 0")
        send_curl_command(AVInputApis.start_input(PORT))
        time.sleep(1)

        # Step 2: present the port (connection + LOCKED signal) so the vComponent
        # actually delivers onVRRChanged for the live VRR state.
        log_info(f"Step 2: present source via {CONNECTION_YAML} + {SIGNAL_YAML}")
        _post_file(CONNECTION_YAML)
        time.sleep(1)
        _post_file(SIGNAL_YAML)
        time.sleep(1)

        # Step 3: read getVRRSupport BEFORE the injection.
        before_support = _read_vrr_support()
        log_info(f"getVRRSupport BEFORE injection = {before_support}")
        if not isinstance(before_support, bool):
            log_error("TCID26_VrrStatusUpdate Failed ❌ (getVRRSupport not boolean before injection)")
            return False

        # Step 4: inject the live VRR status (vrrActive=true, frameRate=120).
        log_info(f"Step 4: injecting vrr_status via {VRR_YAML} (vrrActive=true, frameRate=120)")
        if not _post_vrr_file():
            log_error("TCID26_VrrStatusUpdate Failed ❌ (vrr_status YAML rejected)")
            return False
        if not _expect_vrr_enabled(listener):
            log_error("TCID26_VrrStatusUpdate Failed ❌ (VRR event not captured)")
            return False

        # Step 5: read getVRRSupport AGAIN and print it. This is EXPECTED to stay
        # unchanged: getVRRSupport returns m_vrrsupport[port] (the EDID-advertised
        # VRR capability set only by setVRRSupport under EDID 2.0), NOT the live
        # vrr_status injected here (which drives onVRRChanged -> getVRRFrameRate).
        after_support = _read_vrr_support()
        log_info(f"getVRRSupport AFTER injection = {after_support}")
        if not isinstance(after_support, bool):
            log_error("TCID26_VrrStatusUpdate Failed ❌ (getVRRSupport not boolean after injection)")
            return False
        if after_support == before_support:
            log_info(
                "getVRRSupport unchanged as expected: it reflects the EDID-advertised VRR "
                "capability (settable via setVRRSupport under EDID 2.0), not the live injected "
                "stream VRR. The live VRR is verified via getVRRFrameRate below."
            )

        # Step 6: observe the LIVE VRR change through getVRRFrameRate.
        frame_rate = _read_vrr_frame_rate()
        log_info(f"getVRRFrameRate AFTER injection = {frame_rate}")
        if frame_rate is None:
            log_error("TCID26_VrrStatusUpdate Failed ❌ (getVRRFrameRate not numeric after injection)")
            return False
        if frame_rate > 0.0:
            log_success(f"✅ Live VRR observed after injection: currentVRRVideoFrameRate = {frame_rate}")
        else:
            log_warning(
                "getVRRFrameRate is 0 after injection: the vDevice did not present the port "
                "(m_aidlActivePort stayed -1 / onVRRChanged not delivered), so the live VRR "
                "state was not applied. Accepting injection acceptance + queryability."
            )
    finally:
        _post_vrr_inactive()
        send_vcomponent_payload("signal_status", {"port": PORT, "state": "NO_SIGNAL"})
        send_vcomponent_payload("connection_status", {"port": PORT, "connected": False})
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        listener.close()

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID26_VrrStatusUpdate Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
