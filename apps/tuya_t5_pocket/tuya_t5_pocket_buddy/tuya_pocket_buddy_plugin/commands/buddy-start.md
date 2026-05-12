---
name: buddy-start
description: Start the Tuya Pocket Buddy daemon
---

Start the daemon process:

```bash
cd tuya_pocket_buddy_plugin && node dist/index.js run &
```

The daemon listens on:
- HTTP :9878 (hook server, loopback only)
- WebSocket :7681 (device connections)
