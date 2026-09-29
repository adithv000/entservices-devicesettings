"""
/**
 * @file TCID29_InvalidPortStartStopInput.py
 * @brief L3 AVInput negative lifecycle testcase.
 *
 * @testcase TCID29_InvalidPortStartStopInput
 * @details Calls startInput with invalid port 10 and verifies that the request
 *          is rejected. It then calls stopInput and verifies that a valid input
 *          can still be started and stopped normally.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, SuiteManager.py
 *
 * @expected_result
 *  - startInput rejects port 10, while stopInput and subsequent valid start/stop
 *    requests succeed.
 *
 * @pass_criteria
 *  - The invalid start is rejected, recovery requests succeed, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - The invalid start is accepted or the plugin becomes unresponsive.
 */
"""

import os
import time

import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success
from utils import log_error, log_info, log_success, log_warning, responded, send_curl_command


VALID_PORT = 0
INVALID_PORT = 10


def run_test():
    start_time = time.perf_counter()

    try:
        log_info(f"Attempting to start HDMI input on invalid port {INVALID_PORT}")
        invalid_response = send_curl_command(AVInputApis.start_input(INVALID_PORT))
        log_warning(f"startInput(portId={INVALID_PORT}) response: {invalid_response}")
        if not responded(invalid_response):
            log_error("TCID29_InvalidPortStartStopInput Failed (invalid startInput received no response)")
            return False

        if result_success(invalid_response):
            log_error(f"TCID29_InvalidPortStartStopInput Failed (invalid port {INVALID_PORT} accepted)")
            return False
        log_success(f"startInput rejected invalid port {INVALID_PORT}")

        log_info("Calling stopInput after the invalid start request")
        stop_response = send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        log_warning(f"stopInput response: {stop_response}")
        if not result_success(stop_response):
            log_error("TCID29_InvalidPortStartStopInput Failed (stopInput rejected)")
            return False

        log_info(f"Starting HDMI input on valid port {VALID_PORT} after invalid request")
        recovery_response = send_curl_command(AVInputApis.start_input(VALID_PORT))
        log_warning(f"recovery startInput response: {recovery_response}")
        if not result_success(recovery_response):
            log_error("TCID29_InvalidPortStartStopInput Failed (valid start after invalid request failed)")
            return False
    finally:
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    message = "TCID29_InvalidPortStartStopInput Passed"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True