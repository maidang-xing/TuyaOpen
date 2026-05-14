import * as http from "http";
import { CONFIG } from "./config";

export type HookHandler = (
  eventName: string,
  payload: Record<string, unknown>
) => Promise<Record<string, unknown>>;

export class HookServer {
  private server: http.Server | null = null;
  private handler: HookHandler;

  constructor(handler: HookHandler) {
    this.handler = handler;
  }

  start(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.server = http.createServer(async (req, res) => {
        if (req.method !== "POST" || req.url !== "/hook") {
          res.writeHead(404);
          res.end();
          return;
        }

        const chunks: Buffer[] = [];
        let totalLen = 0;

        req.on("data", (chunk: Buffer) => {
          totalLen += chunk.length;
          if (totalLen > CONFIG.maxPayloadBytes) {
            res.writeHead(413);
            res.end();
            req.destroy();
            return;
          }
          chunks.push(chunk);
        });

        req.on("end", async () => {
          try {
            const body = Buffer.concat(chunks).toString("utf-8");
            const payload = JSON.parse(body || "{}");
            const eventName = payload.hook_event_name || payload.event || payload.type || "unknown";
            const result = await this.handler(eventName, payload);
            res.writeHead(200, { "Content-Type": "application/json" });
            res.end(JSON.stringify(result));
          } catch (err) {
            res.writeHead(500);
            res.end(JSON.stringify({ error: String(err) }));
          }
        });
      });

      this.server.listen(CONFIG.hookPort, "127.0.0.1", () => {
        console.log(`[hook-server] listening on 127.0.0.1:${CONFIG.hookPort}`);
        resolve();
      });

      this.server.on("error", reject);
    });
  }

  stop(): void {
    this.server?.close();
    this.server = null;
  }
}
