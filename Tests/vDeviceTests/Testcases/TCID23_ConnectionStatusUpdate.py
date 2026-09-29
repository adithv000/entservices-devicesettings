"""
/**
 * @file TCID23_ConnectionStatusUpdate.py
 * @brief L3 AVInput vComponent-driven testcase.
 *
 * @testcase TCID23_ConnectionStatusUpdate
 * @details Injects HDMI hot-plug connection changes through the vComponent
 *          (connection_status -> onConnectionStateChanged in the HAL, which
 *          updates the per-port connected flag surfaced by GetHDMIInStatus) and
 *          captures each onDevicesChanged notification and verifies each change
 *          via the JSON-RPC getInputDevices "connected" flag. The cycle
 *          (connected true -> false) is exercised on every HDMI input port
 *          reported by numberOfInputs (typically port 0 and port 1). Port 0's
 *          connected:true injection uses the static command YAML file; all
 *          other injections use inline payload variants.
 *
 * @precondition
 *  - org.rdk.AVInput plugin is active and reachable via JSON-RPC endpoint.
 *  - HDMI Input vComponent reachable at HDMIIN_VCOMPONENT_API_URL.
 *
 * @dependencies
 *  - utils.py, AVInput_Curl.py, AVInput_Helpers.py, SuiteManager.py
 *  - vcomponent_configurations/commands/HDMIInput_Connection_Status.yaml
 *
 * @expected_result
 *  - onDevicesChanged and getInputDevices both report connected == injected
 *    value for each tested port.
 *
 * @pass_criteria
 *  - Both connected:true and connected:false injections are accepted, their
 *    matching events are captured, and both states are reflected in
 *    getInputDevices for every tested port; run_test() returns True.
 *
 * @failure_criteria
 *  - An injection is rejected, an expected event is absent or malformed, or
 *    either the event or getter state does not track the injected value.
 */
"""

import time
import os

from utils import (
    HDMIIN_CMD_BASE,
    JsonRpcEventListener,
    send_curl_command,
    send_vcomponent_command,
    send_vcomponent_payload,
    log_info,
    log_success,
    log_error,
    log_warning,
)
import AVInput_Curl as AVInputApis
from AVInput_Helpers import parse_input_devices, get_device_connected, parse_number_of_inputs

CONNECTION_YAML = "HDMIInput_Connection_Status.yaml"
EVENT_TIMEOUT = float(os.environ.get("AVINPUT_EVENT_TIMEOUT", "8"))


def _post_connection_file():
    http_code, body = send_vcomponent_command(f"{HDMIIN_CMD_BASE}/{CONNECTION_YAML}")
    log_warning(f"vComponent POST {CONNECTION_YAML}: HTTP {http_code} {body}")
    return http_code == 200


def _post_connection(port, connected):
    http_code, body = send_vcomponent_payload("connection_status", {"port": port, "connected": connected})
    log_warning(f"vComponent connection_status(port={port}, connected={connected}): HTTP {http_code} {body}")
    return http_code == 200


def _read_connected(port):
    response = send_curl_command(AVInputApis.get_input_devices(AVInputApis.TYPE_HDMI))
    log_warning(f"getInputDevices response: {response}")
    devices = parse_input_devices(response)
    if devices is None:
        return None
    return get_device_connected(devices, port)


def _resolve_ports():
    count = parse_number_of_inputs(send_curl_command(AVInputApis.number_of_inputs))
    if not isinstance(count, int) or count <= 0:
        # Fall back to the two ports the vDevice is known to expose.
        return [0, 1]
    return list(range(count))


def _expect_connection_event(listener, port, connected):
    notification = listener.wait_for_event(
        lambda params: get_device_connected(params.get("devices"), port) is connected,
        timeout=EVENT_TIMEOUT,
    )
    if notification is None:
        return False, (
            f"onDevicesChanged not received with connected={connected} "
            f"for port {port}"
        )
    log_success(f"Captured onDevicesChanged for port {port}: {notification}")
    return True, None


def _verify_port(listener, port):
    # Port 0's "connected=true" uses the static command file; others use payload.
    log_info(f"--- Port {port}: hot-plug connect/disconnect cycle ---")
    baseline = _read_connected(port)
    log_info(f"Baseline connected(port {port}) = {baseline}")

    if port == 0:
        log_info(f"Injecting connection_status(connected=true) via {CONNECTION_YAML}")
        posted = _post_connection_file()
    else:
        log_info(f"Injecting connection_status(port={port}, connected=true) via inline payload")
        posted = _post_connection(port, True)
    if not posted:
        return False, f"connected:true injection rejected for port {port}"
    event_ok, failure = _expect_connection_event(listener, port, True)
    if not event_ok:
        return False, failure

    after_true = _read_connected(port)
    log_info(f"connected(port {port}) after true injection = {after_true}")
    if not isinstance(after_true, bool):
        return False, f"connected flag not boolean after true for port {port}"
    if after_true is not True:
        return False, f"connected did not become true for port {port}"
    log_success(f"✅ Port {port} reported connected=true after hot-plug injection")

    log_info(f"Injecting connection_status(port={port}, connected=false) via inline payload")
    if not _post_connection(port, False):
        return False, f"connected:false injection rejected for port {port}"
    event_ok, failure = _expect_connection_event(listener, port, False)
    if not event_ok:
        return False, failure

    after_false = _read_connected(port)
    log_info(f"connected(port {port}) after false injection = {after_false}")
    if not isinstance(after_false, bool):
        return False, f"connected flag not boolean after false for port {port}"
    if after_false is not False:
        return False, f"connected did not become false for port {port}"
    log_success(f"✅ Port {port} reported connected=false after unplug injection")
    return True, None


def run_test():
    start_time = time.perf_counter()
    listener = JsonRpcEventListener(
        AVInputApis.CALLSIGN,
        "onDevicesChanged",
        "ID_TCID23_devices",
        timeout=EVENT_TIMEOUT,
    )
    if not listener.connect():
        log_error("TCID23_ConnectionStatusUpdate Failed (event registration rejected)")
        return False

    ports = _resolve_ports()
    log_info(f"Testing HDMI input connection status on ports: {ports}")

    try:
        for port in ports:
            ok, failure = _verify_port(listener, port)
            if not ok:
                log_error(f"TCID23_ConnectionStatusUpdate Failed ❌ ({failure})")
                return False
    finally:
        for port in ports:
            _post_connection(port, False)
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        listener.close()

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID23_ConnectionStatusUpdate Passed ✅"
    if os.environ.get("AVINPUT_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
