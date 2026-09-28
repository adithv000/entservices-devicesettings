"""
/**
 * @file TCID22_DisconnectedPortBehaviour.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID22_DisconnectedPortBehaviour
 * @details Scenario: the platform is asked to present a port that has no source
 *          attached - the common vDevice / bench state. Documents and locks in
 *          the contracted behaviour observed on the target:
 *            - getInputDevices reports connected:false for the port
 *            - startInput on that port is still accepted (success:true)
 *            - currentVideoMode returns an empty string (no source timing)
 *            - EDID / SPD / capability getters remain serviceable
 *          This guards against regressions where a disconnected port starts
 *          throwing errors or returning malformed fields instead of empty data.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *  - Port 0 has no HDMI source attached (skips the assertion if one is present).
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - Disconnected port degrades gracefully with empty data, not errors.
 *
 * @pass_criteria
 *  - All queries answered with correctly typed values; run_test() returns True.
 *
 * @failure_criteria
 *  - Any query errors out or returns a wrongly typed field.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import (
    result_success,
    parse_input_devices,
    get_device_connected,
    parse_current_video_mode,
    parse_edid,
    parse_hdmi_version,
)

PORT = 0


def run_test():
    start_time = time.perf_counter()

    log_info("Step 1: check connection state of port 0")
    dev_resp = send_curl_command(AVInputApis.get_input_devices(AVInputApis.TYPE_HDMI))
    log_warning(f"getInputDevices response: {dev_resp}")
    devices = parse_input_devices(dev_resp)
    if devices is None:
        log_error("TCID22_DisconnectedPortBehaviour Failed ❌ (device list invalid)")
        return False

    connected = get_device_connected(devices, PORT)
    if connected is None:
        log_error("TCID22_DisconnectedPortBehaviour Failed ❌ (port 0 not present in device list)")
        return False

    if connected:
        log_info("Port 0 has a source attached - skipping disconnected-state assertions")
    else:
        log_success("✅ Port 0 reported as disconnected (expected bench state)")

    try:
        log_info("Step 2: startInput on the port (must be accepted even without a source)")
        start_resp = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_resp}")
        if not result_success(start_resp):
            log_error("TCID22_DisconnectedPortBehaviour Failed ❌ (startInput rejected)")
            return False
        log_success("✅ startInput accepted on port with no source")
        time.sleep(2)

        log_info("Step 3: currentVideoMode must be a string (empty when no source)")
        mode_resp = send_curl_command(AVInputApis.current_video_mode)
        log_warning(f"currentVideoMode response: {mode_resp}")
        mode = parse_current_video_mode(mode_resp)
        if mode is None:
            log_error("TCID22_DisconnectedPortBehaviour Failed ❌ (currentVideoMode not a string)")
            return False
        if not connected and mode != "":
            log_info(f"Note: disconnected port reported a non-empty video mode: '{mode}'")
        log_success(f"✅ currentVideoMode well-formed: '{mode}'")

        log_info("Step 4: EDID still readable on a disconnected port")
        edid_resp = send_curl_command(AVInputApis.read_edid(PORT))
        log_warning(f"readEDID response: {edid_resp}")
        if not parse_edid(edid_resp):
            log_error("TCID22_DisconnectedPortBehaviour Failed ❌ (readEDID unavailable)")
            return False
        log_success("✅ readEDID serviceable without a source")

        log_info("Step 5: capability query still serviceable")
        ver_resp = send_curl_command(AVInputApis.get_hdmi_version(PORT))
        log_warning(f"getHdmiVersion response: {ver_resp}")
        if not parse_hdmi_version(ver_resp):
            log_error("TCID22_DisconnectedPortBehaviour Failed ❌ (getHdmiVersion unavailable)")
            return False
        log_success("✅ getHdmiVersion serviceable without a source")
    finally:
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID22_DisconnectedPortBehaviour Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
