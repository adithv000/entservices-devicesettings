"""
/**
 * @file TCID06_Edid2AllmSupport.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID06_Edid2AllmSupport
 * @details Validates the EDID-2.0 gating of the ALLM-in-EDID bit: sets the EDID
 *          version to HDMI2.0, enables then disables setEdid2AllmSupport on port 0,
 *          and verifies getEdid2AllmSupport reflects each cached/persisted value.
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
 *  - getEdid2AllmSupport returns true after enable and false after disable.
 *
 * @pass_criteria
 *  - Both transitions verified and run_test() returns True.
 *
 * @failure_criteria
 *  - Set/get mismatch, call failure, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success, parse_allm_support, parse_edid_version

PORT = 0


def _set_allm_and_verify(enabled):
    set_resp = send_curl_command(AVInputApis.set_edid2_allm_support(PORT, enabled))
    log_warning(f"setEdid2AllmSupport({enabled}) response: {set_resp}")
    if not result_success(set_resp):
        return False
    get_resp = send_curl_command(AVInputApis.get_edid2_allm_support(PORT))
    log_warning(f"getEdid2AllmSupport response: {get_resp}")
    return parse_allm_support(get_resp) is enabled


def run_test():
    start_time = time.perf_counter()

    original_version = parse_edid_version(send_curl_command(AVInputApis.get_edid_version(PORT)))

    try:
        # ALLM-in-EDID is gated on EDID 2.0 in the middleware; select it first.
        ver_resp = send_curl_command(AVInputApis.set_edid_version(PORT, AVInputApis.EDID_VERSION_20))
        log_warning(f"setEdidVersion(HDMI2.0) response: {ver_resp}")
        if not result_success(ver_resp):
            log_error("TCID06_Edid2AllmSupport Failed ❌ (could not select EDID 2.0)")
            return False

        log_info("Enabling ALLM in EDID on port 0")
        if not _set_allm_and_verify(True):
            log_error("TCID06_Edid2AllmSupport Failed ❌ (ALLM not enabled under EDID 2.0)")
            return False
        log_success("✅ ALLM-in-EDID enabled")

        log_info("Disabling ALLM in EDID on port 0")
        if not _set_allm_and_verify(False):
            log_error("TCID06_Edid2AllmSupport Failed ❌ (ALLM not disabled under EDID 2.0)")
            return False
        log_success("✅ ALLM-in-EDID disabled")
    finally:
        if original_version in (AVInputApis.EDID_VERSION_14, AVInputApis.EDID_VERSION_20):
            send_curl_command(AVInputApis.set_edid_version(PORT, original_version))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID06_Edid2AllmSupport Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
