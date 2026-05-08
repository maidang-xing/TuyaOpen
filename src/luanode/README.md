# Luanode

`luanode` embeds Lua v5.5.0 as a TuyaOpen component. It provides a small C API for creating a Lua runtime and executing Lua scripts from applications.

## Enable

Add these options to an application `app_default.config`:

```text
CONFIG_ENABLE_LUANODE=y
CONFIG_LUANODE_ENABLE_STD_LIBS=y
CONFIG_LUANODE_ENABLE_TUYA_LIBS=y
```

Optional CLI support:

```text
CONFIG_ENABLE_SERIAL_CLI_CMD=y
CONFIG_LUANODE_ENABLE_CLI=y
```

CLI is disabled by default.

## Public API

```c
TUYA_LUANODE_HANDLE_T handle = NULL;
TUYA_LUANODE_CFG_T cfg = {
    .heap_size = LUANODE_HEAP_SIZE,
    .stack_size = LUANODE_STACK_SIZE,
    .enable_std_libs = TRUE,
};

tuya_luanode_create(&cfg, &handle);
tuya_luanode_register_tuya_libs(handle);
tuya_luanode_dostring(handle, "print(\"hello lua\")");
tuya_luanode_destroy(handle);
```

## Configuration

- `LUANODE_HEAP_SIZE`: Lua runtime heap limit in KB.
- `LUANODE_STACK_SIZE`: stack size for application or CLI execution tasks.
- `LUANODE_MAX_SCRIPT_SIZE`: maximum accepted script size in bytes.
- `LUANODE_ENABLE_STD_LIBS`: enable Lua standard libraries.
- `LUANODE_ENABLE_TUYA_LIBS`: enable Tuya helper library registration.
- `LUANODE_ENABLE_CLI`: enable `lua run` and `lua eval` CLI commands.

## Tuya Lua Library

The first version exposes a restricted `tuya` table:

```lua
tuya.log("hello tuya")
```

The log message is clamped to 256 bytes before it is sent to the Tuya log system.

## Security Notes

Lua scripts are executable code. If scripts come from network, cloud, OTA, storage, user input, or any other external boundary, the caller must authenticate the source and verify integrity before execution.

`tuya_luanode_dofile()` rejects empty paths, absolute paths, long paths, and path traversal tokens. CLI support is disabled by default and should remain disabled in production unless a product explicitly needs it.

The full Lua standard library includes filesystem and OS APIs. Disable `LUANODE_ENABLE_STD_LIBS` for restricted products and expose only audited helper functions.
