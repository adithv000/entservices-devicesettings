"""
/**
 * @file Init_AVInput_Populate.py
 * @brief Suite initialization / precondition for the AVInput L3 vDevice suite.
 *
 * @testcase Init_AVInput_Populate
 * @details Establishes a known baseline before the test cases run: verifies the
 *          plugin is reachable and responds to numberOfInputs, enumerates the
 *          available HDMI input devices, and ensures no input is left presenting
 *          from a previous run by issuing a best-effort stopInput.
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
 *  - numberOfInputs returns a valid count and baseline is established.
 *
 * @pass_criteria
 *  - Baseline verification succeeds and run_test() returns True.
 *
 * @failure_criteria
 *  - Endpoint unreachable, JSON parse failure, or run_test() returns False.
 */
"""

import time

from utils import send_curl_command, log_info, log_success, log_warning, log_error
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_number_of_inputs, parse_input_devices


def run_test():
    log_info("AVInput suite initialization: verifying plugin reachability")

    count_resp = send_curl_command(AVInputApis.number_of_inputs)
    log_warning(f"numberOfInputs response: {count_resp}")
    count = parse_number_of_inputs(count_resp)
    if count is None:
        log_error("Init failed: numberOfInputs did not return a valid count")
        return False
    log_info(f"  HDMI input ports available: {count}")

    devices_resp = send_curl_command(AVInputApis.get_input_devices(AVInputApis.TYPE_HDMI))
    log_warning(f"getInputDevices response: {devices_resp}")
    devices = parse_input_devices(devices_resp)
    if devices is not None:
        log_info(f"  HDMI input devices: {devices}")

    # Best-effort: ensure nothing is presenting from a previous run.
    stop_resp = send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
    log_warning(f"stopInput (baseline) response: {stop_resp}")

    time.sleep(1)
    log_success("Suite initialization completed successfully")
    return True
