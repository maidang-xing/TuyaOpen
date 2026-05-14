---
name: status
description: Show Tuya Pocket Buddy daemon and hook status
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Check daemon

```bash
curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null && echo "DAEMON_UP" || echo "DAEMON_DOWN"
```

## Step 2: Check hooks registered in Claude settings

```bash
node -e "
const fs=require('fs'),path=require('path'),os=require('os');
const p=path.join(os.homedir(),'.claude','settings.json');
if(!fs.existsSync(p)){console.log('settings.json: NOT FOUND');process.exit(0);}
let s;
try{s=JSON.parse(fs.readFileSync(p,'utf-8'));}catch(e){console.log('settings.json: PARSE ERROR');process.exit(0);}
const h=s.hooks||{};
const marker=':9878';
['SessionStart','UserPromptSubmit','PreToolUse','PostToolUse','Stop'].forEach(ev=>{
  const found=(h[ev]||[]).some(x=>JSON.stringify(x).includes(marker));
  console.log(ev+': '+(found?'OK':'MISSING'));
});
" 2>/dev/null || echo "Hook check failed"
```

## Step 3: Report

Print a status table:

| Item | Status |
|------|--------|
| Daemon | Running / Stopped |
| Hook server | http://127.0.0.1:9878 |
| WebSocket server | ws://0.0.0.0:7681/buddy |
| SessionStart hook | OK / MISSING |
| UserPromptSubmit hook | OK / MISSING |
| PreToolUse hook | OK / MISSING |
| PostToolUse hook | OK / MISSING |
| Stop hook | OK / MISSING |

- If any hook is MISSING: suggest running `/tuya-pocket-buddy:install`
- If daemon is stopped: suggest `/tuya-pocket-buddy:start`
