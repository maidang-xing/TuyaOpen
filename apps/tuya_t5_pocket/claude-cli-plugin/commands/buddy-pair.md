---
description: Scan for `Claude_XXXX` peripherals advertising the Nordic UART Service and persist the chosen MAC.
---

Runs a 10-second BLE scan. If exactly one `Claude_XXXX` peripheral is
found it's selected automatically; otherwise the candidates are listed
and you pick an index.

The paired MAC is stored in `state.json` under the daemon state
directory (`~/.tuya-pocket-buddy/` or
`%LOCALAPPDATA%\tuya-pocket-buddy\`) and reused on every subsequent
`/buddy-start`.

On Windows the first connect may trigger a WinRT permission prompt —
accept it. On Linux, `bleak` needs `cap_net_raw` or the daemon user in
the `bluetooth` group.

!`python3 -m tuya_pocket_buddy pair`
