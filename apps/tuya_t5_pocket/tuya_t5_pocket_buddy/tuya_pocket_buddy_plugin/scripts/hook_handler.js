#!/usr/bin/env node
const http = require("http");

const DAEMON_URL = "http://127.0.0.1:9878/hook";
const TIMEOUT_MS = 42000;

async function main() {
  const chunks = [];
  for await (const chunk of process.stdin) {
    chunks.push(chunk);
  }
  const payload = Buffer.concat(chunks).toString("utf-8");

  return new Promise((resolve) => {
    const url = new URL(DAEMON_URL);
    const req = http.request(
      {
        hostname: url.hostname,
        port: url.port,
        path: url.pathname,
        method: "POST",
        headers: { "Content-Type": "application/json" },
        timeout: TIMEOUT_MS,
      },
      (res) => {
        const resChunks = [];
        res.on("data", (c) => resChunks.push(c));
        res.on("end", () => {
          try {
            const body = JSON.parse(Buffer.concat(resChunks).toString());
            if (body.decision === "block") {
              process.exit(2);
            }
          } catch {}
          process.exit(0);
        });
      }
    );

    req.on("error", () => process.exit(0));
    req.on("timeout", () => {
      req.destroy();
      process.exit(0);
    });

    req.write(payload);
    req.end();
  });
}

main().catch(() => process.exit(0));
