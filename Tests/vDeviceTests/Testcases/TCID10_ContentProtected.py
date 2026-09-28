"""
/**
 * @file TCID10_ContentProtected.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID10_ContentProtected
 * @details Validates contentProtected returns a boolean indicating whether the
 *          currently presented HDMI input content is HDCP protected.
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
 *  - Response contains a boolean isContentProtected field.
 *
 * @pass_criteria
 *  - Boolean present and run_test() returns True.
 *
 * @failure_criteria
 *  - Missing/non-boolean field, call failure, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_content_protected


def run_test():
    start_time = time.perf_counter()

    log_info("Executing contentProtected")
    response = send_curl_command(AVInputApis.content_protected)
    log_warning(f"Response: {response}")

    protected = parse_content_protected(response)
    if protected is None:
        log_error("TCID10_ContentProtected Failed ❌ (isContentProtected missing/non-boolean)")
        return False
    log_success(f"✅ isContentProtected = {protected}")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID10_ContentProtected Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
