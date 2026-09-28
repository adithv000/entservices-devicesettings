"""
/**
 * @file TCID01_NumberOfInputs.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID01_NumberOfInputs
 * @details Validates numberOfInputs returns a non-negative integer count of the
 *          HDMI input ports available on the device.
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
 *  - Response contains a numeric numberOfInputs field >= 0.
 *
 * @pass_criteria
 *  - Count present and non-negative and run_test() returns True.
 *
 * @failure_criteria
 *  - Missing/invalid count, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_number_of_inputs


def run_test():
    start_time = time.perf_counter()

    log_info("Executing numberOfInputs")
    response = send_curl_command(AVInputApis.number_of_inputs)
    log_warning(f"Response: {response}")

    count = parse_number_of_inputs(response)
    if count is None or count < 0:
        log_error("TCID01_NumberOfInputs Failed ❌ (numberOfInputs missing/invalid)")
        return False
    log_success(f"✅ numberOfInputs = {count}")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID01_NumberOfInputs Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
