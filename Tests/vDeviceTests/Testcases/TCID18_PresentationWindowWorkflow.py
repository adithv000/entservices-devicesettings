"""
/**
 * @file TCID18_PresentationWindowWorkflow.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID18_PresentationWindowWorkflow
 * @details Scenario: a UI presents an HDMI source full-screen, shrinks it into a
 *          picture-in-picture tile, moves the tile, restores full-screen, then
 *          exits. Sequence:
 *            startInput(port 0) -> setVideoRectangle(full)
 *            -> setVideoRectangle(PIP top-right) -> setVideoRectangle(PIP bottom-left)
 *            -> setVideoRectangle(full) -> stopInput
 *          Also confirms setVideoRectangle is rejected/handled cleanly when no
 *          input is presenting (called after stop).
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - Every rectangle update during presentation is accepted.
 *
 * @pass_criteria
 *  - Full geometry sequence succeeds and post-stop call is handled.
 *
 * @failure_criteria
 *  - Any rectangle update rejected while presenting, or plugin unresponsive.
 */
"""

import time
import os

from utils import send_curl_command, responded, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success

PORT = 0

GEOMETRY_SEQUENCE = [
    ("full-screen", 0, 0, 1920, 1080),
    ("PIP top-right", 1280, 0, 640, 360),
    ("PIP bottom-left", 0, 720, 640, 360),
    ("full-screen restore", 0, 0, 1920, 1080),
]


def run_test():
    start_time = time.perf_counter()

    try:
        log_info("Step 1: startInput on port 0")
        start_resp = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_resp}")
        if not result_success(start_resp):
            log_error("TCID18_PresentationWindowWorkflow Failed ❌ (startInput rejected)")
            return False

        for label, x, y, w, h in GEOMETRY_SEQUENCE:
            log_info(f"Step: setVideoRectangle {label} ({x},{y} {w}x{h})")
            resp = send_curl_command(AVInputApis.set_video_rectangle(x, y, w, h))
            log_warning(f"setVideoRectangle response: {resp}")
            if not result_success(resp):
                log_error(f"TCID18_PresentationWindowWorkflow Failed ❌ ({label} rejected)")
                return False
            log_success(f"✅ {label} applied")
            time.sleep(1)

        log_info("Step: stopInput")
        stop_resp = send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        log_warning(f"stopInput response: {stop_resp}")
        if not result_success(stop_resp):
            log_error("TCID18_PresentationWindowWorkflow Failed ❌ (stopInput rejected)")
            return False

        log_info("Step: setVideoRectangle after stop (must be handled gracefully)")
        post_resp = send_curl_command(AVInputApis.set_video_rectangle(0, 0, 1920, 1080))
        log_warning(f"setVideoRectangle (after stop) response: {post_resp}")
        if not responded(post_resp):
            log_error("TCID18_PresentationWindowWorkflow Failed ❌ (no response after stop)")
            return False
        log_success("✅ Post-stop rectangle call handled gracefully")
    finally:
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID18_PresentationWindowWorkflow Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
