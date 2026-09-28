"""
/**
 * @file TCID16_VrrProvisioning.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID16_VrrProvisioning
 * @details Scenario: enabling Variable Refresh Rate advertisement on an HDMI
 *          port so a games console negotiates VRR. Sequence:
 *            setEdidVersion(HDMI2.0) -> setVRRSupport(true) -> get (true)
 *            -> setVRRSupport(false) -> get (false) -> restore
 *          Verifies the VRR support bit round-trips in both directions and that
 *          the reported value always tracks the last write.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - getVRRSupport mirrors each setVRRSupport write.
 *
 * @pass_criteria
 *  - Both toggle directions verified; run_test() returns True.
 *
 * @failure_criteria
 *  - Any set rejected or get does not mirror the write.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success, parse_vrr_support, parse_edid_version

PORT = 0


def _toggle_vrr(enabled):
    set_resp = send_curl_command(AVInputApis.set_vrr_support(PORT, enabled))
    log_warning(f"setVRRSupport({enabled}) response: {set_resp}")
    if not result_success(set_resp):
        return False
    get_resp = send_curl_command(AVInputApis.get_vrr_support(PORT))
    log_warning(f"getVRRSupport response: {get_resp}")
    return parse_vrr_support(get_resp) is enabled


def run_test():
    start_time = time.perf_counter()

    original_version = parse_edid_version(send_curl_command(AVInputApis.get_edid_version(PORT)))
    original_vrr = parse_vrr_support(send_curl_command(AVInputApis.get_vrr_support(PORT)))
    log_info(f"Baseline: edidVersion={original_version} vrrSupport={original_vrr}")

    try:
        log_info("Step 1: select EDID 2.0 (prerequisite for VRR advertisement)")
        ver_resp = send_curl_command(AVInputApis.set_edid_version(PORT, AVInputApis.EDID_VERSION_20))
        log_warning(f"setEdidVersion(HDMI2.0) response: {ver_resp}")
        if not result_success(ver_resp):
            log_error("TCID16_VrrProvisioning Failed ❌ (could not select EDID 2.0)")
            return False

        log_info("Step 2: advertise VRR support")
        if not _toggle_vrr(True):
            log_error("TCID16_VrrProvisioning Failed ❌ (VRR support not enabled)")
            return False
        log_success("✅ VRR advertised in EDID")

        log_info("Step 3: withdraw VRR support")
        if not _toggle_vrr(False):
            log_error("TCID16_VrrProvisioning Failed ❌ (VRR support not disabled)")
            return False
        log_success("✅ VRR withdrawn from EDID")
    finally:
        if isinstance(original_vrr, bool):
            send_curl_command(AVInputApis.set_vrr_support(PORT, original_vrr))
        if original_version in (AVInputApis.EDID_VERSION_14, AVInputApis.EDID_VERSION_20):
            send_curl_command(AVInputApis.set_edid_version(PORT, original_version))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID16_VrrProvisioning Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
