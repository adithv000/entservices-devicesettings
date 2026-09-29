"""
/**
 * @file TCID27_StableSignalState.py
 * @brief Reproduces HDMI signal-change notifications with vComponent stimuli.
 *
 * @testcase TCID27_StableSignalState
 * @details Registers for onSignalChanged, presents HDMI port 0, and injects a
 *          sequence of distinct signal states through the HDMI Input
 *          vComponent. Each resulting notification payload is captured and
 *          validated directly over a persistent JSON-RPC WebSocket.
 *
 * @precondition
 *  - org.rdk.AVInput is active and reachable through JSON-RPC.
 *  - HDMI Input vComponent is reachable at HDMIIN_VCOMPONENT_API_URL.
 *
 * @expected_result
 *  - onSignalChanged delivers stableSignal, unstableSignal,
 *    notSupportedSignal, and noSignal in injection order.
 *
 * @pass_criteria
 *  - Every stimulus is accepted and its matching notification is captured.
 */
"""

import os
import time

import AVInput_Curl as AVInputApis
from AVInput_Helpers import result_success
from utils import (
    HDMIIN_CMD_BASE,
    JsonRpcEventListener,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_curl_command,
    send_vcomponent_command,
    send_vcomponent_payload,
)

PORT = 0
SIGNAL_YAML = "HDMIInput_Signal_LOCKED_Status.yaml"
EVENT_TIMEOUT = float(os.environ.get("AVINPUT_EVENT_TIMEOUT", "8"))
TRANSITION_DELAY = float(os.environ.get("AVINPUT_TRANSITION_DELAY", "2"))

def _post_signal(state):
    http_code, body = send_vcomponent_payload(
        "signal_status", {"port": PORT, "state": state}
    )
    log_warning(
        f"vComponent signal_status(port={PORT}, state={state}): "
        f"HTTP {http_code} {body}"
    )
    return http_code == 200

def _post_locked_yaml():
    yaml_path = f"{HDMIIN_CMD_BASE}/{SIGNAL_YAML}"
    http_code, body = send_vcomponent_command(yaml_path)
    log_warning(f"vComponent POST {SIGNAL_YAML}: HTTP {http_code} {body}")
    return http_code == 200


def _expect_signal(listener, expected_status):
    notification = listener.wait_for_event(
        lambda params: (
            str(params.get("id")) == str(PORT)
            and params.get("signalStatus") == expected_status
        ),
        timeout=EVENT_TIMEOUT,
    )
    if notification is None:
        log_error(
            f"onSignalChanged not received for port={PORT}, "
            f"signalStatus='{expected_status}'"
        )
        return False
    log_success(f"Captured onSignalChanged: {notification}")
    return True


def run_test():
    listener = JsonRpcEventListener(
        AVInputApis.CALLSIGN,
        "onSignalChanged",
        "ID_TCID27_signal",
        timeout=EVENT_TIMEOUT,
    )
    if not listener.connect():
        log_error("TCID27_StableSignalState Failed (event registration rejected)")
        return False

    try:
        # Establish a known baseline before driving distinct transitions.
        time.sleep(TRANSITION_DELAY)

        start_response = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_response}")
        if not result_success(start_response):
            log_error("TCID27_StableSignalState Failed (startInput rejected)")
            return False
        time.sleep(TRANSITION_DELAY)

        http_code, body = send_vcomponent_payload(
            "connection_status", {"port": PORT, "connected": True}
        )
        log_warning(f"vComponent connection_status(connected=true): HTTP {http_code} {body}")
        if http_code != 200:
            log_error("TCID27_StableSignalState Failed (connection rejected)")
            return False

        transitions = (
            ("LOCKED", AVInputApis.SIGNAL_STABLE, True),
            ("UNSTABLE", AVInputApis.SIGNAL_UNSTABLE, False),
            ("NOT_SUPPORTED", AVInputApis.SIGNAL_NOT_SUPPORTED, False),
            ("NO_SIGNAL", AVInputApis.SIGNAL_NO, False),
        )
        for index, (state, expected_status, use_yaml) in enumerate(transitions, start=1):
            log_info(
                f"Signal transition {index}: inject {state}; "
                f"expect onSignalChanged status='{expected_status}'"
            )
            accepted = _post_locked_yaml() if use_yaml else _post_signal(state)
            if not accepted:
                log_error(
                    f"TCID27_StableSignalState Failed ({state} injection rejected)"
                )
                return False
            if not _expect_signal(listener, expected_status):
                return False

        log_success("Signal transition notifications captured in injection order")
        return True
    finally:
        send_vcomponent_payload(
            "connection_status", {"port": PORT, "connected": False}
        )
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        listener.close()
