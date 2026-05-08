# Luanode Upstream Analysis

## Sources

- Official Lua: https://github.com/lua/lua/tree/v5.5.0
- TuyaOpen reference: https://gitee.com/tuya-open/luanode-TuyaOpen

## Import Policy

Lua official source is imported as a source snapshot under `src/luanode/lua/`.
TuyaOpen-specific code is not copied blindly; only architecture ideas are referenced and reimplemented under TuyaOpen coding rules.

## Official Lua 5.5.0 Snapshot

The `v5.5.0` tag resolves to `a5522f06d2679b8f18534fd6a9968f7eb539dc31`.

The component should compile Lua core and library sources required by embedded execution. Standalone tool sources such as `lua.c`, `luac.c`, and `ltests.c` are excluded from the component library.

## Reference Repository Findings

The reference repository is arranged as an application plus Lua support modules:

- `main/` contains a TuyaOpen application entry and build glue.
- `components/modules/lua-tuyaopen/` contains Lua bindings for TuyaOpen capabilities.
- `components/modules/lua-socket/` provides socket support.
- `lua_samples/` contains Lua scripts for base language, file, GPIO, network, MQTT, system, timer, thread, UART, and utility examples.
- `tools/nodemcu-uploader/` is a script upload/debug helper and is not needed for the first component import.

The new TuyaOpen component should not copy the application layout. It should keep Lua runtime integration in `src/luanode` and expose a narrow public C API that examples and applications can call.

## Porting Decisions

- Memory allocation uses a Lua custom allocator backed by Tuya TAL memory APIs.
- File loading is exposed through `tuya_luanode_dofile()` and must validate script paths before reading.
- Standard libraries are controlled by Kconfig.
- Tuya-specific libraries are registered explicitly by `tuya_luanode_register_tuya_libs()`.
- CLI support is optional and disabled by default.
- Examples are placed under `examples/get-started/luanode`.

## First Version Scope

The first version should include:

- Lua 5.5.0 runtime import.
- Runtime create/destroy APIs.
- String script execution with `tuya_luanode_dostring()`.
- Optional file script execution with path validation.
- A small `tuya.log()` Lua library.
- A Linux get-started example.

Network, MQTT, GPIO, UART, timers, and cloud activation bindings from the reference repository should be evaluated later as separate extensions.

## Risks

- Lua 5.5.0 API changes must be checked against wrapper code before import.
- Full standard library may expose filesystem or OS APIs that are not appropriate for all products.
- Script execution is untrusted input if scripts come from network, storage, OTA, or users; callers must authenticate and validate script origin before execution.
- Reference samples include public MQTT test credentials and cloud-oriented flows; those must not be copied into TuyaOpen examples as production credentials.
