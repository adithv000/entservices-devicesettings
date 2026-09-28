"""
/**
 * @file TCID17_MultiPortIndependence.py
 * @brief L3 AVInput scenario testcase.
 *
 * @testcase TCID17_MultiPortIndependence
 * @details Scenario: a TV with several HDMI ports must keep per-port settings
 *          isolated - provisioning port 0 must not leak into port 1. Applies
 *          opposing EDID-version and ALLM settings to two ports and verifies
 *          each port retains its own value:
 *            port A -> HDMI2.0 + ALLM true
 *            port B -> HDMI1.4 + ALLM false
 *          then re-reads both to confirm no cross-contamination.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *  - Device reports at least 2 HDMI input ports (skips cleanly otherwise).
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - Each port independently reports the value written to it.
 *
 * @pass_criteria
 *  - No setting bleeds between ports; run_test() returns True.
 *
 * @failure_criteria
 *  - A write to one port changes the value reported by the other.
 */
"""

import time
import os

from utils import send_curl_command, log_info, log_success, log_error, log_warning
import AVInput_Curl as AVInputApis
from AVInput_Helpers import (
    result_success,
    parse_number_of_inputs,
    parse_edid_version,
    parse_allm_support,
)

PORT_A = 0
PORT_B = 1


def _snapshot(port):
    return (
        parse_edid_version(send_curl_command(AVInputApis.get_edid_version(port))),
        parse_allm_support(send_curl_command(AVInputApis.get_edid2_allm_support(port))),
    )


def _provision(port, version, allm):
    ver_resp = send_curl_command(AVInputApis.set_edid_version(port, version))
    log_warning(f"port {port} setEdidVersion({version}): {ver_resp}")
    if not result_success(ver_resp):
        return False
    allm_resp = send_curl_command(AVInputApis.set_edid2_allm_support(port, allm))
    log_warning(f"port {port} setEdid2AllmSupport({allm}): {allm_resp}")
    return result_success(allm_resp)


def run_test():
    start_time = time.perf_counter()

    count = parse_number_of_inputs(send_curl_command(AVInputApis.number_of_inputs))
    if not count or count < 2:
        log_error(
            f"TCID17_MultiPortIndependence Failed ❌ "
            f"(needs >= 2 HDMI ports, device reports {count})"
        )
        return False

    baseline_a = _snapshot(PORT_A)
    baseline_b = _snapshot(PORT_B)
    log_info(f"Baseline port {PORT_A}: {baseline_a}  port {PORT_B}: {baseline_b}")

    try:
        log_info(f"Step 1: provision port {PORT_A} -> HDMI2.0 + ALLM true")
        if not _provision(PORT_A, AVInputApis.EDID_VERSION_20, True):
            log_error("TCID17_MultiPortIndependence Failed ❌ (port A provisioning rejected)")
            return False

        log_info(f"Step 2: provision port {PORT_B} -> HDMI1.4 + ALLM false")
        if _provision(PORT_B, AVInputApis.EDID_VERSION_14, False):
            log_error("TCID17_MultiPortIndependence Failed ❌ (port B provisioning accepted unexpectedly)")
            return False

        log_info("Step 3: re-read both ports and confirm isolation")
        ver_a, allm_a = _snapshot(PORT_A)
        ver_b, allm_b = _snapshot(PORT_B)
        log_info(f"  port {PORT_A}: edidVersion={ver_a} allm={allm_a}")
        log_info(f"  port {PORT_B}: edidVersion={ver_b} allm={allm_b}")

        if ver_a != AVInputApis.EDID_VERSION_20:
            log_error(f"TCID17_MultiPortIndependence Failed ❌ (port A version leaked: {ver_a})")
            return False
        if ver_b != AVInputApis.EDID_VERSION_14:
            log_error(f"TCID17_MultiPortIndependence Failed ❌ (port B version leaked: {ver_b})")
            return False
        if allm_a is not True:
            log_error(f"TCID17_MultiPortIndependence Failed ❌ (port A ALLM leaked: {allm_a})")
            return False
        if allm_b is not False:
            log_error(f"TCID17_MultiPortIndependence Failed ❌ (port B ALLM leaked: {allm_b})")
            return False

        log_success("✅ Per-port EDID version and ALLM settings are isolated")
    finally:
        for port, (ver, allm) in ((PORT_A, baseline_a), (PORT_B, baseline_b)):
            if ver in (AVInputApis.EDID_VERSION_14, AVInputApis.EDID_VERSION_20):
                send_curl_command(AVInputApis.set_edid_version(port, ver))
            if isinstance(allm, bool):
                send_curl_command(AVInputApis.set_edid2_allm_support(port, allm))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID17_MultiPortIndependence Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
