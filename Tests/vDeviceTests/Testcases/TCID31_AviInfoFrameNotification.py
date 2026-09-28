"""
/**
 * @file TCID30_AviInfoFrameNotification.py
 * @brief Captures an AVI InfoFrame content-type notification.
 *
 * @testcase TCID30_AviInfoFrameNotification
 * @details Registers for aviContentTypeUpdate, presents HDMI port 0, posts
 *          HDMIInput_AVIInfo_Frame.yaml through the HDMI Input vComponent, and
 *          validates the resulting JSON-RPC event payload.
 *
 * @precondition
 *  - org.rdk.AVInput is active and reachable through JSON-RPC.
 *  - HDMI Input vComponent is reachable at HDMIIN_VCOMPONENT_API_URL.
 *
 * @expected_result
 *  - aviContentTypeUpdate reports port 0 and AVI_CONTENT_INVALID because the
 *    YAML InfoFrame does not set the content-type-present bit.
 *
 * @pass_criteria
 *  - The YAML post succeeds and the expected notification is captured.
 */
"""

import os

import AVInput_Curl as AVInputApis
from utils import (
    HDMIIN_CMD_BASE,
    JsonRpcEventListener,
    is_ok,
    log_error,
    log_success,
    log_warning,
    send_curl_command,
    send_vcomponent_command,
    send_vcomponent_payload,
)

PORT = 0
AVI_INFOFRAME_YAML = "HDMIInput_AVIInfo_Frame.yaml"
EVENT_TIMEOUT = float(os.environ.get("AVINPUT_EVENT_TIMEOUT", "8"))


def _post_avi_infoframe():
    yaml_path = f"{HDMIIN_CMD_BASE}/{AVI_INFOFRAME_YAML}"
    http_code, body = send_vcomponent_command(yaml_path)
    log_warning(f"vComponent POST {AVI_INFOFRAME_YAML}: HTTP {http_code} {body}")
    return http_code == 200


def _expect_avi_content_type(listener):
    notification = listener.wait_for_event(
        lambda params: (
            str(params.get("id")) == str(PORT)
            and params.get("aviContentType") == AVInputApis.AVI_CONTENT_INVALID
        ),
        timeout=EVENT_TIMEOUT,
    )
    if notification is None:
        log_error(
            "aviContentTypeUpdate not received for port=0, "
            f"aviContentType={AVInputApis.AVI_CONTENT_INVALID}"
        )
        return False
    log_success(f"Captured aviContentTypeUpdate: {notification}")
    return True


def run_test():
    listener = JsonRpcEventListener(
        AVInputApis.CALLSIGN,
        "aviContentTypeUpdate",
        "ID_TCID30_avi_infoframe",
        timeout=EVENT_TIMEOUT,
    )
    if not listener.connect():
        log_error("TCID30_AviInfoFrameNotification Failed (event registration rejected)")
        return False

    try:
        start_response = send_curl_command(AVInputApis.start_input(PORT))
        log_warning(f"startInput response: {start_response}")
        if not is_ok(start_response):
            log_error("TCID30_AviInfoFrameNotification Failed (startInput rejected)")
            return False

        http_code, body = send_vcomponent_payload(
            "connection_status", {"port": PORT, "connected": True}
        )
        log_warning(f"vComponent connection_status(connected=true): HTTP {http_code} {body}")
        if http_code != 200:
            log_error("TCID30_AviInfoFrameNotification Failed (connection rejected)")
            return False

        http_code, body = send_vcomponent_payload(
            "signal_status", {"port": PORT, "state": "LOCKED"}
        )
        log_warning(f"vComponent signal_status(LOCKED): HTTP {http_code} {body}")
        if http_code != 200:
            log_error("TCID30_AviInfoFrameNotification Failed (signal rejected)")
            return False

        if not _post_avi_infoframe():
            log_error("TCID30_AviInfoFrameNotification Failed (AVI InfoFrame rejected)")
            return False
        if not _expect_avi_content_type(listener):
            return False

        log_success("TCID30_AviInfoFrameNotification Passed")
        return True
    finally:
        send_vcomponent_payload(
            "signal_status", {"port": PORT, "state": "NO_SIGNAL"}
        )
        send_vcomponent_payload(
            "connection_status", {"port": PORT, "connected": False}
        )
        send_curl_command(AVInputApis.stop_input(AVInputApis.TYPE_HDMI))
        listener.close()