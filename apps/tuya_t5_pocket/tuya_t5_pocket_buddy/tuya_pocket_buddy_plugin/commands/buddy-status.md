---
name: buddy-status
description: Show Tuya Pocket Buddy daemon status
---

Check if the daemon is running:

```bash
curl -s http://127.0.0.1:9878/hook -X POST -d '{}' -H 'Content-Type: application/json' --max-time 2 && echo " daemon is running" || echo "daemon is not running"
```
