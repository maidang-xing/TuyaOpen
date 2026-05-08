# Luanode Design

## Purpose

`luanode` packages Lua v5.5.0 as a reusable TuyaOpen component under `src/luanode`. Applications can create a Lua runtime, execute script strings or files, and optionally register Tuya helper libraries.

## Directory Layout

```text
src/luanode/
├── CMakeLists.txt
├── Kconfig
├── README.md
├── include/                  Public TuyaOpen API
├── src/                      Runtime wrapper, CLI, and Lua libraries
├── port/                     TuyaOpen allocator and platform ports
├── lua/                      Official Lua v5.5.0 source snapshot
└── doc/                      Design and upstream analysis
```

## Build Integration

The repository root scans `src/` for directories with `CMakeLists.txt`. `src/luanode/CMakeLists.txt` only creates the component when `CONFIG_ENABLE_LUANODE` is enabled.

`src/Kconfig` includes `luanode/Kconfig`, which defines runtime memory, script size, standard library, CLI, and Tuya library options.

Official Lua sources are compiled into the `luanode` component. Standalone tool sources such as `lua.c`, `luac.c`, and `ltests.c` are excluded.

## Runtime Lifecycle

The public lifecycle is:

1. `tuya_luanode_create()` validates the configuration and creates a Lua state with a Tuya allocator.
2. `tuya_luanode_register_tuya_libs()` optionally registers the `tuya` Lua table.
3. `tuya_luanode_dostring()` or `tuya_luanode_dofile()` executes scripts.
4. `tuya_luanode_destroy()` closes the Lua state and releases memory.

The Lua allocator tracks heap usage and refuses growth beyond the configured limit. Lua errors are logged with a fixed format string and bounded message length.

## File Loading

`tuya_luanode_dofile()` validates paths before reading:

- Empty paths are rejected.
- Absolute paths are rejected.
- Paths containing `..` are rejected.
- Paths longer than the internal limit are rejected.

Linux examples read host files with bounded `fopen/fread` after path validation. Other platforms use Tuya filesystem APIs.

## Tuya Lua Libraries

The first helper library exposes only:

```lua
tuya.log("message")
```

The implementation accepts a single string and clamps it to 256 bytes before logging.

## CLI

When `LUANODE_ENABLE_CLI` is enabled, `tuya_luanode_cli_register()` registers:

```text
lua run <script-path>
lua eval <script-text>
```

CLI is disabled by default because it can execute arbitrary Lua code.

## Security Notes

Lua script execution must be treated as code execution. Products that load scripts from outside firmware must add source authentication, integrity checks, authorization, and rollback behavior before calling the runtime.

The standard Lua `os` and `io` libraries can expose filesystem and process-related capabilities depending on platform support. Restricted products should disable standard libraries and expose audited Tuya-specific functions instead.

Logs must not include credentials, tokens, keys, or full authentication material. The current helper library limits log size but does not classify secret content; callers and scripts must not pass secrets to logs.

## Known Limitations

- The first version exposes only `tuya.log()`.
- There is no sandbox or instruction budget.
- There is no script signature verification in the component.
- The CLI command creates a fresh runtime per command.
- Multi-runtime behavior is supported by independent handles but has not been stress-tested on memory-constrained targets.
