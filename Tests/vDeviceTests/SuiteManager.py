"""
/**
 * @file SuiteManager.py
 * @brief SuiteManager.py
 *
 * @testcase SuiteManager
 * @details Orchestrates the AVInput (HDMI Input) L3 test suite by dynamically loading
 *          and executing test case modules, activating the org.rdk.DeviceSettings and
 *          org.rdk.AVInput plugins via
 *          JSON-RPC, and reporting per-test pass/fail results with summary statistics.
 *
 * @precondition
 *  - WPEFramework is running and reachable at the configured JSON-RPC endpoint.
 *  - The org.rdk.AVInput plugin is available for activation.
 *  - All test case modules listed in SUITES are present under the Testcases/ directory.
 *
 * @dependencies
 *  - utils.py
 *  - AVInput_Curl.py
 *  - AVInput_Helpers.py
 *  - Testcases/*.py
 *
 * @expected_result
 *  - All registered test cases are executed in order and results are logged.
 *
 * @pass_criteria
 *  - Each test case module's run_test() returns True and is reported as PASSED.
 *
 * @failure_criteria
 *  - Any test case returns False, raises an exception, or the plugin fails to activate.
 */
"""

import importlib
import io
import sys
import time
from pathlib import Path
import os

from utils import (
    activate_plugin,
    deactivate_plugin,
    log_error,
    log_info,
    log_success,
    WPEFRAMEWORK_JSONRPC_URL,
)


BASE_DIR = Path(__file__).resolve().parent
SUITES = {
    "devicesettings": {
        "banner": "******************** L3 SUITE - RDK - AV INPUT (HDMI IN) **********************",
        "module_dir": BASE_DIR / "Testcases",
        "tests": [
            # Functional API coverage
            "TCID01_NumberOfInputs",
            "TCID02_GetInputDevices",
            "TCID03_StartStopInput",
            "TCID04_EdidVersionRoundTrip",
            "TCID05_ReadWriteEdid",
            "TCID06_Edid2AllmSupport",
            "TCID07_GameFeatureAllmStatus",
            "TCID08_HdmiVersion",
            "TCID09_SetVideoRectangle",
            "TCID10_InvalidParameterHandling",
            # Scenario / multi-step user journeys
            "TCID11_PortEnumerationConsistency",
            "TCID12_InputSwitchingLifecycle",
            "TCID13_EdidProvisioningWorkflow",
            "TCID14_GameModeProvisioning",
            "TCID15_VrrProvisioning",
            "TCID16_MultiPortIndependence",
            "TCID17_PresentationWindowWorkflow",
            "TCID18_SourceInspectionWorkflow",
            "TCID19_AudioMixingWorkflow",
            "TCID20_NegativeAndBoundaryHandling",
            "TCID21_DisconnectedPortBehaviour",
            "TCID22_AidlEventCoverage",
            # vComponent YAML-driven state injection + JSON-RPC verification
            "TCID23_ConnectionStatusUpdate",
            "TCID24_VideoFormatChange",
            "TCID25_VrrStatusUpdate",
            "TCID26_SpdInfoFrameReadback",
            "TCID27_StableSignalState",
            "TCID28_NoSigToStableSignalTransition",
            "TCID29_InvalidPortStartStopInput",
            "TCID30_AviInfoFrameNotification",
        ],
    },
}

# Maps test suite names to their corresponding RDK plugin callsigns for activation.
SUITE_PLUGIN_CALLSIGNS = {
    "devicesettings": "org.rdk.AVInput",
}

SUITE_PREREQUISITE_CALLSIGNS = {
    "devicesettings": ["org.rdk.DeviceSettings"],
}

SUITE_INIT_MODULES = {
    "devicesettings": "Init_AVInput_Populate",
}


def normalize_suite_name(raw_name):
    return raw_name.strip().replace("_", "").replace("-", "").lower()


def normalize_test_name(raw_name):
    return raw_name.strip().replace("_", "").replace("-", "").lower()


def resolve_selected_tests(suite_name, requested_tests):
    if not requested_tests:
        return list(SUITES[suite_name]["tests"])

    available_tests = SUITES[suite_name]["tests"]
    resolved = []

    for requested in requested_tests:
        requested_norm = normalize_test_name(requested)
        matches = [name for name in available_tests if normalize_test_name(name) == requested_norm]
        if not matches:
            raise ValueError(f"Unknown test '{requested}'. Available tests: {available_tests}")
        resolved.extend(matches)

    return resolved


def load_test_cases(suite_name, selected_tests=None):
    suite_config = SUITES[suite_name]
    module_dir = str(suite_config["module_dir"])

    if module_dir not in sys.path:
        sys.path.insert(0, module_dir)

    test_cases = []
    for module_name in resolve_selected_tests(suite_name, selected_tests):
        module = importlib.import_module(module_name)
        run_fn = getattr(module, "run_test", None)
        if not callable(run_fn):
            module_file = getattr(module, "__file__", "<unknown>")
            exported = [name for name in dir(module) if not name.startswith("__")]
            raise AttributeError(
                f"Module '{module_name}' from '{module_file}' does not expose callable run_test(). "
                f"Exported names: {exported}"
            )
        test_cases.append((module_name, run_fn))

    return suite_config["banner"], test_cases


