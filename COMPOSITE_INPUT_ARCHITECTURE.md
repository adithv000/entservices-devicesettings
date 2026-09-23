# Composite Input Architecture

## Purpose

Composite Input is a DeviceSettings component that exposes composite video input discovery, selection, status, scaling, and notifications through `Exchange::IDeviceSettingsCompositeIn`. It is aggregated by the DeviceSettings Thunder plugin rather than deployed as an independent plugin.

The component supports two hardware backends:

- **AIDL HAL:** Uses the Polaris CompositeInput and PlaneControl Binder services.
- **Legacy DS HAL:** Uses dynamically resolved `dsCompositeIn*` APIs from `RDK_DSHAL_NAME`.

The backend is selected at runtime. If `ICompositeInputManager` is published, `CompositeIn::Create()` constructs `dCompositeInAIDLImpl`; otherwise it constructs `dCompositeInImpl`.

## System Architecture

```mermaid
flowchart TB
    Client[Client application]
    Thunder[Thunder Core]

    subgraph Plugin[DeviceSettings plugin process]
        DS[DeviceSettings<br/>IPlugin and interface aggregate]
        DSI[DeviceSettingsImp<br/>IDeviceSettingsCompositeIn]
        Component[DeviceSettingsCompositeInImpl<br/>delegation and notification registry]
        Facade[CompositeIn<br/>backend-independent facade and factory]
        Contract[hal::dCompositeIn::IPlatform]
        Factory{ICompositeInputManager<br/>service available?}
        AIDL[dCompositeInAIDLImpl]
        Legacy[dCompositeInImpl]
    end

    subgraph Polaris[Polaris AIDL services]
        Manager[ICompositeInputManager]
        Port[ICompositeInputPort]
        Controller[ICompositeInputController]
        Plane[IPlaneControl]
    end

    subgraph RDKV[Legacy RDK-V HAL]
        DSHAL[dsCompositeIn APIs]
        Hardware[Platform composite input hardware]
    end

    Client <-->|COM-RPC / generated JSON-RPC| Thunder
    Thunder <--> DS
    DS <--> DSI
    DSI <--> Component
    Component <--> Facade
    Facade --> Factory
    Factory -->|yes| AIDL
    Factory -->|no| Legacy
    AIDL -. implements .-> Contract
    Legacy -. implements .-> Contract
    Facade --> Contract
    AIDL <--> Manager
    Manager --> Port
    Port --> Controller
    AIDL <--> Plane
    Legacy <--> DSHAL
    DSHAL <--> Hardware
```

## Component Responsibilities

| Component | Responsibility |
| --- | --- |
| `DeviceSettings` | Manages Thunder lifecycle, aggregates `IDeviceSettingsCompositeIn`, and registers its notification sink. |
| `DeviceSettingsImp` | Implements the public Exchange interface and delegates Composite Input operations to the component implementation. |
| `DeviceSettingsCompositeInImpl` | Owns the `CompositeIn` facade, manages client notification registrations, and dispatches HAL events. |
| `CompositeIn` | Provides a backend-independent API, installs callback functions, and selects the AIDL or legacy backend. |
| `hal::dCompositeIn::IPlatform` | Defines the common lifecycle, query, selection, and scaling contract. |
| `dCompositeInAIDLImpl` | Maps the common contract to CompositeInput and PlaneControl AIDL services. |
| `dCompositeInImpl` | Maps the common contract to legacy `dsCompositeIn*` functions loaded from the DS HAL library. |

## Public Operations

| Exchange operation | Facade operation | AIDL mapping | Legacy mapping |
| --- | --- | --- | --- |
| Get input count | `GetNrOfCompositeInputs` | `ICompositeInputManager::getPortIds` | `dsCompositeInGetNumberOfInputs` |
| Get active status | `GetCompositeInStatus` | `getPortIds`, `getPort`, `ICompositeInputPort::getStatus` | `dsCompositeInGetStatus` |
| Start/select input | `SelectCompositeInPort(port)` | `getPort`, `registerEventListener`, `open`, `start` | `dsCompositeInSelectPort(port)` |
| Stop input | `SelectCompositeInPort(-1)` | `stop`, `unregisterEventListener`, `close` | `dsCompositeInSelectPort(-1)` |
| Set video rectangle | `ScaleCompositeInVideo` | `IPlaneControl::setPropertyMultiAtomic` with `X`, `Y`, `WIDTH`, and `HEIGHT` | `dsCompositeInScaleVideo` |

## Event Mapping

| Exchange notification | AIDL source | Legacy source |
| --- | --- | --- |
| `OnCompositeInHotPlug` | `ICompositeInputControllerListener::onConnectionChanged` | `dsCompositeInRegisterConnectCB` |
| `OnCompositeInSignalStatus` | `onSignalStatusChanged` | `dsCompositeInRegisterSignalChangeCB` |
| `OnCompositeInStatus` | `ICompositeInputEventListener::onStateChanged` | `dsCompositeInRegisterStatusChangeCB` |
| `OnCompositeInVideoModeUpdate` | `onVideoModeChanged` | `dsCompositeInRegisterVideoModeUpdateCB` |

