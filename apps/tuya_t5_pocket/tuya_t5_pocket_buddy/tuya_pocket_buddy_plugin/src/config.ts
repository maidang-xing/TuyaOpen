export const CONFIG = {
  hookPort: 9878,
  wsPort: 7681,
  maxDevices: 4,
  permissionTimeoutMs: 35_000,
  heartbeatIntervalMs: 10_000,
  timeSyncIntervalMs: 30_000,
  maxPayloadBytes: 65_536,
} as const;
