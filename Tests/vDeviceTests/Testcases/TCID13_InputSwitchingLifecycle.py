"""
/**
 * @file TCID13_InputSwitchingLifecycle.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID13_InputSwitchingLifecycle
 * @details Scenario: a user switches between HDMI sources from the input menu.
 *          Walks the full presentation lifecycle across every reported port:
 *            start(port 0) -> start(port 1) [direct switch, no stop]
 *            -> stop -> stop again (idempotency)
 *          Verifies the plugin accepts a direct source switch without requiring
 *          an intervening stop, and that a redundant stop is handled cleanly.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - Every start/stop in the sequence returns success:true.
 *
 * @pass_criteria
 *  - Full lifecycle completes without error; run_test() returns True.
 *
 * @failure_criteria
 *  - Any start/stop rejected or the plugin becomes unresponsive.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success, parse_number_of_inputs


def run_test():
    start_time = time.perf_counter()

    count = parse_number_of_inputs(send_curl_command(AVInputApis.number_of_inputs))
    if not count:
        log_error("TCID13_InputSwitchingLifecycle Failed ❌ (no HDMI input ports reported)")
        return False
    log_info(f"Ports available for switching: {count}")

    try:
        previous = None
        for port in range(count):
            label = f"switch to port {port}" if previous is not None else f"start port {port}"
            log_info(f"Step: {label} (direct switch, no intervening stop)")
            resp = send_curl_command(AVInputApis.start_input(port))
            log_warning(f"startInput(port={port}) response: {resp}")
            if not result_success(resp):
                log_error(f"TCID13_InputSwitchingLifecycle Failed ❌ ({label} rejected)")
                return False
            log_success(f"✅ {label} accepted")
            previous = port
            time.sleep(2)

        log_info("Step: stopInput")
        stop_resp = send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        log_warning(f"stopInput response: {stop_resp}")
        if not result_success(stop_resp):
            log_error("TCID13_InputSwitchingLifecycle Failed ❌ (stopInput rejected)")
            return False
        log_success("✅ stopInput accepted")

        log_info("Step: redundant stopInput (idempotency)")
        stop_again = send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        log_warning(f"stopInput (again) response: {stop_again}")
        if not result_success(stop_again):
            log_error("TCID13_InputSwitchingLifecycle Failed ❌ (redundant stopInput not handled)")
            return False
        log_success("✅ Redundant stopInput handled cleanly")
    finally:
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID13_InputSwitchingLifecycle Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
