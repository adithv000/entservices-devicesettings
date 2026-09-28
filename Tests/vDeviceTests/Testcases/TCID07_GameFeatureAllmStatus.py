"""
/**
 * @file TCID07_GameFeatureAllmStatus.py
 * @brief L3 AVInput functional testcase.
 *
 * @testcase TCID07_GameFeatureAllmStatus
 * @details Validates getSupportedGameFeatures advertises ALLM and that
 *          getGameFeatureStatus(ALLM) on port 0 returns a boolean mode without
 *          error.
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
 *  - Supported features list includes ALLM; game feature status is boolean.
 *
 * @pass_criteria
 *  - Both queries succeed and run_test() returns True.
 *
 * @failure_criteria
 *  - ALLM absent, status non-boolean, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_supported_game_features, parse_game_feature_mode

PORT = 0


def run_test():
    start_time = time.perf_counter()

    log_info("Executing getSupportedGameFeatures")
    feat_resp = send_curl_command(AVInputApis.get_supported_game_features)
    log_warning(f"getSupportedGameFeatures response: {feat_resp}")
    features = parse_supported_game_features(feat_resp)
    if features is None:
        log_error("TCID07_GameFeatureAllmStatus Failed ❌ (supported features list missing)")
        return False
    if AVInputApis.GAME_FEATURE_ALLM not in features:
        log_error("TCID07_GameFeatureAllmStatus Failed ❌ (ALLM not advertised as supported)")
        return False
    log_success(f"✅ Supported game features: {features}")

    log_info("Executing getGameFeatureStatus(ALLM) on port 0")
    status_resp = send_curl_command(AVInputApis.get_game_feature_status(PORT, AVInputApis.GAME_FEATURE_ALLM))
    log_warning(f"getGameFeatureStatus response: {status_resp}")
    mode = parse_game_feature_mode(status_resp)
    if mode is None:
        log_error("TCID07_GameFeatureAllmStatus Failed ❌ (ALLM status not a boolean)")
        return False
    log_success(f"✅ ALLM game feature status = {mode}")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID07_GameFeatureAllmStatus Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
