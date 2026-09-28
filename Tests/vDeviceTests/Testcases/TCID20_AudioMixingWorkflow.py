"""
/**
 * @file TCID20_AudioMixingWorkflow.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID20_AudioMixingWorkflow
 * @details Scenario: an app keeps its own audio audible while an HDMI source is
 *          presented (e.g. voice guidance over a games console). Sequence:
 *            startInput(port 0, requestAudioMix=true)
 *            -> setMixerLevels across a sweep of primary/input balances
 *            -> stopInput -> restart with requestAudioMix=false
 *          Verifies mixing can be requested at start time and that mixer levels
 *          are accepted across the full 0..100 range including both extremes.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - Mixed-audio start succeeds and every mixer level combination is accepted.
 *
 * @pass_criteria
 *  - All starts and mixer updates succeed; run_test() returns True.
 *
 * @failure_criteria
 *  - Mixed start rejected or any mixer level combination fails.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success

PORT = 0

# (primaryVolume, inputVolume) sweep including both extremes.
MIXER_SWEEP = [
    (100, 0),
    (100, 75),
    (50, 50),
    (0, 100),
]


def run_test():
    start_time = time.perf_counter()

    try:
        log_info("Step 1: startInput with requestAudioMix=true")
        mixed_resp = send_curl_command(
            AVInputApis.start_input(PORT, request_audio_mix=True, plane=0, top_most=False)
        )
        log_warning(f"startInput (mixed) response: {mixed_resp}")
        if not result_success(mixed_resp):
            log_error("TCID20_AudioMixingWorkflow Failed ❌ (mixed-audio start rejected)")
            return False
        log_success("✅ Started with audio mixing requested")

        for primary, source in MIXER_SWEEP:
            log_info(f"Step: setMixerLevels primary={primary} input={source}")
            resp = send_curl_command(AVInputApis.set_mixer_levels(primary, source))
            log_warning(f"setMixerLevels response: {resp}")
            if not result_success(resp):
                log_error(
                    f"TCID20_AudioMixingWorkflow Failed ❌ "
                    f"(mixer levels {primary}/{source} rejected)"
                )
                return False
            log_success(f"✅ Mixer levels {primary}/{source} accepted")
            time.sleep(1)

        log_info("Step: stopInput")
        stop_resp = send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        log_warning(f"stopInput response: {stop_resp}")
        if not result_success(stop_resp):
            log_error("TCID20_AudioMixingWorkflow Failed ❌ (stopInput rejected)")
            return False

        log_info("Step: restart with requestAudioMix=false (exclusive source audio)")
        exclusive_resp = send_curl_command(
            AVInputApis.start_input(PORT, request_audio_mix=False, plane=0, top_most=False)
        )
        log_warning(f"startInput (exclusive) response: {exclusive_resp}")
        if not result_success(exclusive_resp):
            log_error("TCID20_AudioMixingWorkflow Failed ❌ (exclusive-audio start rejected)")
            return False
        log_success("✅ Started with audio mixing disabled")
    finally:
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID20_AudioMixingWorkflow Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
