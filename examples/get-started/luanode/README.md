# Luanode Get Started

This example runs Lua v5.5.0 through the `src/luanode` component on the Linux Ubuntu target.

## Build

From this directory:

```bash
tos.py build
```

## Run

```bash
./dist/luanode_1.0.0/luanode_1.0.0.elf
```

Expected key output:

```text
hello from dostring
hello lua
lua: hello tuya
```

The file script is located at `src/scripts/hello.lua`. Run the ELF from this example directory so the relative script path can be resolved.