def run_suite_init(suite_name):
    module_name = SUITE_INIT_MODULES.get(suite_name)
    if not module_name:
        return True

    if BASE_DIR.as_posix() not in sys.path:
        sys.path.insert(0, str(BASE_DIR))

    try:
        module = importlib.import_module(module_name)
    except Exception as exc:
        log_error(f"Init module import failed: {module_name} ({exc})")
        return False

    run_fn = getattr(module, "run_test", None)
    if not callable(run_fn):
        log_error(f"Init module missing run_test(): {module_name}")
        return False

    log_info(f"Running suite initialization: {module_name}.run_test()")
    try:
        ok = bool(run_fn())
    except Exception as exc:
        log_error(f"Suite initialization threw exception: {exc}")
        return False

    if ok:
        log_success("Suite initialization completed successfully")
    else:
        log_error("Suite initialization failed")
    return ok


def activate_test_plugins(suite_name):
    callsigns = list(SUITE_PREREQUISITE_CALLSIGNS.get(suite_name, []))
    plugin_callsign = SUITE_PLUGIN_CALLSIGNS.get(suite_name)
    if plugin_callsign:
        callsigns.append(plugin_callsign)

    activated = []
    for callsign in callsigns:
        log_info(
            f"Auto-activating plugin '{callsign}' "
            f"via curl JSON-RPC at {WPEFRAMEWORK_JSONRPC_URL}"
        )
        if not activate_plugin(callsign):
            log_error(f"Plugin activation failed: {callsign}")
            return False, activated
        activated.append(callsign)
        log_success(f"Plugin activated: {callsign}")
        log_info("Waiting 6s for plugin to fully initialise...")
        time.sleep(6)

    return True, activated


def deactivate_test_plugins(callsigns):
    success = True
    for callsign in reversed(callsigns):
        log_info(f"Auto-deactivating plugin '{callsign}'")
        if deactivate_plugin(callsign):
            log_success(f"Plugin deactivated: {callsign}")
        else:
            log_error(f"Plugin deactivation failed: {callsign}")
            success = False
    return success


def run_suite(suite_name, selected_tests=None):
    banner, test_cases = load_test_cases(suite_name, selected_tests)
    print(banner)

    auto_activate = os.environ.get("AUTO_ACTIVATE_PLUGINS", "1").lower() not in ("0", "false", "no")

    passed = 0
    failed = 0
    failed_cases = []
    original_stdout = sys.stdout

    for tc_name, tc_fn in test_cases:
        log_info(f"\n{'='*60}")
        log_info(f"Running: {tc_name}")
        log_info(f"{'='*60}")
        captured = io.StringIO()
        sys.stdout = captured
        activated_callsigns = []
        try:
            plugins_ready = True
            if auto_activate:
                plugins_ready, activated_callsigns = activate_test_plugins(suite_name)

            if not plugins_ready:
                log_error("Testcase setup failed because plugin activation did not complete.")
                result = False
            elif not run_suite_init(suite_name):
                log_error("Testcase setup failed because initialization did not complete.")
                result = False
            else:
                result = tc_fn()
        except Exception as exc:
            result = False
            print(f"EXCEPTION in {tc_name}: {exc}")
        finally:
            if auto_activate and not deactivate_test_plugins(activated_callsigns):
                result = False
            sys.stdout = original_stdout

        output = captured.getvalue()
        print(output, end="")

        if result:
            passed += 1
            log_success(f"[PASS] {tc_name}")
        else:
            failed += 1
            failed_cases.append(tc_name)
            log_error(f"[FAIL] {tc_name}")

        time.sleep(1)

    log_info(f"\n{'='*60}")
    log_info(f"Suite Summary: {passed} passed, {failed} failed")
    if failed_cases:
        log_error(f"Failed cases: {failed_cases}")
    log_info(f"{'='*60}")
    return failed == 0


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Run AVInput (HDMI Input) L3 test suites")
    parser.add_argument("suite", help=f"Test suite name. Available: {list(SUITES.keys())}")
    parser.add_argument("-t", "--timing", action="store_true", help="Enable timing output for passed test cases")
    parser.add_argument(
        "--test",
        dest="tests",
        action="append",
        help="Run only the named testcase module. Repeat to run multiple specific tests.",
    )

    args = parser.parse_args()

    if args.timing:
        os.environ["AVINPUT_TIMING_ENABLED"] = "1"

    suite_arg = normalize_suite_name(args.suite)
    matching = [k for k in SUITES if normalize_suite_name(k) == suite_arg]
    if not matching:
        log_error(f"Unknown suite '{args.suite}'. Available: {list(SUITES.keys())}")
        sys.exit(1)

    try:
        ok = run_suite(matching[0], args.tests)
    except (ValueError, AttributeError) as exc:
        log_error(str(exc))
        sys.exit(1)
    sys.exit(0 if ok else 1)