## Class Diagram

```mermaid
classDiagram
    class DeviceSettings {
        -IDeviceSettingsCompositeIn* _mDeviceSettingsCompositeIn
        +Initialize(IShell*) string
        +Deinitialize(IShell*) void
    }

    class IDeviceSettingsCompositeIn {
        <<Exchange interface>>
        +Register(clientName, notification) hresult
        +Unregister(notification) hresult
        +GetNrOfCompositeInputs(count) hresult
        +GetCompositeInStatus(status) hresult
        +SelectCompositeInPort(port) hresult
        +ScaleCompositeInVideo(rectangle) hresult
    }

    class DeviceSettingsImp {
        -DeviceSettingsCompositeInImpl* _compositeInSettings
    }

    class DeviceSettingsCompositeInImpl {
        -CompositeIn _compositeIn
        -notificationList _CompositeInNotifications
        +Register(clientName, notification) hresult
        +Unregister(notification) hresult
        +dispatchCompositeInEvent()
    }

    class CompositeIn {
        -shared_ptr~IPlatform~ _platform
        -INotification& _parent
        +Create(parent)$ CompositeIn
        +Platform_init() void
        +GetNrOfCompositeInputs(count) uint32
        +GetCompositeInStatus(status) uint32
        +SelectCompositeInPort(port) uint32
        +ScaleCompositeInVideo(rectangle) uint32
    }

    class CompositeInNotification {
        <<CompositeIn::INotification>>
        +OnCompositeInHotPlug(port, connected) void
        +OnCompositeInSignalStatus(port, status) void
        +OnCompositeInStatus(port, presented) void
        +OnCompositeInVideoModeUpdate(port, resolution) void
    }

    class IPlatform {
        <<interface>>
        +InitialiseHAL() void
        +DeInitialiseHAL() void
        +setAllCallbacks(bundle) void
        +GetNrOfCompositeInputs(count)* uint32
        +GetCompositeInStatus(status)* uint32
        +SelectCompositeInPort(port)* uint32
        +ScaleCompositeInVideo(rectangle)* uint32
    }

    class dCompositeInAIDLImpl {
        -sp~Manager~ _manager
        -sp~Port~ _activePort
        -sp~Controller~ _controller
        -int32 _activePortId
        +IsAvailable()$ bool
        -StopActivePort() void
    }

    class ControllerListener {
        +onConnectionChanged(connected) Status
        +onSignalStatusChanged(status) Status
        +onVideoModeChanged(resolution) Status
    }

    class EventListener {
        +onStateChanged(oldState, newState) Status
        +onPropertyChanged(property, value) Status
    }

    class dCompositeInImpl {
        +resolve(library, symbol)$ void*
        -registerCompositeInEventCallbacks() void
    }

    DeviceSettings o-- IDeviceSettingsCompositeIn : aggregates
    DeviceSettingsImp ..|> IDeviceSettingsCompositeIn
    DeviceSettingsImp *-- DeviceSettingsCompositeInImpl
    DeviceSettingsCompositeInImpl *-- CompositeIn
    DeviceSettingsCompositeInImpl ..|> CompositeInNotification
    CompositeIn --> CompositeInNotification : callbacks
    CompositeIn o-- IPlatform
    dCompositeInAIDLImpl ..|> IPlatform
    dCompositeInImpl ..|> IPlatform
    dCompositeInAIDLImpl *-- ControllerListener
    dCompositeInAIDLImpl *-- EventListener
```

## Initialization And Backend Selection

```mermaid
sequenceDiagram
    participant DS as DeviceSettingsImp
    participant Component as DeviceSettingsCompositeInImpl
    participant Factory as CompositeIn::Create
    participant SM as Binder ServiceManager
    participant AIDL as dCompositeInAIDLImpl
    participant Legacy as dCompositeInImpl

    DS->>Component: Create()
    Component->>Factory: Create(componentNotification)
    Factory->>SM: checkService(ICompositeInputManager::serviceName)
    alt AIDL manager is available
        SM-->>Factory: manager binder
        Factory->>AIDL: construct
        AIDL->>SM: acquire manager
        AIDL->>AIDL: start Binder thread pool
        Factory-->>Component: CompositeIn(AIDL platform)
    else AIDL manager is unavailable
        SM-->>Factory: null
        Factory->>Legacy: construct
        Legacy->>Legacy: resolve and call dsCompositeInInit
        Factory-->>Component: CompositeIn(legacy platform)
    end
    Component->>Factory: install CallbackBundle
```

## Query Call Flow

The following flow applies to input-count and status queries. The facade does not expose backend-specific types to upper layers.

