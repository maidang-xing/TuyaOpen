import * as fs from "fs";
import * as path from "path";
import * as os from "os";
import { Heartbeat, Entry, SessionInfo, ModelStat } from "./wire";
import { PermissionBridge, Decision } from "./permissions";
import { WsServer, DeviceSession } from "./ws-server";
import { DeviceFrame, encodeHeartbeat } from "./wire";
import { CONFIG } from "./config";

interface TrackedSession {
  sid: string;
  name: string;
  model: string;
  isRunning: boolean;
  tokensOut: number;
  project: string;
  localEntries: string[];
}

export class HookRouter {
  private sessions: Map<string, TrackedSession> = new Map();
  private entries: Entry[] = [];
  private permissions: PermissionBridge;
  private wsServer: WsServer;
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  private currentModel = "unknown";
  private claudeVersion = "";

  constructor(permissions: PermissionBridge, wsServer: WsServer) {
    this.permissions = permissions;
    this.wsServer = wsServer;
  }

  start(): void {
    this.currentModel = this.detectModel();
    this.claudeVersion = this.detectVersion();

    this.heartbeatTimer = setInterval(() => {
      this.broadcastHeartbeat();
    }, CONFIG.heartbeatIntervalMs);
  }

  stop(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  async route(
    eventName: string,
    payload: Record<string, unknown>
  ): Promise<Record<string, unknown>> {
    switch (eventName) {
      case "SessionStart":
        return this.onSessionStart(payload);
      case "UserPromptSubmit":
        return this.onUserPromptSubmit(payload);
      case "PreToolUse":
        return this.onPreToolUse(payload);
      case "PostToolUse":
        return this.onPostToolUse(payload);
      case "Stop":
        return this.onStop(payload);
      default:
        return {};
    }
  }

  handleDeviceFrame(session: DeviceSession, frame: DeviceFrame): void {
    switch (frame.kind) {
      case "permission": {
        const id = frame.payload.id as string;
        const decision = frame.payload.decision as Decision;
        this.permissions.resolve(id, decision);
        break;
      }
      case "hb_req":
        this.sendHeartbeatTo(session);
        break;
      case "asr":
        console.log(`[router] ASR from ${session.name}: ${frame.payload.text}`);
        break;
      case "ack":
        break;
    }
  }

  private onSessionStart(payload: Record<string, unknown>): Record<string, unknown> {
    const sid = (payload.session_id as string) || `s_${Date.now()}`;
    const cwd = (payload.cwd as string) || "";
    const project = cwd ? path.basename(cwd) : "";

    this.sessions.set(sid, {
      sid: sid.substring(0, 15),
      name: "",
      model: this.currentModel,
      isRunning: true,
      tokensOut: 0,
      project: project.substring(0, 15),
      localEntries: [],
    });

    this.broadcastHeartbeat();
    return {};
  }

  private onUserPromptSubmit(payload: Record<string, unknown>): Record<string, unknown> {
    const sid = payload.session_id as string;
    const prompt = (payload.prompt as string) || "";
    const session = this.sessions.get(sid);
    if (session && !session.name) {
      session.name = prompt.substring(0, 32);
    }
    return {};
  }

  private async onPreToolUse(payload: Record<string, unknown>): Promise<Record<string, unknown>> {
    const tool = (payload.tool as string) || "unknown";
    const input = payload.input as Record<string, unknown> | undefined;
    const hint =
      typeof input === "object" && input
        ? (input.command as string) ||
          (input.file_path as string) ||
          JSON.stringify(input).substring(0, 60)
        : "";

    const promptId = `p_${Date.now()}`;

    this.pushEntry({ t: "t", n: tool.substring(0, 24), h: hint.substring(0, 48) });
    this.broadcastHeartbeat();

    const decision = await this.permissions.waitForApproval(promptId, tool, hint);

    if (decision === "deny") {
      return { decision: "block", reason: "denied by device" };
    }
    return {};
  }

  private onPostToolUse(payload: Record<string, unknown>): Record<string, unknown> {
    const sid = payload.session_id as string;
    const tool = (payload.tool as string) || "";
    const session = this.sessions.get(sid);
    if (session) {
      session.localEntries.push(tool.substring(0, 24));
      if (session.localEntries.length > 4) {
        session.localEntries.shift();
      }
    }
    return {};
  }

  private onStop(payload: Record<string, unknown>): Record<string, unknown> {
    const sid = payload.session_id as string;
    const session = this.sessions.get(sid);
    if (session) {
      session.isRunning = false;
      const usage = payload.usage as Record<string, number> | undefined;
      if (usage) {
        session.tokensOut = usage.output_tokens || 0;
      }
    }
    this.pushEntry({ t: "d", n: "session", h: "completed" });
    this.broadcastHeartbeat();
    return {};
  }

  private pushEntry(entry: Entry): void {
    this.entries.push(entry);
    if (this.entries.length > 8) {
      this.entries.shift();
    }
  }

  private buildHeartbeat(): Heartbeat {
    const sessionList = Array.from(this.sessions.values());
    const running = sessionList.filter((s) => s.isRunning).length;
    const totalTokens = sessionList.reduce((sum, s) => sum + s.tokensOut, 0);
    const stats = this.readStatsCache();
    const prompt = this.permissions.getCurrentPrompt() || undefined;
    const now = Math.floor(Date.now() / 1000);
    const tzMin = -new Date().getTimezoneOffset();

    const sessions: SessionInfo[] = sessionList.slice(0, 12).map((s) => ({
      sid: s.sid,
      name: s.name || "(unnamed)",
      model: s.model,
      tok: s.tokensOut,
      proj: s.project,
      run: s.isRunning,
      ent: s.localEntries,
    }));

    return {
      total: sessionList.length,
      running,
      waiting: 0,
      tokens: totalTokens + (stats.totalTokens || 0),
      tokens_today: stats.todayTokens || 0,
      tokens_in: stats.totalInputTokens || 0,
      tokens_in_today: stats.todayInputTokens || 0,
      cache_read: stats.cacheRead || 0,
      cache_write: stats.cacheWrite || 0,
      ctx_used: 0,
      ctx_total: 0,
      model: this.currentModel,
      ver: this.claudeVersion,
      cost_td: stats.costToday || 0,
      cost_all: stats.costTotal || 0,
      entries: this.entries.slice(),
      sessions,
      mstats: stats.mstats || [],
      daily: stats.daily || [],
      prompt: prompt ? { id: prompt.id, tool: prompt.tool, hint: prompt.hint } : undefined,
      time: [now, tzMin],
    };
  }

  private broadcastHeartbeat(): void {
    const hb = this.buildHeartbeat();
    this.wsServer.broadcast(encodeHeartbeat(hb));
  }

  private sendHeartbeatTo(session: DeviceSession): void {
    const hb = this.buildHeartbeat();
    this.wsServer.send(session.name, encodeHeartbeat(hb));
  }

  private readStatsCache(): {
    totalTokens: number; todayTokens: number; totalInputTokens: number;
    todayInputTokens: number; cacheRead: number; cacheWrite: number;
    costToday: number; costTotal: number; mstats: ModelStat[]; daily: number[];
  } {
    const defaults = {
      totalTokens: 0, todayTokens: 0, totalInputTokens: 0,
      todayInputTokens: 0, cacheRead: 0, cacheWrite: 0,
      costToday: 0, costTotal: 0, mstats: [] as ModelStat[], daily: [] as number[],
    };
    try {
      const statsPath = path.join(os.homedir(), ".claude", "stats-cache.json");
      if (!fs.existsSync(statsPath)) return defaults;
      const raw = fs.readFileSync(statsPath, "utf-8");
      const data = JSON.parse(raw);
      const dailyModel = data.dailyModelTokens || {};
      const dates = Object.keys(dailyModel).sort().slice(-28);
      const daily = dates.map((d: string) => {
        const models = dailyModel[d] || {};
        return Object.values(models).reduce((sum: number, v: unknown) => sum + (v as number), 0);
      });
      const modelTotals: Record<string, number> = {};
      for (const d of dates) {
        const models = dailyModel[d] || {};
        for (const [m, t] of Object.entries(models)) {
          modelTotals[m] = (modelTotals[m] || 0) + (t as number);
        }
      }
      const mstats: ModelStat[] = Object.entries(modelTotals)
        .sort((a, b) => b[1] - a[1]).slice(0, 4)
        .map(([m, tok]) => ({ m: m.substring(0, 15), tok }));
      return { ...defaults, mstats, daily };
    } catch { return defaults; }
  }

  private detectModel(): string {
    if (process.env.ANTHROPIC_MODEL) return process.env.ANTHROPIC_MODEL.substring(0, 15);
    try {
      const settingsPath = path.join(os.homedir(), ".claude", "settings.json");
      if (fs.existsSync(settingsPath)) {
        const data = JSON.parse(fs.readFileSync(settingsPath, "utf-8"));
        if (data.model) return String(data.model).substring(0, 15);
      }
    } catch {}
    return "unknown";
  }

  private detectVersion(): string {
    try {
      const { execSync } = require("child_process");
      const ver = execSync("claude --version", { timeout: 3000 }).toString().trim();
      return ver.substring(0, 19);
    } catch { return ""; }
  }
}
