"""
/**
 * @file TCID12_PortEnumerationConsistency.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID12_PortEnumerationConsistency
 * @details Scenario: a client discovers the HDMI input topology before showing a
 *          source-selection UI. Cross-validates numberOfInputs against
 *          getInputDevices and asserts the device list is internally consistent:
 *            - device count == numberOfInputs
 *            - ids are the contiguous range 0..n-1
 *            - each locator ends with its own device id
 *            - connected is boolean for every port
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - Both APIs agree on the port topology.
 *
 * @pass_criteria
 *  - Count, ids, locators and flags all consistent; run_test() returns True.
 *
 * @failure_criteria
 *  - Any mismatch between the two APIs or a malformed entry.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_number_of_inputs, parse_input_devices


def run_test():
    start_time = time.perf_counter()

    log_info("Step 1: numberOfInputs")
    count_resp = send_curl_command(AVInputApis.number_of_inputs)
    log_warning(f"Response: {count_resp}")
    count = parse_number_of_inputs(count_resp)
    if count is None:
        log_error("TCID12_PortEnumerationConsistency Failed ❌ (numberOfInputs invalid)")
        return False
    if count <= 0:
        log_error("TCID12_PortEnumerationConsistency Failed ❌ (device reports 0 HDMI input ports)")
        return False
    log_success(f"✅ numberOfInputs = {count}")

    log_info("Step 2: getInputDevices (HDMI)")
    dev_resp = send_curl_command(AVInputApis.get_input_devices(AVInputApis.TYPE_HDMI))
    log_warning(f"Response: {dev_resp}")
    devices = parse_input_devices(dev_resp)
    if devices is None:
        log_error("TCID12_PortEnumerationConsistency Failed ❌ (device list invalid)")
        return False

    log_info("Step 3: cross-validating topology")
    if len(devices) != count:
        log_error(
            f"TCID12_PortEnumerationConsistency Failed ❌ "
            f"(device count {len(devices)} != numberOfInputs {count})"
        )
        return False

    ids = sorted(d.get("id") for d in devices if isinstance(d, dict))
    if ids != list(range(count)):
        log_error(f"TCID12_PortEnumerationConsistency Failed ❌ (ids not contiguous 0..n-1: {ids})")
        return False

    for dev in devices:
        locator = str(dev.get("locator", ""))
        if not locator.endswith(f"/{dev.get('id')}"):
            log_error(
                f"TCID12_PortEnumerationConsistency Failed ❌ "
                f"(locator '{locator}' does not match id {dev.get('id')})"
            )
            return False
        if not isinstance(dev.get("connected"), bool):
            log_error("TCID12_PortEnumerationConsistency Failed ❌ (connected not boolean)")
            return False

    log_success(f"✅ Topology consistent across both APIs: ids {ids}")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID12_PortEnumerationConsistency Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
