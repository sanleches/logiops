# Context

## Build Entry Points
- `CMakeLists.txt`: sets project versioning, compiler flags, submodule initialization, and adds `src/ipcgull` and `src/logid`.
- `src/logid/CMakeLists.txt`: builds the `logid` daemon, links external libraries, and installs systemd / D-Bus policy files.
- `src/ipcgull/CMakeLists.txt`: builds the IPC abstraction library and selects either the GDBus or stub backend.

## `src/logid`
- `logid.cpp`: daemon entry point, CLI parsing, config loading, worker initialization, IPC server startup, and virtual input setup.
- `Configuration.*`: reads and stores the runtime configuration.
- `DeviceManager.*`: owns device discovery, device lifecycle, and async coordination.
- `Device.*` / `Receiver.*` / `InputDevice.*`: device-facing runtime objects.
- `backend/`: low-level HID++ and raw device implementations.
- `actions/`: user-configurable behaviors triggered by device input.
- `features/`: high-level features exposed by devices.
- `util/`: logging, background tasks, and exception handling.

## Feature Layers
- `features/*`: thin wrappers that bind a profile config to a backend feature, then expose IPC setters/getters and event handling.
- `backend/hidpp20/features/*`: protocol-layer implementations, often with versioned subclasses and `autoVersion()` helpers.
- Several protocol helpers preserve unknown device bits when updating mode flags, so the driver does not clobber unsupported capabilities.
- Some payloads have special encodings: DPI range markers at `>= 0xe000`, chunked device names, and host-switch calls that intentionally use no response.

## `src/ipcgull`
- Public API lives in `src/include/ipcgull/*.h`.
- `node.cpp`, `interface.cpp`, `property.cpp`, `signal.cpp`, `function.cpp`, `variant.h`: object model for exported IPC nodes.
- `server_gdbus.cpp` and `common_gdbus.*`: GDBus-backed server implementation.
- `server_stub.cpp`: stub backend used when IPC is disabled or unavailable.

## Important Runtime Flow
- `logid.cpp` parses CLI flags before loading config.
- `Configuration` determines worker count and device/action behavior.
- `DeviceManager` enumerates devices and coordinates backend-specific objects.
- `ipcgull::make_server(...)` creates the IPC server used for runtime control.
- `InputDevice` creates the virtual device that actions write into.

## Main Objects
- `DeviceManager` owns the global device and receiver maps, plus the IPC roots that expose them.
- `Device` represents one logical Logitech device, its active profile, and the feature wrappers attached to it.
- `Receiver` represents a wireless receiver and turns connection events into child `Device` objects.
- `InputDevice` is the shared virtual `uinput` target for synthesized keyboard and mouse events.
- Feature wrappers under `src/logid/features` bridge profile config to hardware features.
- Protocol features under `src/logid/backend/hidpp20/features` speak raw HID++ and translate payloads into structured state.
- `backend/raw::RawDevice` owns the hidraw fd, the report descriptor, and the lowest-level report callbacks.
- `backend/raw::DeviceMonitor` owns the udev hotplug watch and retries devices that are not ready yet.
- `backend::hidpp::Device` promotes raw devices into HID++ devices and filters request/response traffic.
- `backend::hidpp20::Device` adds feature-call multiplexing so multiple HID++ 2.0 requests can be in flight.
- `backend::hidpp20::Feature` resolves runtime feature indices from the ROOT feature before making calls.
- `actions::Action` is the IPC-exported base class for all user actions.
- `GestureAction` builds a gesture subtree and chooses a direction from raw movement.

## Notes For Commenting
- Favor comments that explain intent, assumptions, and side effects.
- Keep comments short and local to the logic they describe.
- Prefer documenting complex control flow, ownership, and backend selection over restating obvious syntax.
- When the code handles a special case, mention why the special case exists, not just what it does.
- For low-level backend code, explain request/response matching, retry logic, and why certain reports are ignored.

## Documentation Structure
- Use a short file header at the top of each source file that says what the file owns.
- For functions, prefer a compact block that covers purpose, inputs, outputs, and the main call path or references.
- Include `Used by` or `Referenced by` when a function is an important handoff point in the runtime flow.
- Comment the logic inside a function when an operation is non-obvious, especially retries, feature detection, response matching, lock ordering, or fallback behavior.
- Keep the wording concise, but do not omit the reason behind a branch, loop, or special case.
- For getters and one-line wrappers, a short purpose line is enough unless they are part of core runtime flow.

## Logic Finding Notes
- `logid.cpp` is the startup spine: CLI, config, worker pool, IPC server, virtual input, then hotplug enumeration.
- `DeviceManager` is the device/receiver router: it classifies hidraw nodes, owns lifetime maps, and publishes IPC updates.
- `Device` is the profile/application hub: it owns the active profile, feature wrappers, reset handling, and IPC status.
- `Receiver` is the wireless slot manager: it turns connection events into child `Device` objects and exposes pairing/discovery state.
- `backend/raw` is the lowest I/O layer: open fd, epoll monitor, udev hotplug, report descriptor, and raw report dispatch.
- `backend/hidpp` promotes raw devices into HID++ devices, matches responses by sub-ID/address, and handles timeouts.
- `backend/hidpp20` multiplexes feature calls by feature index and resolves runtime feature IDs through ROOT.
- `features/*` are thin config-to-hardware bridges; they translate profile state into concrete backend feature calls.
- `actions/*` are button behaviors; most follow the pattern `construct -> press -> release -> optional IPC setters/getters`.
- `ipcgull` maps the C++ object tree onto D-Bus: nodes own interfaces, interfaces own methods/properties/signals, and the server backend performs registration and dispatch.
- `Configuration` wraps libconfig runtime state; `save()` serializes the current in-memory model back to disk.
- `InputDevice` owns the synthetic uinput target and rebuilds it when new keys/axes must be exported.

## File Hunting Notes
- Main runtime behavior usually lives in `.cpp` files with matching headers in the same directory.
- Feature wrappers are usually thin and live under `src/logid/features` or `src/logid/backend/hidpp20/features`.
- Action classes usually have small constructors plus `press()`, `release()`, and IPC getter/setter methods.
- When looking for the actual behavior of a feature, check the backend module first, then the corresponding action or wrapper.
