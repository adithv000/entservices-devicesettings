"""
/**
 * @file TCID15_GameModeProvisioning.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID15_GameModeProvisioning
 * @details Scenario: enabling "Game Mode" on an HDMI port from the settings UI.
 *          The middleware must advertise ALLM in the port EDID and then report
 *          the ALLM game-feature status. Sequence:
 *            setEdidVersion(HDMI2.0) -> setEdid2AllmSupport(true) -> get (true)
 *            -> getGameFeatureStatus(ALLM) -> setEdid2AllmSupport(false)
 *            -> get (false) -> restore
 *          Verifies the advertised ALLM bit is independently togglable and that
 *          the game-feature status query stays serviceable in both states.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - ALLM-in-EDID toggles true/false and ALLM status remains queryable.
 *
 * @pass_criteria
 *  - Both toggle directions verified; run_test() returns True.
 *
 * @failure_criteria
 *  - Toggle not reflected or ALLM status query fails.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import (
    result_success,
    parse_allm_support,
    parse_game_feature_mode,
    parse_edid_version,
)

PORT = 0


def _toggle_allm(enabled):
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
    original_allm = parse_allm_support(send_curl_command(AVInputApis.get_edid2_allm_support(PORT)))
    log_info(f"Baseline: edidVersion={original_version} allmSupport={original_allm}")

    try:
        log_info("Step 1: select EDID 2.0 (prerequisite for ALLM advertisement)")
        ver_resp = send_curl_command(AVInputApis.set_edid_version(PORT, AVInputApis.EDID_VERSION_20))
        log_warning(f"setEdidVersion(HDMI2.0) response: {ver_resp}")
        if not result_success(ver_resp):
            log_error("TCID15_GameModeProvisioning Failed ❌ (could not select EDID 2.0)")
            return False

        log_info("Step 2: enable Game Mode (ALLM advertised in EDID)")
        if not _toggle_allm(True):
            log_error("TCID15_GameModeProvisioning Failed ❌ (ALLM not enabled)")
            return False
        log_success("✅ ALLM advertised in EDID")

        log_info("Step 3: query ALLM game feature status while enabled")
        status_resp = send_curl_command(
            AVInputApis.get_game_feature_status(PORT, AVInputApis.GAME_FEATURE_ALLM)
        )
        log_warning(f"getGameFeatureStatus response: {status_resp}")
        if parse_game_feature_mode(status_resp) is None:
            log_error("TCID15_GameModeProvisioning Failed ❌ (ALLM status not queryable)")
            return False
        log_success("✅ ALLM status queryable while Game Mode enabled")

        log_info("Step 4: disable Game Mode")
        if not _toggle_allm(False):
            log_error("TCID15_GameModeProvisioning Failed ❌ (ALLM not disabled)")
            return False
        log_success("✅ ALLM withdrawn from EDID")

        log_info("Step 5: ALLM status still queryable while disabled")
        status_off = send_curl_command(
            AVInputApis.get_game_feature_status(PORT, AVInputApis.GAME_FEATURE_ALLM)
        )
        log_warning(f"getGameFeatureStatus response: {status_off}")
        if parse_game_feature_mode(status_off) is None:
            log_error("TCID15_GameModeProvisioning Failed ❌ (ALLM status unavailable when disabled)")
            return False
        log_success("✅ ALLM status queryable while Game Mode disabled")
    finally:
        if isinstance(original_allm, bool):
            send_curl_command(AVInputApis.set_edid2_allm_support(PORT, original_allm))
        if original_version in (AVInputApis.EDID_VERSION_14, AVInputApis.EDID_VERSION_20):
            send_curl_command(AVInputApis.set_edid_version(PORT, original_version))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID15_GameModeProvisioning Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
