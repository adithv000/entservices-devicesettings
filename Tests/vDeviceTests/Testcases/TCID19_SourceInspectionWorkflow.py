"""
/**
 * @file TCID19_SourceInspectionWorkflow.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID19_SourceInspectionWorkflow
 * @details Scenario: a diagnostics screen presents an HDMI source and gathers a
 *          full report about it in one pass. Sequence:
 *            startInput(port 0) -> getHdmiVersion -> getSPD -> getRawSPD
 *            -> currentVideoMode -> contentProtected -> stopInput
 *          Each field is type-checked rather than value-matched, because the
 *          reported values depend on the attached source (on a vDevice with no
 *          source, currentVideoMode is legitimately an empty string).
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - Every diagnostic field is returned with the correct type.
 *
 * @pass_criteria
 *  - All six queries succeed with well-typed values; run_test() returns True.
 *
 * @failure_criteria
 *  - Any query fails or returns a wrongly typed field.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import (
    result_success,
    parse_hdmi_version,
    parse_spd,
    parse_current_video_mode,
    parse_content_protected,
)

PORT = 0


def run_test():
    start_time = time.perf_counter()
    report = {}

    try:
        log_info("Step 1: startInput on port 0")
        start_resp = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_resp}")
        if not result_success(start_resp):
            log_error("TCID19_SourceInspectionWorkflow Failed ❌ (startInput rejected)")
            return False
        time.sleep(2)

        log_info("Step 2: getHdmiVersion")
        ver_resp = send_curl_command(AVInputApis.get_hdmi_version(PORT))
        log_warning(f"getHdmiVersion response: {ver_resp}")
        version = parse_hdmi_version(ver_resp)
        if not version:
            log_error("TCID19_SourceInspectionWorkflow Failed ❌ (HdmiCapabilityVersion missing)")
            return False
        report["hdmiVersion"] = version

        log_info("Step 3: getSPD (decoded)")
        spd_resp = send_curl_command(AVInputApis.get_spd(PORT))
        log_warning(f"getSPD response: {spd_resp}")
        spd = parse_spd(spd_resp)
        if spd is None:
            log_error("TCID19_SourceInspectionWorkflow Failed ❌ (getSPD missing HDMISPD)")
            return False
        report["spd"] = spd

        log_info("Step 4: getRawSPD")
        raw_resp = send_curl_command(AVInputApis.get_raw_spd(PORT))
        log_warning(f"getRawSPD response: {raw_resp}")
        raw_spd = parse_spd(raw_resp)
        if raw_spd is None:
            log_error("TCID19_SourceInspectionWorkflow Failed ❌ (getRawSPD missing HDMISPD)")
            return False
        report["rawSpdLen"] = len(raw_spd)

        log_info("Step 5: currentVideoMode")
        mode_resp = send_curl_command(AVInputApis.current_video_mode)
        log_warning(f"currentVideoMode response: {mode_resp}")
        mode = parse_current_video_mode(mode_resp)
        if mode is None:
            log_error("TCID19_SourceInspectionWorkflow Failed ❌ (currentVideoMode not a string)")
            return False
        report["videoMode"] = mode or "<no source>"

        log_info("Step 6: contentProtected")
        prot_resp = send_curl_command(AVInputApis.content_protected)
        log_warning(f"contentProtected response: {prot_resp}")
        protected = parse_content_protected(prot_resp)
        if protected is None:
            log_error("TCID19_SourceInspectionWorkflow Failed ❌ (isContentProtected not boolean)")
            return False
        report["contentProtected"] = protected

        log_success(f"✅ Source diagnostics report: {report}")
    finally:
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID19_SourceInspectionWorkflow Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
