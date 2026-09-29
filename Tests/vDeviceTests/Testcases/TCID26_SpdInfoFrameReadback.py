"""
/**
 * @file TCID26_SpdInfoFrameReadback.py
 * @brief L3 AVInput vComponent-driven testcase.
 *
 * @testcase TCID26_SpdInfoFrameReadback
 * @details Reads getSPD, injects an SPD InfoFrame through the vComponent
 *          (HDMIInput_SPDInfo_Frame.yaml) and re-reads getSPD to verify the new
 *          SPD is surfaced.
 *
 * @note HAL path (dHdmiInAIDLImpl): getSPD -> GetHDMISPDInformation ->
 *       hi->getSPDInfoFrame() reads the SPD DIRECTLY from the vComponent (the
 *       onSPDInfoFrame HAL callback is a no-op, so nothing is cached
 *       plugin-side). The vComponent only applies an injected SPD frame once the
 *       source is PRESENTED (connected + signal LOCKED); a bare startInput does
 *       not present the port on this AIDL build (SelectHDMIInPort is stubbed), so
 *       the injected SPD would be ignored and getSPD would return the unchanged
 *       baseline. This test therefore injects connection_status(true) and
 *       signal_status(LOCKED) before the SPD frame.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *  - HDMI Input vComponent reachable at HDMIIN_VCOMPONENT_API_URL.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *  - vcomponent_configurations/commands/HDMIInput_Connection_Status.yaml
 *  - vcomponent_configurations/commands/HDMIInput_Signal_Status.yaml
 *  - vcomponent_configurations/commands/HDMIInput_SPDInfo_Frame.yaml
 *
 * @expected_result
 *  - getSPD returns a valid HDMISPD string after the injection and, when the
 *    source is present, reflects the injected SPD payload.
 *
 * @pass_criteria
 *  - The YAML post is accepted and getSPD changes to a non-empty injected
 *    value; run_test() returns True.
 *
 * @failure_criteria
 *  - The YAML post is rejected, or getSPD is empty/unchanged after injection.
 */
"""

import time
import os

from utils import (
    HDMIIN_CMD_BASE,
    send_curl_command,
    send_vcomponent_command,
    send_vcomponent_payload,
    log_info,
    log_success,
    log_error,
    log_warning,
)
import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success, parse_spd

PORT = 0
CONNECTION_YAML = "HDMIInput_Connection_Status.yaml"
SIGNAL_YAML = "HDMIInput_Signal_LOCKED_Status.yaml"
SPD_YAML = "HDMIInput_SPDInfo_Frame.yaml"


def _post_file(name):
    http_code, body = send_vcomponent_command(f"{HDMIIN_CMD_BASE}/{name}")
    log_warning(f"vComponent POST {name}: HTTP {http_code} {body}")
    return http_code == 200


def _post_spd():
    return _post_file(SPD_YAML)


def _read_spd():
    response = send_curl_command(AVInputApis.get_raw_spd(PORT))
    log_warning(f"getSPD response: {response}")
    return parse_spd(response)


def run_test():
    start_time = time.perf_counter()

    try:
        log_info("Step 1: startInput on port 0")
        start_resp = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_resp}")
        if not result_success(start_resp):
            log_error("TCID26_SpdInfoFrameReadback Failed ❌ (startInput rejected)")
            return False
        time.sleep(1)

        # Step 2: present the port (connection + LOCKED signal) so the vComponent
        # applies the injected SPD frame instead of returning the baseline.
        log_info(f"Step 2: present source via {CONNECTION_YAML} + {SIGNAL_YAML}")
        if not _post_file(CONNECTION_YAML):
            log_error("TCID26_SpdInfoFrameReadback Failed ❌ (connection_status YAML rejected)")
            return False
        time.sleep(1)
        if not _post_file(SIGNAL_YAML):
            log_error("TCID26_SpdInfoFrameReadback Failed ❌ (signal_status YAML rejected)")
            return False
        time.sleep(2)

        # Step 3: read the current SPD BEFORE the injection.
        before_spd = _read_spd()
        if before_spd is None:
            log_error("TCID26_SpdInfoFrameReadback Failed ❌ (getSPD missing HDMISPD before injection)")
            return False
        log_info(f"getSPD BEFORE injection = '{before_spd or '<empty>'}'")

        # Step 4: inject the new SPD InfoFrame from the command YAML.
        log_info(f"Step 4: injecting spdinfo_frame via {SPD_YAML}")
        if not _post_spd():
            log_error("TCID26_SpdInfoFrameReadback Failed ❌ (spdinfo_frame YAML rejected)")
            return False
        time.sleep(2)

        # Step 5: re-read getSPD and verify the new value.
        after_spd = _read_spd()
        if after_spd is None:
            log_error("TCID26_SpdInfoFrameReadback Failed ❌ (getSPD missing HDMISPD after injection)")
            return False
        log_info(f"getSPD AFTER injection = '{after_spd or '<empty>'}'")

        if not after_spd:
            log_error(
                "TCID26_SpdInfoFrameReadback Failed ❌ "
                "(getSPD is empty after injection)"
            )
            return False
        if after_spd == before_spd:
            log_error(
                "TCID26_SpdInfoFrameReadback Failed ❌ "
                "(getSPD was unchanged after injection)"
            )
            return False
        log_success(f"✅ SPD updated after injection: '{before_spd}' -> '{after_spd}'")
    finally:
        send_vcomponent_payload("signal_status", {"port": PORT, "state": "NO_SIGNAL"})
        send_vcomponent_payload("connection_status", {"port": PORT, "connected": False})
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID26_SpdInfoFrameReadback Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
