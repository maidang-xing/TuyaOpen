export interface Entry {
  t: string;
  n: string;
  h: string;
}

export interface SessionInfo {
  sid: string;
  name: string;
  model: string;
  tok: number;
  proj: string;
  run: boolean;
  ent: string[];
}

export interface ModelStat {
  m: string;
  tok: number;
}

export interface PromptInfo {
  id: string;
  tool: string;
  hint: string;
}

export interface Heartbeat {
  total: number;
  running: number;
  waiting: number;
  tokens: number;
  tokens_today: number;
  tokens_in: number;
  tokens_in_today: number;
  cache_read: number;
  cache_write: number;
  ctx_used: number;
  ctx_total: number;
  model: string;
  ver: string;
  cost_td: number;
  cost_all: number;
  entries: Entry[];
  sessions: SessionInfo[];
  mstats: ModelStat[];
  daily: number[];
  prompt?: PromptInfo;
  time: [number, number];
}

export function encodeHeartbeat(hb: Heartbeat): string {
  return JSON.stringify(hb);
}

export function encodeTimeSync(epochS: number, tzMin: number): string {
  return JSON.stringify({ time: [epochS, tzMin] });
}

export function encodeOwner(name: string): string {
  return JSON.stringify({ cmd: "owner", name });
}

export function encodeStatusRequest(): string {
  return JSON.stringify({ cmd: "status" });
}

export type DeviceFrameKind = "permission" | "asr" | "hb_req" | "ack" | "unknown";

export interface DeviceFrame {
  kind: DeviceFrameKind;
  payload: Record<string, unknown>;
}

export function parseDeviceFrame(raw: string): DeviceFrame {
  const obj = JSON.parse(raw);

  if (obj.cmd === "permission") {
    return { kind: "permission", payload: obj };
  }
  if (obj.cmd === "asr") {
    return { kind: "asr", payload: obj };
  }
  if (obj.cmd === "hb_req") {
    return { kind: "hb_req", payload: obj };
  }
  if (obj.ack) {
    return { kind: "ack", payload: obj };
  }

  return { kind: "unknown", payload: obj };
}
