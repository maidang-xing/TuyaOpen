#!/usr/bin/env node
/**
 * Removes Tuya Pocket Buddy hooks from ~/.claude/settings.json
 * Usage: node scripts/remove-hooks.js
 */

const fs = require('fs');
const path = require('path');
const os = require('os');

// Matches both curl form ("127.0.0.1:9878/hook") and node inline form ("port:9878")
const MARKER = ':9878';
const settingsPath = path.join(os.homedir(), '.claude', 'settings.json');

if (!fs.existsSync(settingsPath)) {
  console.log('No settings.json found — nothing to remove.');
  process.exit(0);
}

let settings;
try {
  settings = JSON.parse(fs.readFileSync(settingsPath, 'utf-8'));
} catch (e) {
  console.error('ERROR: Failed to parse settings.json:', e.message);
  process.exit(1);
}

if (!settings.hooks) {
  console.log('No hooks section in settings.json — nothing to remove.');
  process.exit(0);
}

let removed = 0;
for (const event of Object.keys(settings.hooks)) {
  const before = settings.hooks[event];
  const after = before.filter(e => !JSON.stringify(e).includes(MARKER));
  const delta = before.length - after.length;
  if (delta > 0) {
    removed += delta;
    if (after.length === 0) {
      delete settings.hooks[event];
    } else {
      settings.hooks[event] = after;
    }
    console.log(`  - ${event}: removed ${delta} buddy entry`);
  }
}

if (removed === 0) {
  console.log('No Tuya Pocket Buddy hooks found in settings.json.');
  process.exit(0);
}

const backup = settingsPath + '.bak';
fs.copyFileSync(settingsPath, backup);

try {
  fs.writeFileSync(settingsPath, JSON.stringify(settings, null, 2) + '\n', 'utf-8');
} catch (e) {
  console.error('ERROR: Failed to write settings.json:', e.message);
  fs.copyFileSync(backup, settingsPath);
  console.error('Restored from backup.');
  process.exit(1);
}

console.log(`\nRemoved ${removed} hook entries from: ${settingsPath}`);
console.log('Backup saved to:                    ', backup);
console.log('\nRestart Claude Code (or open the /hooks menu) to apply.');
