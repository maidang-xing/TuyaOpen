import { WebSocketServer, WebSocket } from "ws";
import { IncomingMessage } from "http";
import { CONFIG } from "./config";
import { parseDeviceFrame, DeviceFrame } from "./wire";

export interface DeviceSession {
  ws: WebSocket;
  name: string;
  connectedAt: number;
}

export type DeviceFrameHandler = (
  session: DeviceSession,
  frame: DeviceFrame
) => void;

export type DeviceConnectHandler = (session: DeviceSession) => void;
export type AllDevicesDisconnectedHandler = () => void;

export class WsServer {
  private wss: WebSocketServer | null = null;
  private devices: Map<string, DeviceSession> = new Map();
  private frameHandler: DeviceFrameHandler;
  private connectHandler: DeviceConnectHandler | undefined;
  private allDisconnectedHandler: AllDevicesDisconnectedHandler | undefined;

  constructor(
    frameHandler: DeviceFrameHandler,
    connectHandler?: DeviceConnectHandler,
    allDisconnectedHandler?: AllDevicesDisconnectedHandler,
  ) {
    this.frameHandler = frameHandler;
    this.connectHandler = connectHandler;
    this.allDisconnectedHandler = allDisconnectedHandler;
  }

  start(): void {
    this.wss = new WebSocketServer({
      host: "0.0.0.0",
      port: CONFIG.wsPort,
      path: "/buddy",
      perMessageDeflate: false, // Embedded WS clients can't handle compression frames
    });

    this.wss.on("error", (err: Error) => {
      console.warn("[ws-server] server error:", err.message);
    });

    this.wss.on("connection", (ws: WebSocket, req: IncomingMessage) => {
      const name =
        (req.headers["x-buddy-name"] as string) || `device_${Date.now()}`;

      if (this.devices.size >= CONFIG.maxDevices) {
        console.log(`[ws-server] max devices reached, rejecting ${name}`);
        ws.close(1013, "max devices");
        return;
      }

      const existing = this.devices.get(name);
      if (existing) {
        console.log(`[ws-server] replacing existing session for ${name}`);
        existing.ws.close(1000, "replaced");
        this.devices.delete(name);
      }

      const session: DeviceSession = { ws, name, connectedAt: Date.now() };
      this.devices.set(name, session);
      console.log(`[ws-server] device connected: ${name} (${this.devices.size} total)`);
      try {
        this.connectHandler?.(session);
      } catch (err) {
        console.warn(`[ws-server] connect handler error for ${name}:`, (err as Error).message);
      }

      ws.on("message", (data: Buffer | string) => {
        try {
          const raw = typeof data === "string" ? data : data.toString("utf-8");
          console.log(`[ws-server] ← ${name}: ${raw.substring(0, 120)}`);
          const frame = parseDeviceFrame(raw);
          this.frameHandler(session, frame);
        } catch (err) {
          console.warn(`[ws-server] bad frame from ${name}:`, err);
        }
      });

      ws.on("close", () => {
        this.devices.delete(name);
        console.log(`[ws-server] device disconnected: ${name} (${this.devices.size} total)`);
        if (this.devices.size === 0) {
          this.allDisconnectedHandler?.();
        }
      });

      ws.on("error", (err) => {
        console.warn(`[ws-server] error from ${name}:`, err.message);
      });
    });

    console.log(`[ws-server] listening on 0.0.0.0:${CONFIG.wsPort}/buddy`);
  }

  broadcast(json: string): void {
    if (this.devices.size > 0) {
      console.log(`[ws-server] → broadcast (${this.devices.size}): ${json.substring(0, 120)}`);
    }
    for (const [, session] of this.devices) {
      if (session.ws.readyState === WebSocket.OPEN) {
        try {
          session.ws.send(json);
        } catch (err) {
          console.warn(`[ws-server] broadcast error to ${session.name}:`, (err as Error).message);
        }
      }
    }
  }

  send(name: string, json: string): void {
    const session = this.devices.get(name);
    if (session && session.ws.readyState === WebSocket.OPEN) {
      try {
        session.ws.send(json);
      } catch (err) {
        console.warn(`[ws-server] send error to ${name}:`, (err as Error).message);
      }
    }
  }

  getConnectedCount(): number {
    return this.devices.size;
  }

  stop(): void {
    for (const [, session] of this.devices) {
      session.ws.close(1001, "server shutdown");
    }
    this.devices.clear();
    this.wss?.close();
    this.wss = null;
  }
}
