"""
/**
 * @file AVInput_Curl.py
 * @brief AVInput (HDMI Input) JSON-RPC curl command library.
 *
 * @testcase AVInput_Curl
 * @details Central library of curl command builders for every
 *          org.rdk.AVInput JSON-RPC method and event exercised by the L3
 *          suite. Static getters are exposed as ready-to-send command lists;
 *          setters / parameterized calls are exposed as builder functions.
 *
 * @note JSON-RPC parameter spelling below follows the working curl set in
 *       curl_extract.txt (audioMix / planeType / topMost / portId as string).
 *       If a target build exposes different parameter spelling, adjust the
 *       builders here (single source of truth) rather than the test cases.
 */
"""

import json

from utils import WPEFRAMEWORK_JSONRPC_URL

CALLSIGN = "org.rdk.AVInput"

# ---------------------------------------------------------------------------
# Constants used across test cases
# ---------------------------------------------------------------------------
TYPE_HDMI = "HDMI"
TYPE_COMPOSITE = "COMPOSITE"

EDID_VERSION_14 = "HDMI1.4"
EDID_VERSION_20 = "HDMI2.0"

GAME_FEATURE_ALLM = "ALLM"

# AVI content type integer values delivered by aviContentTypeUpdate
AVI_CONTENT_GRAPHICS = 0
AVI_CONTENT_PHOTO = 1
AVI_CONTENT_CINEMA = 2
AVI_CONTENT_GAME = 3
AVI_CONTENT_INVALID = 4

# Signal status strings delivered by onSignalChanged
SIGNAL_NO = "noSignal"
SIGNAL_UNSTABLE = "unstableSignal"
SIGNAL_NOT_SUPPORTED = "notSupportedSignal"
SIGNAL_STABLE = "stableSignal"


def _curl(method, params=None, timeout=5, request_id=42):
    """Build a curl command list for an org.rdk.AVInput JSON-RPC method."""
    payload = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": f"{CALLSIGN}.{method}",
    }
    if params is not None:
        payload["params"] = params
    return [
        "curl",
        "--max-time", str(timeout),
        "--header", "Content-Type: application/json",
        "--request", "POST",
        "--data", json.dumps(payload),
        WPEFRAMEWORK_JSONRPC_URL,
    ]


def _curl_raw(method, params=None, timeout=5, request_id=42):
    """Build a curl command list for an arbitrary JSON-RPC method (Controller/register)."""
    payload = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method,
    }
    if params is not None:
        payload["params"] = params
    return [
        "curl",
        "--max-time", str(timeout),
        "--header", "Content-Type: application/json",
        "--request", "POST",
        "-d", json.dumps(payload),
        WPEFRAMEWORK_JSONRPC_URL,
    ]


# ---------------------------------------------------------------------------
# Getters (safe, no side effects)
# ---------------------------------------------------------------------------
number_of_inputs = _curl("numberOfInputs")
current_video_mode = _curl("currentVideoMode")
get_supported_game_features = _curl("getSupportedGameFeatures")
get_arc_port_id = _curl("getARCPortId")


def get_input_devices(type_of_input=TYPE_HDMI):
    return _curl("getInputDevices", params={"typeOfInput": type_of_input})


def read_edid(port_id):
    return _curl("readEDID", params={"portId": int(port_id)})


def get_spd(port_id):
    return _curl("getSPD", params={"portId": int(port_id)})


def get_raw_spd(port_id):
    return _curl("getRawSPD", params={"portId": int(port_id)})


def get_edid_version(port_id):
    return _curl("getEdidVersion", params={"portId": int(port_id)})


def get_edid2_allm_support(port_id):
    return _curl("getEdid2AllmSupport", params={"portId": int(port_id)})


def get_vrr_support(port_id):
    return _curl("getVRRSupport", params={"portId": int(port_id)})


def get_vrr_frame_rate(port_id):
    return _curl("getVRRFrameRate", params={"portId": int(port_id)})


def get_hdmi_version(port_id):
    return _curl("getHdmiVersion", params={"portId": int(port_id)})


def get_game_feature_status(port_id, game_feature=GAME_FEATURE_ALLM):
    return _curl(
        "getGameFeatureStatus",
        params={"portId": int(port_id), "gameFeature": game_feature},
    )


# ---------------------------------------------------------------------------
# Setters / actions (builders)
# ---------------------------------------------------------------------------
def start_input(port_id, type_of_input=TYPE_HDMI, request_audio_mix=False, plane=0, top_most=False, timeout=8):
    return _curl(
        "startInput",
        params={
            "portId": int(port_id),
            "typeOfInput": type_of_input,
            "requestAudioMix": bool(request_audio_mix),
            "plane": plane,
            "topMost": bool(top_most),
        },
        timeout=timeout,
    )


