/**
 * @file buddy_ws.c
 * @brief WebSocket client transport for Claude Buddy.
 *
 * Implements RFC 6455 WebSocket client connecting to PC-side plugin.
 * Handles handshake, frame encode/decode, auto-reconnect, and CLI config.
 *
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#include "buddy_ws.h"
#include "buddy_protocol.h"
#include "buddy_types.h"

#include "cJSON.h"
#include "tal_api.h"
#include "tal_network.h"
#include "tal_cli.h"
#include "tal_kv.h"
#include "mix_method.h"

#include <string.h>
#include <stdlib.h>

#define TAG "buddy_ws"

#define WS_RX_BUF_SIZE    8192
#define WS_TX_BUF_SIZE    4096
#define WS_RECONNECT_MIN  5000
#define WS_RECONNECT_MAX  60000
#define WS_STACK_SIZE      (8 * 1024)

#define KV_KEY_HOST "buddy_ws_host"
#define KV_KEY_PORT "buddy_ws_port"

static THREAD_HANDLE   s_ws_thread   = NULL;
static volatile BOOL_T s_ws_running  = FALSE;
static volatile BOOL_T s_ws_connected = FALSE;
static int             s_ws_fd       = -1;
static MUTEX_HANDLE    s_ws_tx_mutex = NULL;

static char     s_host[BUDDY_WS_HOST_LEN + 1] = {0};
static uint16_t s_port = BUDDY_WS_DEFAULT_PORT;

static uint8_t  s_rx_buf[WS_RX_BUF_SIZE];
static size_t   s_rx_len = 0;

/* ---- WS frame helpers ---- */

static OPERATE_RET __send_all(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = tal_net_send(fd, buf + sent, (uint32_t)(len - sent));
        if (n == OPRT_RESOURCE_NOT_READY) {
            tal_system_sleep(5);
            continue;
        }
        if (n <= 0) return OPRT_SEND_ERR;
        sent += (size_t)n;
    }
    return OPRT_OK;
}

static OPERATE_RET __send_ws_frame(int fd, uint8_t opcode,
                                    const uint8_t *payload, size_t payload_len)
{
    uint8_t header[14] = {0};
    size_t  header_len = 0;

    header[0] = (uint8_t)(0x80 | (opcode & 0x0F));

    /* Client frames MUST be masked (RFC 6455 Section 5.1) */
    uint8_t mask[4];
    uint32_t mask_val = (uint32_t)rand();
    memcpy(mask, &mask_val, 4);

    if (payload_len <= 125) {
        header[1]  = (uint8_t)(0x80 | payload_len);
        header_len = 2;
    } else if (payload_len <= 0xFFFF) {
        header[1]  = 0x80 | 126;
        header[2]  = (uint8_t)((payload_len >> 8) & 0xFF);
        header[3]  = (uint8_t)(payload_len & 0xFF);
        header_len = 4;
    } else {
        header[1] = 0x80 | 127;
        uint64_t plen64 = (uint64_t)payload_len;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (uint8_t)((plen64 >> (56 - i * 8)) & 0xFF);
        }
        header_len = 10;
    }

    memcpy(header + header_len, mask, 4);
    header_len += 4;

    OPERATE_RET rt = __send_all(fd, header, header_len);
    if (rt != OPRT_OK) return rt;

    if (payload_len > 0) {
        uint8_t *masked = tal_malloc(payload_len);
        if (!masked) return OPRT_MALLOC_FAILED;
        for (size_t i = 0; i < payload_len; i++) {
            masked[i] = payload[i] ^ mask[i % 4];
        }
        rt = __send_all(fd, masked, payload_len);
        tal_free(masked);
    }
    return rt;
}

