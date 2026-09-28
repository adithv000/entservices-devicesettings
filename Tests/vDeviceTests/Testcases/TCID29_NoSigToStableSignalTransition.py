"""
/**
 * @file TCID29_NoSigToStableSignalTransition.py
 * @brief Reproduces HDMI signal-change notifications with vComponent stimuli.
 *
 * @testcase TCID29_NoSigToStableSignalTransition
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
 *  - onSignalChanged delivers noSignal, unstableSignal, and stableSignal in
 *    injection order.
 *
 * @pass_criteria
 *  - Every stimulus is accepted and its matching notification is captured.
 */
"""

import os

import AVInput_Curl as AVInputApis
from utils import (
    HDMIIN_CMD_BASE,
    JsonRpcEventListener,
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_curl_command,
    send_vcomponent_command,
    send_vcomponent_payload,
)

PORT = 0
STABLE_SIGNAL_YAML = "HDMIInput_Signal_LOCKED_Status.yaml"
UNSTABLE_SIGNAL_YAML = "HDMIInput_Signal_UNSTABLE_Status.yaml"
EVENT_TIMEOUT = float(os.environ.get("AVINPUT_EVENT_TIMEOUT", "8"))


def _post_signal(state):
    http_code, body = send_vcomponent_payload(
        "signal_status", {"port": PORT, "state": state}
    )
    log_warning(
        f"vComponent signal_status(port={PORT}, state={state}): "
        f"HTTP {http_code} {body}"
    )
    return http_code == 200


def _post_yaml(yaml_file):
    yaml_path = f"{HDMIIN_CMD_BASE}/{yaml_file}"
    http_code, body = send_vcomponent_command(yaml_path)
    log_warning(f"vComponent POST {yaml_file}: HTTP {http_code} {body}")
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
        "ID_TCID29_signal",
        timeout=EVENT_TIMEOUT,
    )
    if not listener.connect():
        log_error("TCID29_NoSigToStableSignalTransition Failed (event registration rejected)")
        return False

    try:
        # Establish a known baseline before driving distinct transitions.
        if not _post_signal("NO_SIGNAL"):
            log_error("TCID29_NoSigToStableSignalTransition Failed (baseline rejected)")
            return False
        if not _expect_signal(listener, AVInputApis.SIGNAL_NO):
            return False

        start_response = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_response}")
        if not is_ok(start_response):
            log_error("TCID29_NoSigToStableSignalTransition Failed (startInput rejected)")
            return False

        http_code, body = send_vcomponent_payload(
            "connection_status", {"port": PORT, "connected": True}
        )
        log_warning(f"vComponent connection_status(connected=true): HTTP {http_code} {body}")
        if http_code != 200:
            log_error("TCID29_NoSigToStableSignalTransition Failed (connection rejected)")
            return False

        transitions = (
            (UNSTABLE_SIGNAL_YAML, AVInputApis.SIGNAL_UNSTABLE),
            (STABLE_SIGNAL_YAML, AVInputApis.SIGNAL_STABLE),
        )
        for index, (yaml_file, expected_status) in enumerate(transitions, start=1):
            log_info(
                f"Signal transition {index}: inject {yaml_file}; "
                f"expect onSignalChanged status='{expected_status}'"
            )
            accepted = _post_yaml(yaml_file)
            if not accepted:
                log_error(
                    f"TCID29_NoSigToStableSignalTransition Failed ({yaml_file} injection rejected)"
                )
                return False
            if not _expect_signal(listener, expected_status):
                return False

        log_success("No-signal to stable-signal notifications captured in order")
        return True
    finally:
        _post_signal("NO_SIGNAL")
        send_vcomponent_payload(
            "connection_status", {"port": PORT, "connected": False}
        )
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        listener.close()
