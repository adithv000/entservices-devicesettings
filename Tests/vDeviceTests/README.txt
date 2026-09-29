AVInput (HDMI Input) L3 vDevice Test Suite
==========================================

JSON-RPC functional test suite for the org.rdk.AVInput plugin (HDMI Input),
modelled on the PowerManager L3 vDevice suite.

EXECUTION
---------
cd source_code/AVInput_vDeviceTests

with timing   : python3 SuiteManager.py -t devicesettings
without timing: python3 SuiteManager.py devicesettings

Scenario suite (multi-step user journeys):
  python3 SuiteManager.py devicesettings_scenarios
  python3 SuiteManager.py -t devicesettings_scenarios

Run a single test (repeatable):
  python3 SuiteManager.py devicesettings --test TCID03_StartStopInput
  python3 SuiteManager.py devicesettings_scenarios --test TCID13_EdidProvisioningWorkflow

Default Actions:
Plugin activation and deactivation are done around every testcase by default:
- Controller.1.activate(callsign=org.rdk.DeviceSettings)
- Controller.1.activate(callsign=org.rdk.AVInput)
- Init_AVInput_Populate establishes a known baseline (reads numberOfInputs /
  getInputDevices and issues a best-effort stopInput).
- The testcase runs.
- org.rdk.AVInput and org.rdk.DeviceSettings are deactivated in reverse order.

Disable automatic plugin lifecycle management only if needed:
- export AUTO_ACTIVATE_PLUGINS=0

ENDPOINTS / DEFAULTS
--------------------
- MW JSON-RPC        : http://127.0.0.1:9998/jsonrpc
- HDMI vComponent   : http://127.0.0.1:8082/api/postKVP

Useful overrides:
- TARGET_HOST                 (applies to the endpoint)
- JSONRPC_PORT
- WPEFRAMEWORK_JSONRPC_URL     (full URL, highest priority)
- AVINPUT_EVENT_TIMEOUT        (seconds to wait for each notification; default 8)

TCID27 and TCID28 capture JSON-RPC notifications over a persistent WebSocket
implemented with the Python standard library; no pip packages are required.

Examples:

# inside QEMU guest (services on localhost)
python3 SuiteManager.py devicesettings

# from host against QEMU target IP
export TARGET_HOST=127.0.0.1
export JSONRPC_PORT=9998
python3 SuiteManager.py devicesettings
# then forward the port via QEMU:
#   hostfwd=tcp:127.0.0.1:9998-:9998
# and set WPEFramework "binding":"0.0.0.0" in /etc/WPEFramework/config.json.

TEST CASES
----------
TCID01_NumberOfInputs          - numberOfInputs returns a valid port count.
TCID02_GetInputDevices         - getInputDevices lists id/locator/connected.
TCID03_StartStopInput          - startInput/stopInput + onInputStatusChanged.
TCID04_EdidVersionRoundTrip    - set/get EDID version (HDMI2.0 / HDMI1.4).
TCID05_ReadWriteEdid           - readEDID / writeEDID / re-read round-trip.
TCID06_Edid2AllmSupport        - EDID-2.0 gated ALLM-in-EDID set/get.
TCID07_GameFeatureAllmStatus   - getSupportedGameFeatures + getGameFeatureStatus.
TCID08_HdmiVersion             - getHdmiVersion returns capability version.
TCID09_SetVideoRectangle       - setVideoRectangle (full-screen + PIP).
TCID10_InvalidParameterHandling- malformed portId/typeOfInput handled gracefully.

SCENARIO TEST CASES (suite: devicesettings_scenarios)
----------------------------------------------
Multi-step user journeys chaining several APIs, with baseline capture and
restore in a finally block so each scenario leaves the device as it found it.

TCID11_PortEnumerationConsistency - numberOfInputs vs getInputDevices cross-check
                                   (count, contiguous ids, locator/id match).
TCID12_InputSwitchingLifecycle    - direct source switching across all ports
                                   without an intervening stop + stop idempotency.