static OPERATE_RET __decode_ws_frame(uint8_t **out_payload, size_t *out_len,
                                      uint8_t *out_opcode, size_t *consumed)
{
    if (s_rx_len < 2) return OPRT_RESOURCE_NOT_READY;

    uint8_t op   = s_rx_buf[0] & 0x0F;
    bool masked  = (s_rx_buf[1] & 0x80) != 0;
    uint64_t plen = s_rx_buf[1] & 0x7F;
    size_t off    = 2;

    if (plen == 126) {
        if (s_rx_len < off + 2) return OPRT_RESOURCE_NOT_READY;
        plen = (uint64_t)((s_rx_buf[off] << 8) | s_rx_buf[off + 1]);
        off += 2;
    } else if (plen == 127) {
        if (s_rx_len < off + 8) return OPRT_RESOURCE_NOT_READY;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | s_rx_buf[off + i];
        off += 8;
    }

    if (plen > WS_RX_BUF_SIZE - 16) return OPRT_MSG_OUT_OF_LIMIT;

    size_t mask_len = masked ? 4 : 0;
    size_t frame_len = off + mask_len + (size_t)plen;
    if (s_rx_len < frame_len) return OPRT_RESOURCE_NOT_READY;

    uint8_t mask_key[4] = {0};
    if (masked) {
        memcpy(mask_key, s_rx_buf + off, 4);
        off += 4;
    }

    uint8_t *data = tal_malloc((size_t)plen + 1);
    if (!data) return OPRT_MALLOC_FAILED;

    if (plen > 0) {
        memcpy(data, s_rx_buf + off, (size_t)plen);
        if (masked) {
            for (size_t i = 0; i < (size_t)plen; i++)
                data[i] ^= mask_key[i % 4];
        }
    }
    data[plen] = '\0';

    *out_payload = data;
    *out_len     = (size_t)plen;
    *out_opcode  = op;
    *consumed    = frame_len;
    return OPRT_OK;
}

static void __consume_rx(size_t n)
{
    if (n >= s_rx_len) {
        s_rx_len = 0;
    } else {
        memmove(s_rx_buf, s_rx_buf + n, s_rx_len - n);
        s_rx_len -= n;
    }
}

/* ---- WS handshake (client side) ---- */

static OPERATE_RET __ws_handshake(int fd)
{
    uint8_t raw_key[16];
    for (int i = 0; i < 16; i++) raw_key[i] = (uint8_t)(rand() & 0xFF);

    char ws_key[32] = {0};
    tuya_base64_encode(raw_key, ws_key, 16);

    char request[512];
    int n = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "X-Buddy-Name: Claude_Buddy\r\n"
        "X-Buddy-Version: 1.0\r\n"
        "\r\n",
        BUDDY_WS_PATH, s_host, (unsigned)s_port, ws_key);

    OPERATE_RET rt = __send_all(fd, (const uint8_t *)request, (size_t)n);
    if (rt != OPRT_OK) return rt;

    uint8_t resp[1024];
    size_t  resp_len = 0;
    int     timeout_ms = 5000;
    int     elapsed = 0;

    while (elapsed < timeout_ms) {
        int r = tal_net_recv(fd, resp + resp_len, (uint32_t)(sizeof(resp) - resp_len));
        if (r > 0) {
            resp_len += (size_t)r;
            resp[resp_len] = '\0';
            if (strstr((char *)resp, "\r\n\r\n")) break;
        } else if (r == OPRT_RESOURCE_NOT_READY) {
            tal_system_sleep(50);
            elapsed += 50;
        } else {
            return OPRT_RECV_ERR;
        }
    }

    if (resp_len == 0) {
        PR_WARN("WS handshake timeout: no response in %d ms", timeout_ms);
        return OPRT_COM_ERROR;
    }

    if (!strstr((char *)resp, "101")) {
        PR_WARN("WS handshake rejected (%d bytes): %.*s", (int)resp_len,
                (int)(resp_len > 120 ? 120 : resp_len), resp);
        return OPRT_COM_ERROR;
    }

    /* Preserve any WS frame data that arrived after the HTTP headers.
     * The server may send the first frame in the same TCP segment as
     * the 101 response — without this, those bytes are lost and
     * subsequent frame parsing is misaligned. */
    char *hdr_end = strstr((char *)resp, "\r\n\r\n");
    if (hdr_end) {
        size_t hdr_len = (size_t)(hdr_end - (char *)resp) + 4;
        size_t extra = resp_len - hdr_len;
        if (extra > 0 && extra <= sizeof(s_rx_buf)) {
            memcpy(s_rx_buf, resp + hdr_len, extra);
            s_rx_len = extra;
            PR_INFO("WS handshake: carried %d trailing bytes", (int)extra);
        }
    }

    PR_INFO("WS handshake success");
    return OPRT_OK;
}

