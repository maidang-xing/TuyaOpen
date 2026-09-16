#include "tkl_wifi.h"
#include "tkl_memory.h"
#include "tkl_system.h"
#include "tuya_error_code.h"

#include "lwip/port/lwip.h"

#include <stdio.h>
#include <string.h>

static WIFI_EVENT_CB s_wifi_event_cb;
static WF_WK_MD_E s_wifi_mode = WWM_STATION;

/* The wl82 SDK's wifi_def.h uses a C++-only enum underlying-type syntax.
 * Keep the small ABI-facing declarations C-compatible in the Tuya adapter. */
enum jieli_wifi_state {
    JIELI_WIFI_DISCONNECT,
    JIELI_WIFI_CONNECT_SUCC,
    JIELI_WIFI_CONNECT_NO_SSID,
    JIELI_WIFI_CONNECT_ASSOC_FAIL,
    JIELI_WIFI_CONNECT_ASSOC_TIMEOUT,
    JIELI_WIFI_STATE_DHCP_SUCC,
    JIELI_WIFI_STATE_DHCP_TIMEOUT,
};

enum jieli_wifi_event {
    JIELI_WIFI_MODULE_INIT,
    JIELI_WIFI_MODULE_START,
    JIELI_WIFI_MODULE_STOP,
    JIELI_WIFI_MODULE_START_ERR,
    JIELI_WIFI_AP_START,
    JIELI_WIFI_AP_STOP,
    JIELI_WIFI_STA_START,
    JIELI_WIFI_STA_STOP,
    JIELI_WIFI_STA_SCAN_COMPLETED,
    JIELI_WIFI_STA_CONNECT_SUCC,
    JIELI_WIFI_STA_CONNECT_NO_SSID,
    JIELI_WIFI_STA_CONNECT_ASSOC_FAIL,
    JIELI_WIFI_STA_CONNECT_ASSOC_TIMEOUT,
    JIELI_WIFI_STA_DISCONNECT,
    JIELI_WIFI_SMP_CFG_START,
    JIELI_WIFI_SMP_CFG_STOP,
    JIELI_WIFI_SMP_CFG_TIMEOUT,
    JIELI_WIFI_SMP_CFG_COMPLETED,
    JIELI_WIFI_DHCP_SUCC,
    JIELI_WIFI_DHCP_TIMEOUT,
};

enum jieli_wifi_auth_mode {
    JIELI_WIFI_AUTH_OPEN,
    JIELI_WIFI_AUTH_WEP,
    JIELI_WIFI_AUTH_WAPI,
    JIELI_WIFI_AUTH_WPA,
    JIELI_WIFI_AUTH_WPA2,
    JIELI_WIFI_AUTH_WPA_WPA2,
    JIELI_WIFI_AUTH_WPA3,
};

struct jieli_wifi_scan_info {
    unsigned char ssid_len;
    char ssid[33];
    unsigned char mac_addr[6];
    char rssi;
    char snr;
    char rssi_db;
    char rssi_rsv;
    char channel_number;
    unsigned char signal_strength;
    unsigned char signal_quality;
    unsigned char supported_rates[16];
    enum jieli_wifi_auth_mode auth_mode;
};

struct jieli_wifi_mode_info {
    int mode;
    char *ssid;
    char *pwd;
};

extern void wifi_set_event_callback(int (*cb)(void *, int));
extern int wifi_on(void);
extern int wifi_off(void);
extern int wifi_get_mac(unsigned char *mac);
extern int wifi_set_mac(char *mac);
extern unsigned int wifi_get_channel(void);
extern void wifi_set_channel(unsigned char channel);
extern void wifi_get_bssid(unsigned char bssid[6]);
extern void wifi_get_mode_cur_info(struct jieli_wifi_mode_info *info);
extern int wifi_enter_sta_mode(char *ssid, char *password);
extern int wifi_enter_ap_mode(char *ssid, char *password);
extern int wifi_scan_req(void);
extern struct jieli_wifi_scan_info *wifi_get_scan_result(unsigned int *count);
extern void wifi_clear_scan_result(void);
extern char wifi_get_rssi(void);
extern enum jieli_wifi_state wifi_get_sta_connect_state(void);
extern int wifi_enter_smp_cfg_mode(void);

static OPERATE_RET jieli_result(int result)
{
    return (result == 0) ? OPRT_OK : OPRT_COM_ERROR;
}