TCID13_EdidProvisioningWorkflow   - readEDID -> setEdidVersion -> writeEDID ->
                                   re-read; validates base64 + 128-byte blocks.
TCID14_GameModeProvisioning       - Game Mode on/off via ALLM-in-EDID, with ALLM
                                   status queried in both states.
TCID15_VrrProvisioning            - VRR advertisement toggled on/off and verified.
TCID16_MultiPortIndependence      - opposing settings on 2 ports; asserts no
                                   cross-contamination between ports.
TCID17_PresentationWindowWorkflow - full-screen -> PIP -> move -> restore -> stop,
                                   plus post-stop geometry call handling.
TCID18_SourceInspectionWorkflow   - one-pass diagnostics report (HDMI version,
                                   SPD, raw SPD, and video mode).
TCID19_AudioMixingWorkflow        - requestAudioMix true/false + mixer level sweep
                                   including both 0/100 extremes.
TCID20_NegativeAndBoundaryHandling- non-numeric + out-of-range portId, unknown
                                   typeOfInput, invalid edidVersion, then health.
TCID21_DisconnectedPortBehaviour  - locks in graceful degradation on a port with
                                   no source (empty video mode, not errors).
TCID22_AidlEventCoverage          - injects connection, signal, VIC, VRR, AVI,
                                   audio, SPD, DRM, VSIF, and HDCP events.
TCID27_StableSignalState          - drives HDMI signal transitions and captures
                                   each onSignalChanged notification.
TCID28_NoSigToStableSignalTransition - captures no-signal, unstable-signal, and
                                      stable-signal notifications in order.
TCID29_InvalidPortStartStopInput  - rejects startInput with invalid portId 10,
                                   then verifies normal stop/start still works.
TCID30_AviInfoFrameNotification  - posts the AVI InfoFrame YAML and captures the
                                   aviContentTypeUpdate notification.

VERIFIED JSON-RPC CONTRACT
--------------------------
Parameter spelling and response shapes in AVInput_Curl.py / AVInput_Helpers.py
were confirmed against a live vDevice run (wpeframework_vdevice.log, 21/21 pass):

  portId          : INTEGER (e.g. 0), not a string
  startInput      : {portId, typeOfInput, requestAudioMix, plane, topMost}
  getInputDevices : returns the same list under BOTH "devices" and "deviceList"
  numberOfInputs  : {"numberOfInputs":2,"success":true}
  readEDID        : {"EDID":"<base64>","success":true}
  getEdidVersion  : {"edidVersion":"HDMI2.0","success":true}
  getEdid2AllmSupport : {"allmSupport":true,"success":true}
  getVRRSupport   : {"vrrSupport":false,"success":true}
  getHdmiVersion  : {"HdmiCapabilityVersion":"2.1","success":true}
  getSPD/getRawSPD: {"HDMISPD":"...","success":true}
  currentVideoMode: {"currentVideoMode":"","success":true}  <- empty w/o a source

Known vDevice behaviours the scenarios accommodate:
  - Both HDMI ports report connected:false (no physical source on the bench).
  - startInput succeeds even on a disconnected port.
  - currentVideoMode is an empty string when no source is attached.
  - getSupportedGameFeatures returns {"supportedGameFeatures":[],"success":false}
    on the vDevice, so scenarios rely on getGameFeatureStatus instead.

NOTE ON ASYNC EVENTS
--------------------
TCID22 posts connection, signal, video format, VRR, and InfoFrame stimuli to
the HDMI Input vComponent. JSON is sent as valid YAML directly to postKVP, so
the test does not depend on local command-template files. Some vComponent
builds apply a command and close the connection without a response; curl error
52 is therefore treated as accepted by the transport helper.

NOTES
-----
- JSON-RPC parameter spelling in AVInput_Curl.py follows the working curl set in
  curl_extract.txt (audioMix / planeType / topMost / portId as string). If a
  target build exposes different parameter spelling, adjust the builders in
  AVInput_Curl.py (single source of truth).

Troubleshooting:
- On "connection refused", verify WPEFramework JSON-RPC is reachable using the
  endpoint overrides above.