/* ---- Send JSON text frame ---- */

static OPERATE_RET __send_json(const char *json_str)
{
    if (!s_ws_connected || s_ws_fd < 0 || !json_str) return OPRT_COM_ERROR;

    PR_DEBUG("WS send: %.120s", json_str);

    tal_mutex_lock(s_ws_tx_mutex);
    OPERATE_RET rt = __send_ws_frame(s_ws_fd, 0x1,
                                      (const uint8_t *)json_str, strlen(json_str));
    tal_mutex_unlock(s_ws_tx_mutex);

    if (rt != OPRT_OK) {
        PR_WARN("WS send failed rt=%d", rt);
    }
    return rt;
}

/* ---- Public send APIs ---- */

OPERATE_RET buddy_ws_send_permission(const char *id, const char *decision)
{
    if (!id || !decision) return OPRT_INVALID_PARM;
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "cmd", "permission");
    cJSON_AddStringToObject(obj, "id", id);
    cJSON_AddStringToObject(obj, "decision", decision);
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    OPERATE_RET rt = __send_json(str);
    cJSON_free(str);
    return rt;
}

OPERATE_RET buddy_ws_send_asr(const char *text, const char *sid)
{
    if (!text) return OPRT_INVALID_PARM;
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "cmd", "asr");
    cJSON_AddStringToObject(obj, "text", text);
    if (sid) cJSON_AddStringToObject(obj, "sid", sid);
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    OPERATE_RET rt = __send_json(str);
    cJSON_free(str);
    return rt;
}

OPERATE_RET buddy_ws_send_hb_req(const char *page)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "cmd", "hb_req");
    if (page) cJSON_AddStringToObject(obj, "page", page);
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    OPERATE_RET rt = __send_json(str);
    cJSON_free(str);
    return rt;
}

OPERATE_RET buddy_ws_send_ack(const char *cmd)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "ack", cmd ? cmd : "status");
    cJSON_AddBoolToObject(obj, "ok", 1);
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    OPERATE_RET rt = __send_json(str);
    cJSON_free(str);
    return rt;
}

bool buddy_ws_is_connected(void)
{
    return s_ws_connected;
}

/* ---- Connection loop ---- */

static void __ws_close(void)
{
    if (s_ws_fd >= 0) {
        tal_net_close(s_ws_fd);
        s_ws_fd = -1;
    }
    if (s_ws_connected) {
        s_ws_connected = FALSE;
        buddy_state_set_connected(false);
        PR_INFO("WS disconnected");
    }
    s_rx_len = 0;
}

static OPERATE_RET __ws_connect(void)
{
    if (s_host[0] == '\0') return OPRT_COM_ERROR;

    PR_NOTICE("WS connecting to %s:%u ...", s_host, s_port);

    TUYA_IP_ADDR_T addr = tal_net_str2addr(s_host);
    if (addr == 0) {
        if (tal_net_gethostbyname(s_host, &addr) != OPRT_OK || addr == 0) {
            PR_WARN("WS DNS resolve failed: %s", s_host);
            return OPRT_COM_ERROR;
        }
        PR_NOTICE("WS DNS resolved %s -> 0x%08x", s_host, (unsigned)addr);
    }

    int fd = tal_net_socket_create(PROTOCOL_TCP);
    if (fd < 0) {
        PR_ERR("WS socket create failed");
        return OPRT_SOCK_ERR;
    }

    OPERATE_RET rt = tal_net_connect(fd, addr, s_port);
    if (rt != OPRT_OK) {
        PR_WARN("WS TCP connect to %s:%u failed (rt=%d)", s_host, s_port, rt);
        tal_net_close(fd);
        return rt;
    }
    PR_NOTICE("WS TCP connected, starting handshake");

    s_ws_fd = fd;

    rt = __ws_handshake(fd);
    if (rt != OPRT_OK) {
        PR_WARN("WS handshake failed (rt=%d)", rt);
        __ws_close();
        return rt;
    }

    tal_net_set_block(fd, FALSE);

    s_ws_connected = TRUE;
    buddy_state_set_connected(true);
    PR_NOTICE("WS connected to %s:%u", s_host, s_port);
    return OPRT_OK;
}

