"""
/**
 * @file TCID08_HdmiVersion.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID08_HdmiVersion
 * @details Validates getHdmiVersion(port 0) returns a non-empty HDMI capability
 *          version string (e.g. 2.0 / 2.1) for the port.
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
 *  - Response contains a non-empty HdmiCapabilityVersion string.
 *
 * @pass_criteria
 *  - Version present and non-empty and run_test() returns True.
 *
 * @failure_criteria
 *  - Missing/empty version, call failure, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_hdmi_version

PORT = 0


def run_test():
    start_time = time.perf_counter()

    log_info("Executing getHdmiVersion on port 0")
    response = send_curl_command(AVInputApis.get_hdmi_version(PORT))
    log_warning(f"Response: {response}")

    version = parse_hdmi_version(response)
    if not version:
        log_error("TCID08_HdmiVersion Failed ❌ (HdmiCapabilityVersion missing/empty)")
        return False
    log_success(f"✅ HDMI capability version = {version}")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID08_HdmiVersion Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
