import { HookServer } from "./hook-server";
import { HookRouter } from "./hook-router";
import { WsServer } from "./ws-server";
import { PermissionBridge } from "./permissions";

async function main(): Promise<void> {
  const command = process.argv[2] || "run";

  if (command === "run") {
    await runDaemon();
  } else if (command === "status") {
    console.log("Daemon status check not yet implemented");
  } else {
    console.log(`Unknown command: ${command}`);
    console.log("Usage: buddy-daemon [run|status]");
    process.exit(1);
  }
}

async function runDaemon(): Promise<void> {
  console.log("[daemon] starting...");

  const permissions = new PermissionBridge();

  const wsServer = new WsServer(
    (session, frame) => { router.handleDeviceFrame(session, frame); },
    (session) => { router.onDeviceConnect(session); },
    () => { permissions.resolveAllOnDisconnect(); }
  );

  const router = new HookRouter(permissions, wsServer);

  const hookServer = new HookServer(async (eventName, payload) => {
    return router.route(eventName, payload);
  });

  wsServer.start();
  await hookServer.start();
  router.start();

  console.log("[daemon] all services started");

  const shutdown = () => {
    console.log("[daemon] shutting down...");
    router.stop();
    hookServer.stop();
    wsServer.stop();
    process.exit(0);
  };

  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}

main().catch((err) => {
  console.error("[daemon] fatal error:", err);
  process.exit(1);
});

process.on("uncaughtException", (err) => {
  console.error("[daemon] uncaught exception:", err.message, err.stack);
});

process.on("unhandledRejection", (reason) => {
  console.error("[daemon] unhandled rejection:", reason);
});
