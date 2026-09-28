"""
/**
 * @file TCID14_EdidProvisioningWorkflow.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID14_EdidProvisioningWorkflow
 * @details Scenario: a provisioning agent reads the factory EDID, switches the
 *          port to EDID 2.0, writes an EDID payload back, and confirms the port
 *          still serves a readable EDID. Sequence:
 *            readEDID -> setEdidVersion(HDMI2.0) -> getEdidVersion
 *            -> writeEDID(original) -> readEDID -> restore original version
 *          Validates the EDID is base64-decodable and of a legal EDID length
 *          (128-byte blocks) as returned by the device.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - EDID round-trips and the version set/get round-trip matches.
 *
 * @pass_criteria
 *  - All steps succeed and the EDID decodes to a 128-byte multiple.
 *
 * @failure_criteria
 *  - Any step rejected, EDID undecodable, or version mismatch.
 */
"""

import time
import os
import base64

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success, parse_edid, parse_edid_version

PORT = 0


def _decoded_len(edid_b64):
    try:
        return len(base64.b64decode(edid_b64))
    except Exception:
        return None


def run_test():
    start_time = time.perf_counter()

    log_info("Step 1: readEDID (capture factory EDID)")
    read_resp = send_curl_command(AVInputApis.read_edid(PORT))
    log_warning(f"readEDID response: {read_resp}")
    original_edid = parse_edid(read_resp)
    if not original_edid:
        log_error("TCID14_EdidProvisioningWorkflow Failed ❌ (readEDID returned no EDID)")
        return False

    decoded_len = _decoded_len(original_edid)
    if decoded_len is None:
        log_error("TCID14_EdidProvisioningWorkflow Failed ❌ (EDID is not valid base64)")
        return False
    if decoded_len == 0 or decoded_len % 128 != 0:
        log_error(
            f"TCID14_EdidProvisioningWorkflow Failed ❌ "
            f"(EDID length {decoded_len} is not a 128-byte multiple)"
        )
        return False
    log_success(f"✅ Factory EDID read: {decoded_len} bytes ({decoded_len // 128} block(s))")

    original_version = parse_edid_version(send_curl_command(AVInputApis.get_edid_version(PORT)))
    log_info(f"Baseline EDID version: {original_version}")

    try:
        log_info("Step 2: setEdidVersion(HDMI2.0)")
        set_resp = send_curl_command(AVInputApis.set_edid_version(PORT, AVInputApis.EDID_VERSION_20))
        log_warning(f"setEdidVersion response: {set_resp}")
        if not result_success(set_resp):
            log_error("TCID14_EdidProvisioningWorkflow Failed ❌ (setEdidVersion rejected)")
            return False

        log_info("Step 3: getEdidVersion (verify)")
        ver_resp = send_curl_command(AVInputApis.get_edid_version(PORT))
        log_warning(f"getEdidVersion response: {ver_resp}")
        if parse_edid_version(ver_resp) != AVInputApis.EDID_VERSION_20:
            log_error("TCID14_EdidProvisioningWorkflow Failed ❌ (EDID version did not persist)")
            return False
        log_success("✅ EDID version set to HDMI2.0 and verified")

        log_info("Step 4: writeEDID (write the captured EDID back)")
        write_resp = send_curl_command(AVInputApis.write_edid(PORT, original_edid))
        log_warning(f"writeEDID response: {write_resp}")
        if not result_success(write_resp):
            log_error("TCID14_EdidProvisioningWorkflow Failed ❌ (writeEDID rejected)")
            return False
        log_success("✅ writeEDID accepted")

        log_info("Step 5: readEDID (confirm port still serves a valid EDID)")
        final_resp = send_curl_command(AVInputApis.read_edid(PORT))
        log_warning(f"readEDID response: {final_resp}")
        final_edid = parse_edid(final_resp)
        if not final_edid or _decoded_len(final_edid) is None:
            log_error("TCID14_EdidProvisioningWorkflow Failed ❌ (post-write EDID unreadable)")
            return False
        log_success("✅ EDID readable after write")
    finally:
        if original_version in (AVInputApis.EDID_VERSION_14, AVInputApis.EDID_VERSION_20):
            send_curl_command(AVInputApis.set_edid_version(PORT, original_version))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID14_EdidProvisioningWorkflow Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
