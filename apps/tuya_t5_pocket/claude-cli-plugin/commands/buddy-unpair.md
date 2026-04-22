---
description: Send a one-shot `{"cmd":"unpair"}` to the currently paired device and forget the stored MAC.
---

Connects to the paired T5AI-Pocket, emits `{"cmd":"unpair"}`, and
forgets the persisted MAC. Note: firmware v1.0 acks `unpair`
cosmetically — the actual BLE bond is **not** erased on the device
side. Tracked in the wire-protocol gap table.

Run `/buddy-pair` again afterwards to pick a new device.

!`python3 -m tuya_pocket_buddy unpair`