```mermaid
sequenceDiagram
    actor Client
    participant Plugin as DeviceSettings
    participant Impl as DeviceSettingsImp
    participant Component as DeviceSettingsCompositeInImpl
    participant Facade as CompositeIn
    participant Platform as IPlatform backend
    participant HAL as AIDL service or DS HAL

    Client->>Plugin: Composite Input query
    Plugin->>Impl: IDeviceSettingsCompositeIn method
    Impl->>Component: delegate method
    Component->>Facade: query
    Facade->>Platform: query
    Platform->>HAL: backend-specific API calls
    HAL-->>Platform: port IDs or status
    Platform-->>Facade: common type and Core error
    Facade-->>Component: result
    Component-->>Impl: result
    Impl-->>Plugin: hresult
    Plugin-->>Client: response
```

## AIDL Start And Stop Flow

Only one Composite Input port is active in the adapter at a time. Selecting another port first releases the current controller.

```mermaid
sequenceDiagram
    participant Facade as CompositeIn
    participant Adapter as dCompositeInAIDLImpl
    participant Manager as ICompositeInputManager
    participant Port as ICompositeInputPort
    participant Controller as ICompositeInputController

    Facade->>Adapter: SelectCompositeInPort(portId)
    Adapter->>Adapter: StopActivePort()
    Adapter->>Manager: getPort(portId)
    Manager-->>Adapter: Port
    Adapter->>Port: registerEventListener(EventListener)
    Adapter->>Port: open(ControllerListener)
    Port-->>Adapter: Controller
    Adapter->>Controller: start()
    Adapter-->>Facade: ERROR_NONE

    Note over Facade,Controller: Stop is represented by SelectCompositeInPort(-1)
    Facade->>Adapter: SelectCompositeInPort(-1)
    Adapter->>Controller: stop()
    Adapter->>Port: unregisterEventListener(EventListener)
    Adapter->>Port: close(Controller)
    Adapter->>Adapter: clear active references
    Adapter-->>Facade: ERROR_NONE
```

## Video Rectangle Flow

```mermaid
sequenceDiagram
    participant Client
    participant Facade as CompositeIn
    participant Adapter as dCompositeInAIDLImpl
    participant Plane as IPlaneControl

    Client->>Facade: ScaleCompositeInVideo(x, y, width, height)
    Facade->>Adapter: ScaleCompositeInVideo(rectangle)
    Adapter->>Plane: getVideoSourceDestinationPlaneMapping()
    Plane-->>Adapter: source-to-plane mappings
    Adapter->>Adapter: find COMPOSITE mapping for active port
    Adapter->>Plane: setPropertyMultiAtomic(plane, X/Y/WIDTH/HEIGHT)
    Plane-->>Adapter: applied
    Adapter-->>Facade: Core error code
    Facade-->>Client: result
```

## Asynchronous Event Flow

```mermaid
sequenceDiagram
    participant HAL as AIDL listener or DS callback
    participant Adapter as HAL adapter
    participant Facade as CompositeIn
    participant Component as DeviceSettingsCompositeInImpl
    participant Sink as Registered INotification clients

    HAL-->>Adapter: connection, signal, state, or video event
    Adapter->>Adapter: convert HAL types to Exchange types
    Adapter-->>Facade: CallbackBundle function
    Facade->>Component: CompositeIn::INotification callback
    Component->>Component: copy notification list under lock and AddRef
    loop each registered client
        Component-->>Sink: corresponding OnCompositeIn notification
        Component->>Component: Release client reference
    end
```

## Threading And Ownership

- `DeviceSettingsCompositeInImpl` protects notification registration with `_callbackLock` and holds an Exchange reference for each registered client.
- Event dispatch copies and `AddRef`s the client list before invoking callbacks, avoiding callbacks while the registration lock is held.
- `dCompositeInAIDLImpl` protects active Binder objects and selection operations with `_adminLock`.
- AIDL Binder callbacks run on the Binder thread pool started during adapter initialization.
- The AIDL adapter retains only the active `ICompositeInputPort`, controller, and listeners. Stop and destruction release them in reverse order.
- The legacy adapter serializes DS HAL operations with `dsCompositeInLock` and stores callback functions in its callback bundle bridge.

## Failure And Fallback Behavior

- AIDL selection occurs only when `ICompositeInputManager` is available during factory execution.
- If it is unavailable, construction falls back to the legacy DS HAL implementation.
- AIDL method failures are translated to `Core::ERROR_UNAVAILABLE`, `Core::ERROR_BAD_REQUEST`, or `Core::ERROR_GENERAL` as appropriate.
- Plane scaling requires both an active Composite Input port and a COMPOSITE source-to-plane mapping.
- Runtime loss of an AIDL service is reported by subsequent method failures; backend selection is not changed for an already constructed facade.

## Build Integration

AIDL support is compiled only when CMake finds all required generated interfaces and libraries:

- CompositeInput 0.2 headers and library
- PlaneControl 0.2 headers and library
- Common AIDL property types
- Android Binder headers, `libbinder`, and `libutils`

When found, the implementation target defines `ENABLE_COMPOSITEINPUT_AIDL`, adds `dCompositeInAIDLImpl.cpp`, and uses C++17 for generated `std::optional` and `std::variant` types. If any artifact is absent, the target remains legacy-only.