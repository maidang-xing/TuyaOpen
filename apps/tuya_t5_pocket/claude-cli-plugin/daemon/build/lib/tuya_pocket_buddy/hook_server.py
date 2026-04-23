"""Local HTTP hook server.

Receives Claude Code hook POSTs on ``127.0.0.1:9878/hook`` and forwards
the payload into the event router. Strictly loopback-only: any request
whose Host header isn't ``127.0.0.1:9878`` or ``localhost:9878`` is
rejected with HTTP 403.
"""

from __future__ import annotations

import json
import logging
from typing import Any

from aiohttp import web

from .hook_router import Router

log = logging.getLogger(__name__)

BIND_HOST: str = "127.0.0.1"
DEFAULT_PORT: int = 9878
HOOK_PATH: str = "/hook"

MAX_HOOK_BODY_BYTES: int = 64 * 1024

_ALLOWED_HOSTS: frozenset[str] = frozenset({
    f"{BIND_HOST}:{DEFAULT_PORT}",
    f"localhost:{DEFAULT_PORT}",
})

ROUTER_KEY: web.AppKey[Router] = web.AppKey("router", Router)


def _host_allowed(host_header: str | None) -> bool:
    """Return True if *host_header* names a loopback address + port."""
    if not host_header:
        return False
    return host_header in _ALLOWED_HOSTS


async def _handle_hook(request: web.Request) -> web.Response:
    """Handle ``POST /hook`` from Claude Code."""
    if not _host_allowed(request.headers.get("Host")):
        log.warning("hook: rejected non-loopback Host=%r",
                    request.headers.get("Host"))
        return web.Response(status=403, text="forbidden")

    # aiohttp's content_length can be ``None`` for chunked encoding; still
    # enforce the cap by bounded read.
    raw = await request.content.read(MAX_HOOK_BODY_BYTES + 1)
    if len(raw) > MAX_HOOK_BODY_BYTES:
        log.warning("hook: oversized payload (%d bytes)", len(raw))
        return web.Response(status=413, text="payload too large")

    try:
        payload: Any = json.loads(raw)
    except json.JSONDecodeError:
        return web.Response(status=400, text="invalid json")

    if not isinstance(payload, dict):
        return web.Response(status=400, text="payload must be a JSON object")

    event = payload.get("hook_event_name")
    if not isinstance(event, str):
        return web.Response(status=400, text="missing hook_event_name")

    router: Router = request.app[ROUTER_KEY]
    try:
        reply = await router.route(event, payload)
    except Exception:
        log.exception("hook: router raised for event=%s", event)
        return web.json_response({}, status=200)

    return web.json_response(reply or {}, status=200)


def build_app(router: Router) -> web.Application:
    """Build an :class:`aiohttp.web.Application` wired to *router*."""
    app = web.Application(client_max_size=MAX_HOOK_BODY_BYTES + 4096)
    app[ROUTER_KEY] = router
    app.router.add_post(HOOK_PATH, _handle_hook)
    return app


async def run_server(
    router: Router,
    port: int = DEFAULT_PORT,
) -> web.AppRunner:
    """Start the hook server on ``127.0.0.1:<port>`` and return its runner.

    Caller is responsible for keeping the event loop alive and ultimately
    calling :meth:`AppRunner.cleanup` on shutdown.
    """
    app = build_app(router)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, host=BIND_HOST, port=port)
    await site.start()
    log.info("hook server listening on http://%s:%d%s", BIND_HOST, port,
             HOOK_PATH)
    return runner