def stop_input(type_of_input=TYPE_HDMI, timeout=8):
    return _curl("stopInput", params={"typeOfInput": type_of_input}, timeout=timeout)


def set_video_rectangle(x, y, w, h, type_of_input=TYPE_HDMI):
    return _curl(
        "setVideoRectangle",
        params={"x": x, "y": y, "w": w, "h": h, "typeOfInput": type_of_input},
    )


def write_edid(port_id, message):
    return _curl("writeEDID", params={"portId": int(port_id), "message": message})


def set_edid_version(port_id, edid_version):
    return _curl(
        "setEdidVersion",
        params={"portId": int(port_id), "edidVersion": edid_version},
    )


def set_edid2_allm_support(port_id, allm_support):
    return _curl(
        "setEdid2AllmSupport",
        params={"portId": int(port_id), "allmSupport": bool(allm_support)},
    )


def set_vrr_support(port_id, vrr_support):
    return _curl(
        "setVRRSupport",
        params={"portId": int(port_id), "vrrSupport": bool(vrr_support)},
    )


def set_mixer_levels(primary_volume, input_volume):
    return _curl(
        "setMixerLevels",
        params={"primaryVolume": primary_volume, "inputVolume": input_volume},
    )


def register_event(event_name, listener_id, timeout=8):
    return _curl_raw(
        f"{CALLSIGN}.1.register",
        params={"event": event_name, "id": listener_id},
        timeout=timeout,
    )


def controller_activate(callsign=CALLSIGN, timeout=8):
    return _curl_raw("Controller.1.activate", params={"callsign": callsign}, timeout=timeout)


def controller_deactivate(callsign=CALLSIGN, timeout=8):
    return _curl_raw("Controller.1.deactivate", params={"callsign": callsign}, timeout=timeout)


# ---------------------------------------------------------------------------
# Negative / malformed variants (invalid portId / typeOfInput) for robustness.
# Mirrors the malformed requests at the bottom of curl_extract.txt.
# ---------------------------------------------------------------------------
get_edid2_allm_support_invalid_port = _curl(
    "getEdid2AllmSupport", params={"portId": "S"}
)
set_edid2_allm_support_invalid_port = _curl(
    "setEdid2AllmSupport", params={"portId": "f", "allmSupport": True}
)
get_raw_spd_invalid_port = _curl("getRawSPD", params={"portId": "foo"})
get_spd_invalid_port = _curl("getSPD", params={"portId": "foo"})
get_input_devices_invalid_type = _curl(
    "getInputDevices", params={"typeOfInput": "ABCD"}
)
get_hdmi_version_invalid_port = _curl("getHdmiVersion", params={"portId": "foo"})


def get_edid_version_out_of_range(port_id=99):
    return _curl("getEdidVersion", params={"portId": int(port_id)})


def start_input_out_of_range(port_id=99):
    return _curl(
        "startInput",
        params={
            "portId": int(port_id),
            "typeOfInput": TYPE_HDMI,
            "requestAudioMix": False,
            "plane": 0,
            "topMost": False,
        },
    )


def start_input_invalid_type(port_id=0, type_of_input="INVALID_TYPE"):
    return _curl(
        "startInput",
        params={
            "portId": int(port_id),
            "typeOfInput": type_of_input,
            "requestAudioMix": False,
            "plane": 0,
            "topMost": False,
        },
    )


def stop_input_invalid_type(type_of_input="INVALID_TYPE"):
    return _curl("stopInput", params={"typeOfInput": type_of_input})


def set_edid_version_invalid(port_id=0, edid_version="HDMI9.9"):
    return _curl(
        "setEdidVersion",
        params={"portId": int(port_id), "edidVersion": edid_version},
    )


def set_vrr_support_invalid(port_id=-1, vrr_support=True):
    return _curl(
        "setVRRSupport",
        params={"portId": int(port_id), "vrrSupport": bool(vrr_support)},
    )


def set_video_rectangle_invalid_bounds():
    return _curl(
        "setVideoRectangle",
        params={"x": -1, "y": -1, "w": -10, "h": 0, "typeOfInput": TYPE_HDMI},
    )


def set_mixer_levels_invalid():
    return _curl(
        "setMixerLevels",
        params={"primaryVolume": -1, "inputVolume": 101},
    )
