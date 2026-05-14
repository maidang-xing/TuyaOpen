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

interface StatsSnapshot {
  totalTokens: number;
  todayTokens: number;
  totalInputTokens: number;
  todayInputTokens: number;
  cacheRead: number;
  cacheWrite: number;
  costToday: number;
  costTotal: number;
  mstats: ModelStat[];
  daily: number[];
  ctxUsed: number;
  ctxTotal: number;
  fileSessions: SessionInfo[];
}

const MODEL_CTX_SIZES: Record<string, number> = {
  "opus": 200000,
  "sonnet": 200000,
  "haiku": 200000,
};

function getCtxTotal(model: string): number {
  if (model.includes("1m")) return 1000000;
  for (const [key, size] of Object.entries(MODEL_CTX_SIZES)) {
    if (model.includes(key)) return size;
  }
  return 200000;
}

export class HookRouter {
  private sessions: Map<string, TrackedSession> = new Map();
  private entries: Entry[] = [];
  private permissions: PermissionBridge;
  private wsServer: WsServer;
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  private currentModel = "unknown";
  private claudeVersion = "";
  private tickCount = 0;
  private cachedStats: StatsSnapshot | null = null;

  constructor(permissions: PermissionBridge, wsServer: WsServer) {
    this.permissions = permissions;
    this.wsServer = wsServer;
    permissions.setBroadcastCallback(() => this.broadcastHeartbeat());
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

  onDeviceConnect(session: DeviceSession): void {
    this.sendHeartbeatTo(session);
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
    const tool = (payload.tool_name as string) || (payload.tool as string) || "unknown";
    const input = (payload.tool_input ?? payload.input) as Record<string, unknown> | undefined;
    const hint =
      typeof input === "object" && input
        ? (input.command as string) ||
          (input.file_path as string) ||
          JSON.stringify(input).substring(0, 60)
        : "";

    this.pushEntry({ t: "t", n: tool.substring(0, 24), h: hint.substring(0, 48) });

    if (this.wsServer.getConnectedCount() === 0) {
      this.broadcastHeartbeat();
      return {};
    }

    const promptId = `p_${Date.now()}`;
    // Start the approval Promise first (sets pending synchronously), then broadcast
    // so the heartbeat carries the prompt field and the device sees it immediately.
    const decisionPromise = this.permissions.waitForApproval(promptId, tool, hint);
    this.broadcastHeartbeat();

    const decision = await decisionPromise;

    if (decision === "deny") {
      return { decision: "block", reason: "denied by device" };
    }
    return {};
  }

  private onPostToolUse(payload: Record<string, unknown>): Record<string, unknown> {
    const sid = payload.session_id as string;
    const tool = (payload.tool_name as string) || (payload.tool as string) || "";
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
    const stats = this.cachedStats || this.readStatsDeep();
    const prompt = this.permissions.getCurrentPrompt() || undefined;
    const now = Math.floor(Date.now() / 1000);
    const tzMin = -new Date().getTimezoneOffset();

    const liveSessions: SessionInfo[] = sessionList.slice(0, 12).map((s) => ({
      sid: s.sid,
      name: s.name || "(unnamed)",
      model: s.model,
      tok: s.tokensOut,
      proj: s.project,
      run: s.isRunning,
      ent: s.localEntries,
    }));

    const liveSids = new Set(liveSessions.map((s) => s.sid));
    const fileSessions = (stats.fileSessions || []).filter((s) => !liveSids.has(s.sid));
    const allSessions = [...liveSessions, ...fileSessions].slice(0, 12);
    const totalSessions = liveSessions.length + (stats.fileSessions || []).length;

    return {
      total: totalSessions,
      running: running + allSessions.filter((s) => s.run && !liveSids.has(s.sid)).length,
      waiting: 0,
      tokens: totalTokens + (stats.totalTokens || 0),
      tokens_today: stats.todayTokens || 0,
      tokens_in: stats.totalInputTokens || 0,
      tokens_in_today: stats.todayInputTokens || 0,
      cache_read: stats.cacheRead || 0,
      cache_write: stats.cacheWrite || 0,
      ctx_used: stats.ctxUsed || 0,
      ctx_total: stats.ctxTotal || 0,
      model: this.currentModel,
      ver: this.claudeVersion,
      cost_td: stats.costToday || 0,
      cost_all: stats.costTotal || 0,
      entries: this.entries.slice(),
      sessions: allSessions,
      mstats: stats.mstats || [],
      daily: stats.daily || [],
      prompt: prompt ? { id: prompt.id, tool: prompt.tool, hint: prompt.hint } : undefined,
      time: [now, tzMin],
    };
  }

  private broadcastHeartbeat(): void {
    this.tickCount++;
    if (this.tickCount % 6 === 1 || !this.cachedStats) {
      this.cachedStats = this.readStatsDeep();
    }
    const hb = this.buildHeartbeat();
    const json = encodeHeartbeat(hb);
    if (this.wsServer.getConnectedCount() > 0) {
      console.log(
        `[router] hb: total=${hb.total} run=${hb.running} tok=${hb.tokens} ctx=${hb.ctx_used}/${hb.ctx_total} sess=${hb.sessions.length} prompt=${!!hb.prompt}`
      );
    }
    this.wsServer.broadcast(json);
  }

  private sendHeartbeatTo(session: DeviceSession): void {
    try {
      const hb = this.buildHeartbeat();
      this.wsServer.send(session.name, encodeHeartbeat(hb));
    } catch (err) {
      console.warn(`[router] heartbeat error for ${session.name}:`, (err as Error).message);
    }
  }

  private readStatsDeep(): StatsSnapshot {
    const defaults: StatsSnapshot = {
      totalTokens: 0, todayTokens: 0, totalInputTokens: 0,
      todayInputTokens: 0, cacheRead: 0, cacheWrite: 0,
      costToday: 0, costTotal: 0, mstats: [], daily: [],
      ctxUsed: 0, ctxTotal: getCtxTotal(this.currentModel),
      fileSessions: [],
    };
    try {
      const statsPath = path.join(os.homedir(), ".claude", "stats-cache.json");
      if (!fs.existsSync(statsPath)) return defaults;
      const raw = fs.readFileSync(statsPath, "utf-8");
      const data = JSON.parse(raw);

      const mu = data.modelUsage || {};
      let totalOut = 0, totalIn = 0, cacheRead = 0, cacheWrite = 0, costTotal = 0;
      for (const v of Object.values(mu) as Record<string, number>[]) {
        totalOut += v.outputTokens || 0;
        totalIn += v.inputTokens || 0;
        cacheRead += v.cacheReadInputTokens || 0;
        cacheWrite += v.cacheCreationInputTokens || 0;
        costTotal += v.costUSD || 0;
      }

      const dmt: { date: string; tokensByModel: Record<string, number> }[] = data.dailyModelTokens || [];
      const today = new Date().toISOString().slice(0, 10);
      const sorted = dmt.slice().sort((a, b) => a.date.localeCompare(b.date));
      const recent = sorted.slice(-28);
      const daily = recent.map((d) => {
        const models = d.tokensByModel || {};
        return Object.values(models).reduce((sum, v) => sum + (v as number), 0);
      });

      let todayTokens = 0;
      const todayEntry = sorted.find((d) => d.date === today);
      if (todayEntry) {
        todayTokens = Object.values(todayEntry.tokensByModel || {}).reduce(
          (sum, v) => sum + (v as number), 0
        );
      }

      const totalAllDays = daily.reduce((s, v) => s + v, 0);
      const costToday = totalAllDays > 0
        ? Math.round(costTotal * 1e6 * (todayTokens / totalAllDays))
        : 0;

      const modelTotals: Record<string, number> = {};
      for (const d of recent) {
        for (const [m, t] of Object.entries(d.tokensByModel || {})) {
          modelTotals[m] = (modelTotals[m] || 0) + (t as number);
        }
      }
      const mstats: ModelStat[] = Object.entries(modelTotals)
        .sort((a, b) => b[1] - a[1]).slice(0, 4)
        .map(([m, tok]) => ({ m: m.substring(0, 15), tok }));

      const todayIn = this.estimateTodayInput(mu, todayTokens, totalOut);

      const { ctxUsed, fileSessions } = this.readActiveSessions();

      return {
        totalTokens: totalOut,
        todayTokens,
        totalInputTokens: totalIn,
        todayInputTokens: todayIn,
        cacheRead,
        cacheWrite,
        costToday,
        costTotal: Math.round(costTotal * 1e6),
        mstats,
        daily,
        ctxUsed,
        ctxTotal: getCtxTotal(this.currentModel),
        fileSessions,
      };
    } catch (err) {
      console.warn("[router] readStatsDeep error:", (err as Error).message);
      return defaults;
    }
  }

  private estimateTodayInput(
    mu: Record<string, Record<string, number>>,
    todayOut: number,
    totalOut: number
  ): number {
    if (totalOut === 0) return 0;
    let totalIn = 0;
    for (const v of Object.values(mu)) {
      totalIn += (v.inputTokens || 0) + (v.cacheReadInputTokens || 0) + (v.cacheCreationInputTokens || 0);
    }
    return Math.round(totalIn * (todayOut / totalOut));
  }

  private readActiveSessions(): { ctxUsed: number; fileSessions: SessionInfo[] } {
    const empty = { ctxUsed: 0, fileSessions: [] as SessionInfo[] };
    try {
      const projectsDir = path.join(os.homedir(), ".claude", "projects");
      if (!fs.existsSync(projectsDir)) return empty;

      interface FileEntry {
        filePath: string;
        project: string;
        mtimeMs: number;
        sessionId: string;
      }

      const allFiles: FileEntry[] = [];
      const projDirs = fs.readdirSync(projectsDir);
      for (const proj of projDirs) {
        const projPath = path.join(projectsDir, proj);
        try { if (!fs.statSync(projPath).isDirectory()) continue; } catch { continue; }
        const projectName = this.decodeProjectDir(proj);
        const files = fs.readdirSync(projPath).filter((f) => f.endsWith(".jsonl"));
        for (const f of files) {
          const fp = path.join(projPath, f);
          try {
            const stat = fs.statSync(fp);
            allFiles.push({
              filePath: fp,
              project: projectName.substring(0, 15),
              mtimeMs: stat.mtimeMs,
              sessionId: f.replace(".jsonl", "").substring(0, 15),
            });
          } catch { continue; }
        }
      }

      allFiles.sort((a, b) => b.mtimeMs - a.mtimeMs);
      const recent = allFiles.slice(0, 12);

      let ctxUsed = 0;
      const fileSessions: SessionInfo[] = [];

      for (let idx = 0; idx < recent.length; idx++) {
        const entry = recent[idx];
        try {
          const info = this.parseSessionFile(entry.filePath, idx === 0);
          if (idx === 0) ctxUsed = info.ctxUsed;
          fileSessions.push({
            sid: entry.sessionId,
            name: info.name || "(unnamed)",
            model: info.model,
            tok: info.tokensOut,
            proj: entry.project,
            run: info.isRunning,
            ent: info.recentTools,
          });
        } catch { continue; }
      }

      return { ctxUsed, fileSessions };
    } catch (err) {
      console.warn("[router] readActiveSessions error:", (err as Error).message);
      return empty;
    }
  }

  private decodeProjectDir(dirName: string): string {
    const parts = dirName.split("-").filter(Boolean);
    return parts[parts.length - 1] || dirName;
  }

  private parseSessionFile(filePath: string, readCtx: boolean): {
    name: string; model: string; tokensOut: number;
    isRunning: boolean; recentTools: string[]; ctxUsed: number;
  } {
    const result = {
      name: "", model: "", tokensOut: 0,
      isRunning: false, recentTools: [] as string[], ctxUsed: 0,
    };

    const stat = fs.statSync(filePath);
    const nowMs = Date.now();
    result.isRunning = (nowMs - stat.mtimeMs) < 120_000;

    const headSize = Math.min(stat.size, 65536);
    const headFd = fs.openSync(filePath, "r");
    const headBuf = Buffer.alloc(headSize);
    fs.readSync(headFd, headBuf, 0, headSize, 0);
    fs.closeSync(headFd);

    const headLines = headBuf.toString("utf-8").split("\n");
    for (const line of headLines) {
      if (!line.trim()) continue;
      try {
        const obj = JSON.parse(line);
        if (obj.type === "user" && !result.name) {
          const msg = obj.message;
          if (typeof msg === "object" && msg) {
            const content = msg.content;
            let raw = "";
            if (Array.isArray(content)) {
              for (const c of content) {
                if (c?.type === "text" && c.text) {
                  raw = String(c.text);
                  break;
                }
              }
            } else if (typeof content === "string") {
              raw = content;
            }
            if (raw) {
              result.name = raw.replace(/<[^>]+>/g, "").trim().substring(0, 32);
            }
          }
        }
        if (result.name) break;
      } catch { continue; }
    }

    const tailSize = Math.min(stat.size, 32768);
    const tailFd = fs.openSync(filePath, "r");
    const tailBuf = Buffer.alloc(tailSize);
    fs.readSync(tailFd, tailBuf, 0, tailSize, Math.max(0, stat.size - tailSize));
    fs.closeSync(tailFd);

    const tailLines = tailBuf.toString("utf-8").split("\n").filter((l) => l.trim());
    const tools: string[] = [];

    for (let i = tailLines.length - 1; i >= 0; i--) {
      try {
        const obj = JSON.parse(tailLines[i]);
        if (obj.type === "assistant") {
          if (!result.model && obj.message?.model) {
            result.model = String(obj.message.model).substring(0, 15);
          }
          if (readCtx && result.ctxUsed === 0 && obj.message?.usage) {
            const u = obj.message.usage;
            result.ctxUsed = (u.input_tokens || 0) +
              (u.cache_read_input_tokens || 0) +
              (u.cache_creation_input_tokens || 0) +
              (u.output_tokens || 0);
          }
          if (obj.message?.content) {
            const content = obj.message.content;
            if (Array.isArray(content)) {
              for (const c of content) {
                if (c?.type === "tool_use" && c.name && tools.length < 4) {
                  tools.push(String(c.name).substring(0, 24));
                }
              }
            }
          }
          result.tokensOut += obj.message?.usage?.output_tokens || 0;
        }
        if (result.model && tools.length >= 4 && (!readCtx || result.ctxUsed > 0)) break;
      } catch { continue; }
    }

    result.recentTools = tools.slice(0, 4);
    return result;
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
