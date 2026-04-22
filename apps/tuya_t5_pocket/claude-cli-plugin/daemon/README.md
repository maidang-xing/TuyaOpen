# tuya-pocket-buddy daemon

Background Python daemon that bridges a Claude Code session to a
Tuya T5AI-Pocket running the M1-A firmware.

- BLE central: scans for `Claude_XXXX`, connects to the Nordic UART
  Service, and exchanges newline-delimited JSON frames as specified in
  [`BLE_WIRE_PROTOCOL.md`](../../tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md)
  v1.0.
- Hook HTTP server: binds `127.0.0.1:9878`, accepts Claude Code hook
  payloads (SessionStart, UserPromptSubmit, PreToolUse, PostToolUse,
  Stop) and forwards them to the device.
- Permission bridge: PreToolUse blocks on a device button press;
  returns `approve` / `deny` / `approve+permanent` to Claude Code.

Run directly:

```sh
python -m tuya_pocket_buddy
```

Packaged as a plugin — see the parent `README.md` for the user-facing
`/buddy-install`, `/buddy-start`, `/buddy-stop` workflow.
