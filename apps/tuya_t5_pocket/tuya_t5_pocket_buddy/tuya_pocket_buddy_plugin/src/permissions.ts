import { CONFIG } from "./config";

export type Decision = "once" | "always" | "deny";

interface PendingPrompt {
  id: string;
  tool: string;
  hint: string;
  resolve: (decision: Decision) => void;
  timer: ReturnType<typeof setTimeout>;
}

export class PermissionBridge {
  private pending: PendingPrompt | null = null;
  private queue: PendingPrompt[] = [];
  private broadcastFn?: () => void;

  // Called by HookRouter so the bridge can trigger heartbeat when a queued
  // item becomes active (pending changes asynchronously via _processNext).
  setBroadcastCallback(fn: () => void): void {
    this.broadcastFn = fn;
  }

  async waitForApproval(
    promptId: string,
    tool: string,
    hint: string
  ): Promise<Decision> {
    return new Promise<Decision>((resolve) => {
      const timer = setTimeout(() => {
        this._resolveById(promptId, "once");
      }, CONFIG.permissionTimeoutMs);

      const item: PendingPrompt = { id: promptId, tool, hint, resolve, timer };

      if (!this.pending) {
        // No active approval — start immediately and trigger heartbeat.
        this.pending = item;
        this.broadcastFn?.();
      } else {
        // Another approval is in flight — queue this one.
        this.queue.push(item);
      }
    });
  }

  private _resolveById(promptId: string, decision: Decision): void {
    if (this.pending?.id === promptId) {
      clearTimeout(this.pending.timer);
      this.pending.resolve(decision);
      this.pending = null;
      this._processNext();
      return;
    }
    // May still be in the queue (e.g. timeout fired before it became active).
    const idx = this.queue.findIndex((q) => q.id === promptId);
    if (idx >= 0) {
      const item = this.queue.splice(idx, 1)[0];
      clearTimeout(item.timer);
      item.resolve(decision);
    }
  }

  private _processNext(): void {
    if (this.queue.length === 0) return;
    this.pending = this.queue.shift()!;
    // Trigger heartbeat so device receives the next prompt immediately.
    this.broadcastFn?.();
  }

  resolve(promptId: string, decision: Decision): boolean {
    if (!this.pending || this.pending.id !== promptId) return false;
    this._resolveById(promptId, decision);
    return true;
  }

  resolveAllOnDisconnect(): void {
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.resolve("once");
      this.pending = null;
    }
    for (const item of this.queue) {
      clearTimeout(item.timer);
      item.resolve("once");
    }
    this.queue = [];
  }

  getCurrentPrompt(): { id: string; tool: string; hint: string } | null {
    if (!this.pending) return null;
    return {
      id: this.pending.id,
      tool: this.pending.tool,
      hint: this.pending.hint,
    };
  }

  hasPending(): boolean {
    return this.pending !== null;
  }

  getQueueLength(): number {
    return this.queue.length;
  }
}