static void __ws_task(void *arg)
{
    (void)arg;
    uint32_t backoff = WS_RECONNECT_MIN;

    PR_INFO("WS task started");

    while (s_ws_running) {
        if (!s_ws_connected) {
            if (s_host[0] == '\0') {
                tal_system_sleep(1000);
                continue;
            }
            OPERATE_RET rt = __ws_connect();
            if (rt != OPRT_OK) {
                PR_NOTICE("WS connect failed, retry in %u ms", backoff);
                tal_system_sleep(backoff);
                if (backoff < WS_RECONNECT_MAX) backoff *= 2;
                continue;
            }
            backoff = WS_RECONNECT_MIN;
        }

        TUYA_FD_SET_T readfds;
        TAL_FD_ZERO(&readfds);
        TAL_FD_SET(s_ws_fd, &readfds);

        int ready = tal_net_select(s_ws_fd + 1, &readfds, NULL, NULL, 200);
        if (ready < 0) {
            PR_WARN("WS select error (%d)", ready);
            __ws_close();
            continue;
        }
        if (ready == 0) continue;

        if (s_rx_len >= sizeof(s_rx_buf)) {
            PR_WARN("WS RX buffer overflow (rx_len=%d)", (int)s_rx_len);
            __ws_close();
            continue;
        }

        int n = tal_net_recv(s_ws_fd, s_rx_buf + s_rx_len,
                              (uint32_t)(sizeof(s_rx_buf) - s_rx_len));
        if (n == OPRT_RESOURCE_NOT_READY) continue;
        if (n <= 0) {
            PR_WARN("WS recv returned %d (peer closed or error)", n);
            __ws_close();
            continue;
        }
        s_rx_len += (size_t)n;

        while (s_rx_len > 0) {
            uint8_t *payload  = NULL;
            size_t   pay_len  = 0;
            uint8_t  opcode   = 0;
            size_t   consumed = 0;

            OPERATE_RET rt = __decode_ws_frame(&payload, &pay_len, &opcode, &consumed);
            if (rt == OPRT_RESOURCE_NOT_READY) break;
            if (rt != OPRT_OK) {
                PR_WARN("WS frame decode error rt=%d rx_len=%d", rt, (int)s_rx_len);
                tal_free(payload);
                __ws_close();
                break;
            }

            __consume_rx(consumed);

            if (opcode == 0x1) {
                PR_DEBUG("WS recv(%d): %.*s", (int)pay_len,
                         (int)(pay_len > 120 ? 120 : pay_len), (const char *)payload);
                buddy_protocol_on_recv((const char *)payload);
            } else if (opcode == 0x8) {
                PR_NOTICE("WS close frame from server (len=%d)", (int)pay_len);
                __send_ws_frame(s_ws_fd, 0x8, payload, pay_len);
                tal_free(payload);
                __ws_close();
                break;
            } else if (opcode == 0x9) {
                __send_ws_frame(s_ws_fd, 0xA, payload, pay_len);
            } else {
                PR_WARN("WS unknown opcode 0x%02x len=%d", opcode, (int)pay_len);
            }

            tal_free(payload);
        }
    }

    __ws_close();
    PR_INFO("WS task stopped");
}

/* ---- KV persistence ---- */

static void __load_kv_config(void)
{
    uint8_t *val = NULL;
    size_t len = 0;

    if (tal_kv_get(KV_KEY_HOST, &val, &len) == OPRT_OK && val) {
        strncpy(s_host, (char *)val, BUDDY_WS_HOST_LEN);
        s_host[BUDDY_WS_HOST_LEN] = '\0';
        tal_free(val);
    } else {
        s_host[0] = '\0';
    }

    val = NULL;
    len = 0;
    if (tal_kv_get(KV_KEY_PORT, &val, &len) == OPRT_OK && val) {
        s_port = (uint16_t)atoi((char *)val);
        if (s_port == 0) s_port = BUDDY_WS_DEFAULT_PORT;
        tal_free(val);
    }

    if (s_host[0]) {
        PR_INFO("WS config loaded: %s:%u", s_host, s_port);
    } else {
        PR_INFO("WS config not set. Use: buddy ws set <ip> [port]");
    }
}

