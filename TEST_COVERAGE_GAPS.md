# DeviceSettings L1/L2 Test Coverage Gaps

Snapshot of `Exchange::IDeviceSettingsXxx` methods **not** exercised by
`Tests/L1Tests/tests/test_DeviceSettingsXxx.cpp` and/or
`Tests/L2Tests/tests/DeviceSettings_L2Test.cpp`, across all 8 DeviceSettings
components. `Register`/`Unregister` notification methods are excluded from
this list for every component.

Legend: **L1 gap** = missing from the L1 test file. **L2 gap** = missing from
the L2 test file. **Both** = not tested at either level.

## FPD

Fully covered at both L1 and L2.

## Host

Fully covered at both L1 and L2 (`GetEDID`, `GetMS12ConfigType`).

## Display

- **Both**: `GetDisplayEdid` — requires hand-populating the real vendor
  `dsDisplayEDID_t` HAL struct plus a supported-resolution iterator; the
  exact struct field layout isn't available in this workspace
  (`rdk-halif-device_settings` not checked out here), so it was not faked.

All other methods (`GetDisplayEdidBytes`, `SetAVIContentType`,
`SetAVIScanInformation`) are now covered at both L1 and L2.

## CompositeIn

Fully covered at both L1 and L2 (`ScaleCompositeInVideo` added to L1).

## Audio

L1 only covers `GetAudioPort`, `SetAudioMute`, `IsAudioMuted`,
`SetAudioLevel`, `GetAudioLevel`, `SetAudioDelayOffset`/`GetAudioDelayOffset`
— everything else below is an L1 gap in addition to whatever is marked for L2.

- **Both**:
  - `GetMS12Capabilities` (iterator)
  - `GetSupportedCompressions` (iterator)
  - `GetAudioMS12ProfileList` (iterator)
  - `GetAudioEncoding` (non-obvious: derived from `dsGetStereoMode`, not a
    direct HAL call — needs dedicated verification of the derivation logic)
  - `GetAudioCompression` / `SetAudioCompression` (the `AudioCompression`
    enum overload — only the `int32_t compressionLevel` overload is tested)
  - `SetAudioDucking`
  - `SetSAD` (takes an opaque `dsAudioSADList_t` HAL list struct)
  - `GetAudioEnablePersist` / `SetAudioEnablePersist`
  - `SetAudioMS12SettingsOverride`
  - `ResetAudioDialogEnhancement`, `ResetAudioBassEnhancer`,
    `ResetAudioSurroundVirtualizer`, `ResetAudioVolumeLeveller`
  - `GetAudioHDMIARCPortId`

~~`SetAudioDelayOffset`/`GetAudioDelayOffset` had no mock method in
`DsAudioHALMock.h`~~ — fixed: added `dsSetAudioDelayOffset`/
`dsGetAudioDelayOffset` MOCK_METHODs + extern "C" definitions to
`entservices-testframework`, and both methods are now covered at L1 and L2.

~~`IsAudioMuted` was L2 gap only~~ — fixed, now covered in L2 too.

## VideoPort

L1 only covers `GetVideoPort`, `IsVideoPortEnabled`, `EnableVideoPort` —
everything else below is an L1 gap in addition to whatever is marked for L2.

- **Both**:
  - `GetVideoPortResolutionConfig` (iterator)
  - `GetVideoPortResolution` / `SetVideoPortResolution` (multi-step
    cache + connection-state + force-disable-4K logic; too complex to
    safely fake without further verification)
  - `GetCurrentOutputSettings` (5 simultaneous out-params)

~~`IsVideoPortEnabled` was L2 gap only~~ — fixed, now covered in L2 too.

## VideoDevice

L1 only covers `GetVideoDeviceHandle`, `SetVideoDeviceDFC`,
`GetVideoDeviceDFC`, `GetHDRCapabilities` — everything else below is an L1
gap in addition to whatever is marked for L2.

- **Both**: `GetCodecInfo` (iterator)

## HdmiIn

L1 only covers `GetHDMIInNumberOfInputs`, `SelectHDMIInPort`,
`GetHDMIInAllmStatus` — everything else below is an L1 gap in addition to
whatever is marked for L2.

- **Both**:
  - `GetHDMIInStatus` (iterator)
  - `GetSupportedGameFeaturesList` (iterator)
  - `GetHDMIVideoMode` (unverified `dsVideoPortResolution_t` HAL struct
    field layout)
  - `GetVRRStatus` (unverified `dsHdmiInVrrStatus_t` HAL struct field
    layout)
  - `GetHDMIInEdid2AllmSupport` (only the `Set` counterpart is tested;
    `Get` reads from persistence with a HAL fallback not yet verified)

## Summary of universally-skipped categories

1. **Iterator-returning methods** (`IDeviceSettingsXxxIterator*&` out-params)
   across all components — these need COM-RPC iterator construction helpers
   that weren't built out as part of this pass.
2. **Opaque/complex HAL structs** whose field layout isn't available in this
   workspace (`dsAudioSADList_t`, `dsDisplayEDID_t`, `dsVideoPortResolution_t`,
   `dsHdmiInVrrStatus_t`) — the real headers live in `rdk-halif-device_settings`,
   which isn't checked out alongside this repo in this environment.
3. **Persistence-only paths with no direct HAL mock counterpart**
   (`GetAudioEnablePersist`/`SetAudioEnablePersist`, `GetHDMIInEdid2AllmSupport`).
4. **Reset-style helpers** (`Reset*` in Audio) with no distinct HAL call
   visible in the mock surface.

## entservices-testframework mock completeness audit (feature/RDKEMW-25013)

Cross-checked every HAL C function actually called from
`entservices-devicesettings/plugin/hal/dXxxImpl.h` against the `MOCK_METHOD`
surface in `entservices-testframework/Tests/mocks/devicesettings/DsXxxHALMock.h`
for all 8 components.

- **Fixed**: `dsSetAudioDelayOffset`/`dsGetAudioDelayOffset` were called by
  `dAudioImpl.h` but had no mock at all (would silently miss HAL-level
  verification). Added both `MOCK_METHOD`s and extern "C" forwarding
  definitions to `DsAudioHALMock.h`/`.cpp`.
- **Investigated, not changed**: `dHdmiInImpl.h` resolves `dsIsHdmiARCPort`
  through a `bool(*)(int, bool*)` function-pointer typedef, but the real
  vendor HAL (`rdk-halif-device_settings/include/dsHdmiIn.h`, confirmed via
  the public GitHub source) declares it as
  `dsError_t dsIsHdmiARCPort(dsHdmiInPort_t, bool*)`. This is a latent bug in
  `dHdmiInImpl.h`'s typedef (plugin code, not mock code) — left the mock
  matching the real HAL spec rather than matching the plugin's incorrect cast.
  Worth a follow-up fix in `entservices-devicesettings` itself.
- **Everything else**: all other HAL functions called by all 8 components
  already had correct, matching mocks — no further mock-surface gaps found.
