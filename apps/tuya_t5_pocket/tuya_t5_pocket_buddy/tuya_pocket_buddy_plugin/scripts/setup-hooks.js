#!/usr/bin/env node
/**
 * Registers Tuya Pocket Buddy hooks in ~/.claude/settings.json
 * Idempotent: running multiple times updates existing entries rather than duplicating.
 * Usage: node scripts/setup-hooks.js
 */

const fs = require('fs');
const path = require('path');
const os = require('os');

// Matches both curl form ("127.0.0.1:9878/hook") and node inline form ("port:9878")
const MARKER = ':9878';

const CURL_ASYNC =
  "curl -s -X POST http://127.0.0.1:9878/hook -H 'Content-Type: application/json' --data-binary @- --max-time 5 2>/dev/null || true";

// Inline node script: posts to daemon and exits 2 if response is {"decision":"block"}.
// Uses node -e so no file path dependency — works on all platforms.
const NODE_PRETOOL =
  'node -e "const http=require(\'http\'),c=[];process.stdin.on(\'data\',d=>c.push(d));' +
  'process.stdin.on(\'end\',()=>{const p=Buffer.concat(c).toString();' +
  'const req=http.request({hostname:\'127.0.0.1\',port:9878,path:\'/hook\',method:\'POST\',' +
  'headers:{\'Content-Type\':\'application/json\'},timeout:42000},' +
  'res=>{const r=[];res.on(\'data\',d=>r.push(d));res.on(\'end\',()=>{' +
  'try{const b=JSON.parse(Buffer.concat(r).toString());if(b.decision===\'block\')process.exit(2);}' +
  'catch(e){}process.exit(0);});});' +
  'req.on(\'error\',()=>process.exit(0));req.on(\'timeout\',()=>{req.destroy();process.exit(0);});' +
  'req.write(p);req.end();});"';

const HOOKS_CONFIG = {
  SessionStart: [{
    hooks: [{ type: 'command', command: CURL_ASYNC, timeout: 10, async: true }]
  }],
  UserPromptSubmit: [{
    hooks: [{ type: 'command', command: CURL_ASYNC, timeout: 10, async: true }]
  }],
  PreToolUse: [{
    hooks: [{ type: 'command', command: NODE_PRETOOL, timeout: 45 }]
  }],
  PostToolUse: [{
    hooks: [{ type: 'command', command: CURL_ASYNC, timeout: 10, async: true }]
  }],
  Stop: [{
    hooks: [{ type: 'command', command: CURL_ASYNC, timeout: 10, async: true }]
  }],
};

const claudeDir = path.join(os.homedir(), '.claude');
const settingsPath = path.join(claudeDir, 'settings.json');

// Ensure ~/.claude/ exists
if (!fs.existsSync(claudeDir)) {
  fs.mkdirSync(claudeDir, { recursive: true });
}

// Read existing settings
let settings = {};
if (fs.existsSync(settingsPath)) {
  try {
    settings = JSON.parse(fs.readFileSync(settingsPath, 'utf-8'));
  } catch (e) {
    console.error('ERROR: Failed to parse settings.json:', e.message);
    console.error('Fix the JSON syntax error in', settingsPath, 'and re-run.');
    process.exit(1);
  }
}

settings.hooks = settings.hooks || {};

// Merge: replace existing buddy entries, append if none found
for (const [event, newEntries] of Object.entries(HOOKS_CONFIG)) {
  const existing = settings.hooks[event] || [];
  const nonBuddy = existing.filter(e => !JSON.stringify(e).includes(MARKER));
  const action = nonBuddy.length < existing.length ? 'updated' : 'added';
  settings.hooks[event] = [...nonBuddy, ...newEntries];
  console.log(`  ${action === 'added' ? '+' : '~'} ${event}: ${action}`);
}

// Write back with backup
const backup = settingsPath + '.bak';
if (fs.existsSync(settingsPath)) {
  fs.copyFileSync(settingsPath, backup);
}

try {
  fs.writeFileSync(settingsPath, JSON.stringify(settings, null, 2) + '\n', 'utf-8');
} catch (e) {
  console.error('ERROR: Failed to write settings.json:', e.message);
  if (fs.existsSync(backup)) {
    fs.copyFileSync(backup, settingsPath);
    console.error('Restored from backup.');
  }
  process.exit(1);
}

console.log('\nHooks registered in:', settingsPath);
console.log('Backup saved to:    ', backup);
console.log('\nRestart Claude Code (or open the /hooks menu) to apply the new hooks.');
