"""
/**
 * @file TCID02_GetInputDevices.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID02_GetInputDevices
 * @details Validates getInputDevices(HDMI) returns a device list where each entry
 *          carries an id, a well-formed locator (hdmiin://.../deviceid/<id>), and a
 *          boolean connected flag.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py
 *  - AVInput_Curl.py
 *  - AVInput_Helpers.py
 *  - SuiteManager.py
 *
 * @expected_result
 *  - Response contains a devices list with valid id/locator/connected fields.
 *
 * @pass_criteria
 *  - Device list present and well-formed and run_test() returns True.
 *
 * @failure_criteria
 *  - Missing/invalid list, malformed entries, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_input_devices


def run_test():
    start_time = time.perf_counter()

    log_info("Executing getInputDevices (HDMI)")
    response = send_curl_command(AVInputApis.get_input_devices(AVInputApis.TYPE_HDMI))
    log_warning(f"Response: {response}")

    devices = parse_input_devices(response)
    if devices is None:
        log_error("TCID02_GetInputDevices Failed ❌ (devices list missing/invalid)")
        return False

    for dev in devices:
        if not isinstance(dev, dict):
            log_error("TCID02_GetInputDevices Failed ❌ (device entry not an object)")
            return False
        if "id" not in dev or "locator" not in dev or "connected" not in dev:
            log_error("TCID02_GetInputDevices Failed ❌ (device entry missing fields)")
            return False
        if not isinstance(dev.get("connected"), bool):
            log_error("TCID02_GetInputDevices Failed ❌ (connected flag not boolean)")
            return False
        if "hdmiin" not in str(dev.get("locator")):
            log_error("TCID02_GetInputDevices Failed ❌ (locator not an hdmiin URL)")
            return False

    log_success(f"✅ {len(devices)} HDMI device(s) reported")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID02_GetInputDevices Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
