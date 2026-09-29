"""
/**
 * @file TCID20_NegativeAndBoundaryHandling.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID20_NegativeAndBoundaryHandling
 * @details Scenario: a misbehaving client sends malformed and out-of-range
 *          requests. The plugin must reject them without crashing and must stay
 *          serviceable for well-formed traffic afterwards. Exercises:
 *            - non-numeric portId  ("S" / "f" / "foo")
 *            - out-of-range portId (99) on a getter and on startInput
 *            - unknown typeOfInput ("ABCD")
 *            - invalid edidVersion ("HDMI9.9")
 *          Every request must return a parseable JSON-RPC body, and a health
 *          check must succeed at the end.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - All malformed requests are answered; plugin remains healthy.
 *
 * @pass_criteria
 *  - Every request answered and health check passes; run_test() returns True.
 *
 * @failure_criteria
 *  - Any request drops the connection or the plugin becomes unresponsive.
 */
"""

import time
import os

from utils import send_curl_command, is_ok, responded, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_number_of_inputs, parse_edid_version


def run_test():
    start_time = time.perf_counter()

    malformed_calls = [
        ("getEdid2AllmSupport (portId='S')", AVInputApis.get_edid2_allm_support_invalid_port),
        ("setEdid2AllmSupport (portId='f')", AVInputApis.set_edid2_allm_support_invalid_port),
        ("getRawSPD (portId='foo')", AVInputApis.get_raw_spd_invalid_port),
        ("getSPD (portId='foo')", AVInputApis.get_spd_invalid_port),
        ("getInputDevices (typeOfInput='ABCD')", AVInputApis.get_input_devices_invalid_type),
        ("getHdmiVersion (portId='foo')", AVInputApis.get_hdmi_version_invalid_port),
        ("getEdid2AllmSupport (portId=99, out of range)", AVInputApis.get_edid2_allm_support(99)),
        ("setEdid2AllmSupport (portId=99, out of range)", AVInputApis.set_edid2_allm_support(99, True)),
        ("getGameFeatureStatus (portId=99, out of range)", AVInputApis.get_game_feature_status(99)),
        ("getEdidVersion (portId=99, out of range)", AVInputApis.get_edid_version_out_of_range()),
        ("getVRRSupport (portId=99, out of range)", AVInputApis.get_vrr_support(99)),
        ("getVRRFrameRate (portId=99, out of range)", AVInputApis.get_vrr_frame_rate(99)),
        ("startInput (portId=99, out of range)", AVInputApis.start_input_out_of_range()),
        # ("startInput (typeOfInput='INVALID_TYPE')", AVInputApis.start_input_invalid_type()),
        ("stopInput (typeOfInput='INVALID_TYPE')", AVInputApis.stop_input_invalid_type()),
        ("setVideoRectangle (negative bounds)", AVInputApis.set_video_rectangle_invalid_bounds()),
        ("setVRRSupport (portId=-1)", AVInputApis.set_vrr_support_invalid()),
        ("setMixerLevels (primaryVolume=-1, inputVolume=101)", AVInputApis.set_mixer_levels_invalid()),
        ("setEdidVersion (edidVersion='HDMI9.9')", AVInputApis.set_edid_version_invalid()),
    ]

    try:
        for label, cmd in malformed_calls:
            log_info(f"Sending malformed request: {label}")
            resp = send_curl_command(cmd)
            log_warning(f"Response: {resp}")
            if not responded(resp):
                log_error(f"TCID20_NegativeAndBoundaryHandling Failed ❌ (no response for {label})")
                return False
            log_success(f"✅ Answered without dropping connection: {label}")

        log_info("Health check: numberOfInputs must still work")
        health = send_curl_command(AVInputApis.number_of_inputs)
        log_warning(f"Response: {health}")
        if not is_ok(health) or parse_number_of_inputs(health) is None:
            log_error("TCID20_NegativeAndBoundaryHandling Failed ❌ (plugin unhealthy after fuzzing)")
            return False
        log_success("✅ Plugin healthy after malformed traffic")

        log_info("Health check: a well-formed getter must still return valid data")
        good = send_curl_command(AVInputApis.get_edid_version(0))
        log_warning(f"Response: {good}")
        if not parse_edid_version(good):
            log_error("TCID20_NegativeAndBoundaryHandling Failed ❌ (valid getter broken after fuzzing)")
            return False
        log_success("✅ Valid requests still served correctly")
    finally:
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID20_NegativeAndBoundaryHandling Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
