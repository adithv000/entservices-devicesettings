"""
/**
 * @file TCID04_EdidVersionRoundTrip.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID04_EdidVersionRoundTrip
 * @details Sets the HDMI EDID version on port 0 to HDMI2.0 then HDMI1.4 and
 *          verifies getEdidVersion reflects each value (set/get round-trip and
 *          middleware persistence of the selected version).
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
 *  - getEdidVersion returns the last value written for each set.
 *
 * @pass_criteria
 *  - Both round-trips match and run_test() returns True.
 *
 * @failure_criteria
 *  - Set/get mismatch, call failure, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success, parse_edid_version

PORT = 0


def _set_and_verify(version):
    set_resp = send_curl_command(AVInputApis.set_edid_version(PORT, version))
    log_warning(f"setEdidVersion({version}) response: {set_resp}")
    if not result_success(set_resp):
        return False
    get_resp = send_curl_command(AVInputApis.get_edid_version(PORT))
    log_warning(f"getEdidVersion response: {get_resp}")
    return parse_edid_version(get_resp) == version


def run_test():
    start_time = time.perf_counter()

    original = parse_edid_version(send_curl_command(AVInputApis.get_edid_version(PORT)))
    log_info(f"Baseline EDID version: {original}")

    try:
        log_info("Setting EDID version HDMI2.0")
        if not _set_and_verify(AVInputApis.EDID_VERSION_20):
            log_error("TCID04_EdidVersionRoundTrip Failed ❌ (HDMI2.0 round-trip mismatch)")
            return False
        log_success("✅ HDMI2.0 round-trip verified")

        log_info("Setting EDID version HDMI1.4")
        if not _set_and_verify(AVInputApis.EDID_VERSION_14):
            log_error("TCID04_EdidVersionRoundTrip Failed ❌ (HDMI1.4 round-trip mismatch)")
            return False
        log_success("✅ HDMI1.4 round-trip verified")
    finally:
        if original in (AVInputApis.EDID_VERSION_14, AVInputApis.EDID_VERSION_20):
            send_curl_command(AVInputApis.set_edid_version(PORT, original))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID04_EdidVersionRoundTrip Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