static int jieli_wifi_event_cb(void *priv, int event)
{
    (void)priv;
    if (!s_wifi_event_cb) {
        return 0;
    }

    switch (event) {
    case JIELI_WIFI_STA_CONNECT_SUCC:
    case JIELI_WIFI_DHCP_SUCC:
        s_wifi_event_cb(WFE_CONNECTED, NULL);
        break;
    case JIELI_WIFI_STA_CONNECT_NO_SSID:
    case JIELI_WIFI_STA_CONNECT_ASSOC_FAIL:
    case JIELI_WIFI_STA_CONNECT_ASSOC_TIMEOUT:
        s_wifi_event_cb(WFE_CONNECT_FAILED, NULL);
        break;
    case JIELI_WIFI_STA_DISCONNECT:
    case JIELI_WIFI_DHCP_TIMEOUT:
    case JIELI_WIFI_MODULE_STOP:
        s_wifi_event_cb(WFE_DISCONNECTED, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t jieli_auth_mode(enum jieli_wifi_auth_mode mode)
{
    switch (mode) {
    case JIELI_WIFI_AUTH_OPEN:
        return WAAM_OPEN;
    case JIELI_WIFI_AUTH_WEP:
        return WAAM_WEP;
    case JIELI_WIFI_AUTH_WPA:
        return WAAM_WPA_PSK;
    case JIELI_WIFI_AUTH_WPA2:
        return WAAM_WPA2_PSK;
    case JIELI_WIFI_AUTH_WPA_WPA2:
        return WAAM_WPA_WPA2_PSK;
    case JIELI_WIFI_AUTH_WPA3:
        return WAAM_WPA_WPA3_SAE;
    default:
        return WAAM_UNKNOWN;
    }
}

OPERATE_RET tkl_wifi_init(WIFI_EVENT_CB cb)
{
    s_wifi_event_cb = cb;
    wifi_set_event_callback(jieli_wifi_event_cb);
    return jieli_result(wifi_on());
}

OPERATE_RET tkl_wifi_scan_ap(const int8_t *ssid, AP_IF_S **ap_ary, uint32_t *num)
{
    uint32_t count = 0;
    struct jieli_wifi_scan_info *result;
    AP_IF_S *aps;
    uint32_t out_count = 0;

    if (!ap_ary || !num) {
        return OPRT_INVALID_PARM;
    }
    *ap_ary = NULL;
    *num = 0;

    if (wifi_scan_req() != 0) {
        return OPRT_COM_ERROR;
    }
    for (uint32_t retry = 0; retry < 100; ++retry) {
        result = wifi_get_scan_result(&count);
        if (result && count) {
            break;
        }
        tkl_system_delay(100);
    }
    if (!result || count == 0) {
        wifi_clear_scan_result();
        return OPRT_OK;
    }

    aps = (AP_IF_S *)tkl_system_calloc(count, sizeof(AP_IF_S));
    if (!aps) {
        wifi_clear_scan_result();
        return OPRT_MALLOC_FAILED;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (ssid && (strlen((const char *)ssid) != result[i].ssid_len ||
                     memcmp(ssid, result[i].ssid, result[i].ssid_len) != 0)) {
            continue;
        }
        aps[out_count].channel = (uint8_t)result[i].channel_number;
        aps[out_count].rssi = result[i].rssi;
        memcpy(aps[out_count].bssid, result[i].mac_addr, sizeof(aps[out_count].bssid));
        aps[out_count].s_len = result[i].ssid_len;
        memcpy(aps[out_count].ssid, result[i].ssid, result[i].ssid_len);
        aps[out_count].ssid[result[i].ssid_len] = '\0';
        aps[out_count].security = jieli_auth_mode(result[i].auth_mode);
        ++out_count;
    }
    wifi_clear_scan_result();
    if (!out_count) {
        tkl_system_free(aps);
        return OPRT_OK;
    }
    *ap_ary = aps;
    *num = out_count;
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_release_ap(AP_IF_S *ap)
{
    tkl_system_free(ap);
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_start_ap(const WF_AP_CFG_IF_S *cfg)
{
    if (!cfg) {
        return OPRT_INVALID_PARM;
    }
    if (cfg->chan) {
        wifi_set_channel(cfg->chan);
    }
    s_wifi_mode = WWM_SOFTAP;
    return jieli_result(wifi_enter_ap_mode((char *)cfg->ssid, (char *)cfg->passwd));
}

OPERATE_RET tkl_wifi_stop_ap(void)
{
    s_wifi_mode = WWM_POWERDOWN;
    return jieli_result(wifi_off());
}

OPERATE_RET tkl_wifi_set_cur_channel(const uint8_t chan)
{
    wifi_set_channel(chan);
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_get_cur_channel(uint8_t *chan)
{
    if (!chan) {
        return OPRT_INVALID_PARM;
    }
    *chan = (uint8_t)wifi_get_channel();
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_set_sniffer(const BOOL_T en, const SNIFFER_CALLBACK cb)
{
    (void)en;
    (void)cb;
    return OPRT_NOT_SUPPORTED;
}

static void jieli_ip_to_text(uint32_t value, char *text, size_t text_size)
{
    ip4addr_ntoa_r((const ip4_addr_t *)&value, text, text_size);
}

OPERATE_RET tkl_wifi_get_ip(const WF_IF_E wf, NW_IP_S *ip)
{
    struct netif_info info;
    (void)wf;
    if (!ip) {
        return OPRT_INVALID_PARM;
    }
    memset(ip, 0, sizeof(*ip));
    memset(&info, 0, sizeof(info));
    lwip_get_netif_info(WIFI_NETIF, &info);
    jieli_ip_to_text(info.ip, ip->ip, sizeof(ip->ip));
    jieli_ip_to_text(info.netmask, ip->mask, sizeof(ip->mask));
    jieli_ip_to_text(info.gw, ip->gw, sizeof(ip->gw));
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_get_ipv6(const WF_IF_E wf, NW_IP_TYPE type, NW_IP_S *ip)
{
    (void)wf;
    (void)type;
    if (!ip) {
        return OPRT_INVALID_PARM;
    }
    memset(ip, 0, sizeof(*ip));
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_set_ip(const WF_IF_E wf, NW_IP_S *ip)
{
    (void)wf;
    (void)ip;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_set_mac(const WF_IF_E wf, const NW_MAC_S *mac)
{
    (void)wf;
    if (!mac) {
        return OPRT_INVALID_PARM;
    }
    return jieli_result(wifi_set_mac((char *)mac->mac));
}

OPERATE_RET tkl_wifi_get_mac(const WF_IF_E wf, NW_MAC_S *mac)
{
    (void)wf;
    if (!mac) {
        return OPRT_INVALID_PARM;
    }
    return jieli_result(wifi_get_mac(mac->mac));
}

OPERATE_RET tkl_wifi_set_work_mode(const WF_WK_MD_E mode)
{
    switch (mode) {
    case WWM_POWERDOWN:
        s_wifi_mode = mode;
        return jieli_result(wifi_off());
    case WWM_STATION:
        s_wifi_mode = mode;
        return jieli_result(wifi_on());
    case WWM_SNIFFER:
        s_wifi_mode = mode;
        return jieli_result(wifi_enter_smp_cfg_mode());
    default:
        return OPRT_NOT_SUPPORTED;
    }
}

OPERATE_RET tkl_wifi_get_work_mode(WF_WK_MD_E *mode)
{
    struct jieli_wifi_mode_info info;
    if (!mode) {
        return OPRT_INVALID_PARM;
    }
    memset(&info, 0, sizeof(info));
    wifi_get_mode_cur_info(&info);
    switch (info.mode) {
    case 1:
        *mode = WWM_STATION;
        break;
    case 2:
        *mode = WWM_SOFTAP;
        break;
    case 4:
        *mode = WWM_SNIFFER;
        break;
    default:
        *mode = s_wifi_mode;
        break;
    }
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_get_connected_ap_info(FAST_WF_CONNECTED_AP_INFO_T **fast_ap_info)
{
    if (fast_ap_info) {
        *fast_ap_info = NULL;
    }
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_get_bssid(uint8_t *mac)
{
    if (!mac) {
        return OPRT_INVALID_PARM;
    }
    wifi_get_bssid(mac);
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_set_country_code(const COUNTRY_CODE_E ccode)
{
    (void)ccode;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_set_rf_calibrated(void) { return OPRT_OK; }

OPERATE_RET tkl_wifi_set_lp_mode(const BOOL_T enable, const uint8_t dtim)
{
    (void)enable;
    (void)dtim;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_station_fast_connect(const FAST_WF_CONNECTED_AP_INFO_T *fast_ap_info)
{
    (void)fast_ap_info;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_station_connect(const int8_t *ssid, const int8_t *passwd)
{
    if (!ssid || !passwd) {
        return OPRT_INVALID_PARM;
    }
    s_wifi_mode = WWM_STATION;
    return jieli_result(wifi_enter_sta_mode((char *)ssid, (char *)passwd));
}

OPERATE_RET tkl_wifi_station_disconnect(void)
{
    return jieli_result(wifi_off());
}

OPERATE_RET tkl_wifi_station_get_conn_ap_rssi(int8_t *rssi)
{
    if (!rssi) {
        return OPRT_INVALID_PARM;
    }
    *rssi = wifi_get_rssi();
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_station_get_status(WF_STATION_STAT_E *stat)
{
    if (!stat) {
        return OPRT_INVALID_PARM;
    }
    switch (wifi_get_sta_connect_state()) {
    case JIELI_WIFI_CONNECT_SUCC:
        *stat = WSS_CONN_SUCCESS;
        break;
    case JIELI_WIFI_STATE_DHCP_SUCC:
        *stat = WSS_GOT_IP;
        break;
    case JIELI_WIFI_CONNECT_NO_SSID:
        *stat = WSS_NO_AP_FOUND;
        break;
    case JIELI_WIFI_CONNECT_ASSOC_FAIL:
    case JIELI_WIFI_CONNECT_ASSOC_TIMEOUT:
        *stat = WSS_CONN_FAIL;
        break;
    default:
        *stat = WSS_IDLE;
        break;
    }
    return OPRT_OK;
}

OPERATE_RET tkl_wifi_send_mgnt(const uint8_t *buf, const uint32_t len)
{
    (void)buf;
    (void)len;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_register_recv_mgnt_callback(const BOOL_T enable, const WIFI_REV_MGNT_CB recv_cb)
{
    (void)enable;
    (void)recv_cb;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_ioctl(WF_IOCTL_CMD_E cmd, void *args)
{
    if (cmd == WFI_CONNECT_CMD && args) {
        WF_IOCTL_CONN_T *conn = (WF_IOCTL_CONN_T *)args;
        return tkl_wifi_station_connect((const int8_t *)conn->ssid, (const int8_t *)conn->passwd);
    }
    return OPRT_NOT_SUPPORTED;
}
