"""
/**
 * @file TCID05_ReadWriteEdid.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID05_ReadWriteEdid
 * @details Reads the current EDID for port 0, writes it back via writeEDID, and
 *          reads it again to confirm the write path is accepted and the value is
 *          retrievable (EDID authoring round-trip through the middleware).
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
 *  - readEDID returns a non-empty EDID; writeEDID is accepted; re-read succeeds.
 *
 * @pass_criteria
 *  - Read/write/re-read all succeed and run_test() returns True.
 *
 * @failure_criteria
 *  - Any step fails or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success, parse_edid

PORT = 0


def run_test():
    start_time = time.perf_counter()

    log_info("Executing readEDID on port 0")
    read_resp = send_curl_command(AVInputApis.read_edid(PORT))
    log_warning(f"readEDID response: {read_resp}")
    edid = parse_edid(read_resp)
    if not edid:
        log_error("TCID05_ReadWriteEdid Failed ❌ (readEDID returned empty/invalid EDID)")
        return False
    log_success(f"✅ readEDID returned {len(edid)} chars")

    log_info("Executing writeEDID (writing the value back) on port 0")
    write_resp = send_curl_command(AVInputApis.write_edid(PORT, edid))
    log_warning(f"writeEDID response: {write_resp}")
    if not result_success(write_resp):
        log_error("TCID05_ReadWriteEdid Failed ❌ (writeEDID not accepted)")
        return False
    log_success("✅ writeEDID accepted")

    log_info("Re-reading EDID after write")
    reread_resp = send_curl_command(AVInputApis.read_edid(PORT))
    log_warning(f"readEDID (after write) response: {reread_resp}")
    if not parse_edid(reread_resp):
        log_error("TCID05_ReadWriteEdid Failed ❌ (re-read EDID empty/invalid)")
        return False
    log_success("✅ EDID re-read after write")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID05_ReadWriteEdid Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
