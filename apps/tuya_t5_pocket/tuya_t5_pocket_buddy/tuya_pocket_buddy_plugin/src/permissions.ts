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

  async waitForApproval(
    promptId: string,
    tool: string,
    hint: string
  ): Promise<Decision> {
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.resolve("deny");
      this.pending = null;
    }

    return new Promise<Decision>((resolve) => {
      const timer = setTimeout(() => {
        if (this.pending?.id === promptId) {
          this.pending = null;
          resolve("deny");
        }
      }, CONFIG.permissionTimeoutMs);

      this.pending = { id: promptId, tool, hint, resolve, timer };
    });
  }

  resolve(promptId: string, decision: Decision): boolean {
    if (!this.pending || this.pending.id !== promptId) {
      return false;
    }
    clearTimeout(this.pending.timer);
    this.pending.resolve(decision);
    this.pending = null;
    return true;
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
}
