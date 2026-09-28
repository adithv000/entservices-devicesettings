"""
/**
 * @file AVInput_Helpers.py
 * @brief Response parsers for AVInput (HDMI Input) JSON-RPC results.
 *
 * @testcase AVInput_Helpers
 * @details Shared helpers that extract typed values from org.rdk.AVInput
 *          JSON-RPC responses (device lists, success flags, EDID/version/VRR
 *          fields) so the individual test cases stay focused on scenario logic.
 */
"""

from utils import parse_result


def result_success(curl_response):
    """True when result is a dict with success == True (SuccessResult shape)."""
    result = parse_result(curl_response)
    return isinstance(result, dict) and result.get("success") is True


def parse_number_of_inputs(curl_response):
    """Return numberOfInputs as int, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("numberOfInputs")
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return None


def parse_input_devices(curl_response):
    """Return the list of {id, locator, connected} devices, or None.

    The plugin returns the same list under both 'devices' and 'deviceList'.
    """
    result = parse_result(curl_response)
    if not isinstance(result, dict):
        return None
    devices = result.get("devices")
    if not isinstance(devices, list):
        devices = result.get("deviceList")
    return devices if isinstance(devices, list) else None


def get_device_connected(devices, port_id):
    """Return the 'connected' flag for the given port id from a device list."""
    if not isinstance(devices, list):
        return None
    for dev in devices:
        if isinstance(dev, dict) and str(dev.get("id")) == str(port_id):
            return dev.get("connected")
    return None


def parse_current_video_mode(curl_response):
    """Return the currentVideoMode string, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("currentVideoMode")
        if isinstance(value, str):
            return value
    return None


def parse_content_protected(curl_response):
    """Return the isContentProtected bool, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("isContentProtected")
        if isinstance(value, bool):
            return value
    return None


def parse_edid_version(curl_response):
    """Return the edidVersion string, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("edidVersion")
        if isinstance(value, str):
            return value
    return None


def parse_allm_support(curl_response):
    """Return the allmSupport bool, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("allmSupport")
        if isinstance(value, bool):
            return value
    return None


def parse_vrr_support(curl_response):
    """Return the vrrSupport bool, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("vrrSupport")
        if isinstance(value, bool):
            return value
    return None


def parse_vrr_frame_rate(curl_response):
    """Return the currentVRRVideoFrameRate as float, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("currentVRRVideoFrameRate")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            return float(value)
    return None


def parse_hdmi_version(curl_response):
    """Return the HdmiCapabilityVersion string, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("HdmiCapabilityVersion")
        if isinstance(value, str):
            return value
    return None


def parse_game_feature_mode(curl_response):
    """Return the game feature 'mode' bool (e.g. ALLM status), or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("mode")
        if isinstance(value, bool):
            return value
    return None


def parse_supported_game_features(curl_response):
    """Return the supportedGameFeatures list of strings, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("supportedGameFeatures")
        if isinstance(value, list):
            return value
    return None


def parse_spd(curl_response):
    """Return the HDMISPD string (SPD / raw SPD), or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("HDMISPD")
        if isinstance(value, str):
            return value
    return None


def parse_edid(curl_response):
    """Return the EDID string from readEDID, or None."""
    result = parse_result(curl_response)
    if isinstance(result, dict):
        value = result.get("EDID")
        if isinstance(value, str):
            return value
    return None
