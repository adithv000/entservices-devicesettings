"""
/**
 * @file TCID10_InvalidParameterHandling.py
 * @brief L3 AVInput robustness testcase for malformed parameters.
 *
 * @testcase TCID10_InvalidParameterHandling
 * @details Sends requests with invalid parameter values that mirror the malformed
 *          curls in curl_extract.txt:
 *            - getEdid2AllmSupport  (portId="S")
 *            - setEdid2AllmSupport  (portId="f")
 *            - getRawSPD            (portId="foo")
 *            - getSPD              (portId="foo")
 *            - getInputDevices     (typeOfInput="ABCD")
 *          Verifies the plugin handles each request gracefully (returns a valid
 *          JSON-RPC response and does not drop the connection) and remains healthy
 *          afterwards by reading numberOfInputs.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py
 *  - AVInput_Curl.py
 *  - SuiteManager.py
 *
 * @expected_result
 *  - Each malformed request yields a parseable JSON-RPC response and the plugin
 *    stays responsive (numberOfInputs succeeds afterwards).
 *
 * @pass_criteria
 *  - All malformed requests return a valid response and the post-check succeeds.
 *
 * @failure_criteria
 *  - Any malformed request yields no response, or the plugin becomes unresponsive.
 */
"""

import time
import os

from utils import send_curl_command, is_ok, responded, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis


def run_test():
    start_time = time.perf_counter()

    malformed_calls = [
        ("getEdid2AllmSupport (portId=S)", AVInputApis.get_edid2_allm_support_invalid_port),
        ("setEdid2AllmSupport (portId=f)", AVInputApis.set_edid2_allm_support_invalid_port),
        ("getRawSPD (portId=foo)", AVInputApis.get_raw_spd_invalid_port),
        ("getSPD (portId=foo)", AVInputApis.get_spd_invalid_port),
        ("getInputDevices (typeOfInput=ABCD)", AVInputApis.get_input_devices_invalid_type),
        ("getHdmiVersion (portId=foo)", AVInputApis.get_hdmi_version_invalid_port),
        ("startInput (typeOfInput=INVALID_TYPE)", AVInputApis.start_input_invalid_type()),
        ("setVideoRectangle (negative bounds)", AVInputApis.set_video_rectangle_invalid_bounds()),
    ]

    for label, cmd in malformed_calls:
        log_info(f"Executing {label}")
        resp = send_curl_command(cmd)
        log_warning(f"Response: {resp}")
        if not responded(resp):
            log_error(f"TCID10_InvalidParameterHandling Failed ❌ (no response for {label})")
            return False
        log_success(f"✅ Handled gracefully: {label}")

    log_info("Verifying plugin is still responsive via numberOfInputs")
    health = send_curl_command(AVInputApis.number_of_inputs)
    log_warning(f"Response: {health}")
    if not is_ok(health):
        log_error("TCID10_InvalidParameterHandling Failed ❌ (plugin unresponsive after malformed input)")
        return False
    log_success("✅ Plugin responsive after malformed input")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID10_InvalidParameterHandling Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
