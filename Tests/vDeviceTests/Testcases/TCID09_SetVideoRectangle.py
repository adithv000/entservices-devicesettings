"""
/**
 * @file TCID09_SetVideoRectangle.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID09_SetVideoRectangle
 * @details Starts HDMI input on port 0 and sets the HDMI input video window to a
 *          full-screen 1920x1080 rectangle, then to a scaled PIP rectangle,
 *          verifying each setVideoRectangle call is accepted.
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
 *  - Both setVideoRectangle calls return a successful JSON-RPC result.
 *
 * @pass_criteria
 *  - Both rectangle updates succeed and run_test() returns True.
 *
 * @failure_criteria
 *  - Either call fails or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, is_ok, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success

PORT = 0


def run_test():
    start_time = time.perf_counter()

    try:
        start_resp = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_resp}")
        if not is_ok(start_resp):
            log_error("TCID09_SetVideoRectangle Failed ❌ (startInput not accepted)")
            return False

        log_info("Setting full-screen video rectangle 0,0 1920x1080")
        full_resp = send_curl_command(AVInputApis.set_video_rectangle(0, 0, 1920, 1080))
        log_warning(f"setVideoRectangle (full) response: {full_resp}")
        if not result_success(full_resp):
            log_error("TCID09_SetVideoRectangle Failed ❌ (full-screen rectangle not accepted)")
            return False
        log_success("✅ Full-screen rectangle accepted")

        log_info("Setting PIP video rectangle 960,540 640x360")
        pip_resp = send_curl_command(AVInputApis.set_video_rectangle(960, 540, 640, 360))
        log_warning(f"setVideoRectangle (pip) response: {pip_resp}")
        if not result_success(pip_resp):
            log_error("TCID09_SetVideoRectangle Failed ❌ (PIP rectangle not accepted)")
            return False
        log_success("✅ PIP rectangle accepted")
    finally:
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID09_SetVideoRectangle Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