OPERATE_RET buddy_ws_set_host(const char *host)
{
    if (!host || strlen(host) > BUDDY_WS_HOST_LEN) return OPRT_INVALID_PARM;
    strncpy(s_host, host, BUDDY_WS_HOST_LEN);
    s_host[BUDDY_WS_HOST_LEN] = '\0';
    return tal_kv_set(KV_KEY_HOST, (const uint8_t *)s_host, strlen(s_host) + 1);
}

/* ---- CLI commands ---- */

static void __cli_ws_handler(int argc, char *argv[])
{
    if (argc < 3 || strcmp(argv[1], "ws") != 0) {
        PR_NOTICE("Usage: buddy ws <set|status>");
        return;
    }

    if (strcmp(argv[2], "set") == 0) {
        if (argc < 4) {
            PR_NOTICE("Usage: buddy ws set <ip> [port]");
            return;
        }
        strncpy(s_host, argv[3], BUDDY_WS_HOST_LEN);
        s_host[BUDDY_WS_HOST_LEN] = '\0';
        tal_kv_set(KV_KEY_HOST, (const uint8_t *)s_host, strlen(s_host) + 1);

        if (argc >= 5) {
            s_port = (uint16_t)atoi(argv[4]);
            if (s_port == 0) s_port = BUDDY_WS_DEFAULT_PORT;
        } else {
            s_port = BUDDY_WS_DEFAULT_PORT;
        }
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", s_port);
        tal_kv_set(KV_KEY_PORT, (const uint8_t *)port_str, strlen(port_str) + 1);

        PR_NOTICE("WS target set to %s:%u", s_host, s_port);
        __ws_close();

    } else if (strcmp(argv[2], "status") == 0) {
        PR_NOTICE("WS host:      %s", s_host[0] ? s_host : "(not set)");
        PR_NOTICE("WS port:      %u", s_port);
        PR_NOTICE("WS connected: %s", s_ws_connected ? "yes" : "no");
    } else {
        PR_NOTICE("Unknown subcommand: %s", argv[2]);
    }
}

void buddy_ws_cli_register(void)
{
    static const cli_cmd_t cmds[] = {
        {.name = "buddy", .help = "buddy ws <set|status>", .func = __cli_ws_handler},
    };
    tal_cli_cmd_register(cmds, 1);
}

/* ---- Lifecycle ---- */

OPERATE_RET buddy_ws_init(void)
{
    __load_kv_config();

    if (!s_ws_tx_mutex) {
        OPERATE_RET rt = tal_mutex_create_init(&s_ws_tx_mutex);
        if (rt != OPRT_OK) return rt;
    }

    buddy_ws_cli_register();

    PR_INFO("buddy_ws_init done");
    return OPRT_OK;
}

OPERATE_RET buddy_ws_start(void *data)
{
    (void)data;
    if (s_ws_thread) return OPRT_OK;

    s_ws_running = TRUE;

    THREAD_CFG_T cfg = {0};
    cfg.stackDepth = WS_STACK_SIZE;
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "buddy_ws";

    OPERATE_RET rt = tal_thread_create_and_start(&s_ws_thread, NULL, NULL,
                                                  __ws_task, NULL, &cfg);
    if (rt != OPRT_OK) {
        s_ws_running = FALSE;
        PR_ERR("WS thread create failed rt=%d", rt);
    }
    return rt;
}

OPERATE_RET buddy_ws_stop(void)
{
    s_ws_running = FALSE;
    if (s_ws_thread) {
        tal_thread_delete(s_ws_thread);
        s_ws_thread = NULL;
    }
    __ws_close();
    return OPRT_OK;
}
