#include "tkl_bluetooth.h"

#include "att.h"
#include "ble_api.h"
#include "bt_event.h"
#include "bluetooth.h"
#include "btstack_event.h"
#include "btstack_task.h"
#include "gap.h"
#include "gatt.h"
#include "le_user.h"

#include <string.h>

/*
 * wl82 BLE integration notes
 * --------------------------
 *
 * The Jieli stack exposes a command-oriented LE API. This file translates
 * the TuyaOpen TKL API to that native interface.
 *
 * Jieli's ATT server database is a link-time/static profile database. It does
 * not provide a runtime service builder, so tkl_ble_gatts_service_add() is
 * deliberately reported as unsupported until a profile generator is added.
 */

static TKL_BLE_GAP_EVT_FUNC_CB s_gap_callback;
static TKL_BLE_GATT_EVT_FUNC_CB s_gatt_callback;

static OPERATE_RET jieli_ble_cmd_result(ble_cmd_ret_e result)
{
    return result == BLE_CMD_RET_SUCESS ? OPRT_OK : OPRT_COM_ERROR;
}

static void jieli_hci_event_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    TKL_BLE_GAP_PARAMS_EVT_T event;

    (void)packet_type;
    (void)channel;
    if (packet == NULL || s_gap_callback == NULL || size == 0) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.result = OPRT_OK;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        if (size < 6) {
            return;
        }
        event.type = TKL_BLE_GAP_EVT_DISCONNECT;
        event.conn_handle = hci_event_disconnection_complete_get_connection_handle(packet);
        event.gap_event.disconnect.role = TKL_BLE_ROLE_SERVER;
        event.gap_event.disconnect.reason = hci_event_disconnection_complete_get_reason(packet);
        s_gap_callback(&event);
        break;

    case HCI_EVENT_LE_META:
        if (size < 3) {
            return;
        }
        if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
            uint8_t peer_addr[6];
            event.type = TKL_BLE_GAP_EVT_CONNECT;
            event.conn_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            event.gap_event.connect.role = hci_subevent_le_connection_complete_get_role(packet) == 0
                                                ? TKL_BLE_ROLE_SERVER
                                                : TKL_BLE_ROLE_CLIENT;
            event.gap_event.connect.peer_addr.type =
                hci_subevent_le_connection_complete_get_peer_address_type(packet);
            hci_subevent_le_connection_complete_get_peer_address(packet, peer_addr);
            memcpy(event.gap_event.connect.peer_addr.addr, peer_addr, sizeof(peer_addr));
            event.gap_event.connect.conn_params.conn_interval_min =
                hci_subevent_le_connection_complete_get_conn_interval(packet);
            event.gap_event.connect.conn_params.conn_interval_max =
                event.gap_event.connect.conn_params.conn_interval_min;
            event.gap_event.connect.conn_params.conn_latency =
                hci_subevent_le_connection_complete_get_conn_latency(packet);
            event.gap_event.connect.conn_params.conn_sup_timeout =
                hci_subevent_le_connection_complete_get_supervision_timeout(packet);
            s_gap_callback(&event);
        }
        break;
    case GAP_EVENT_ADVERTISING_REPORT:
        if (size > 2) {
            adv_report_t *report = (adv_report_t *)&packet[2];
            event.type = TKL_BLE_GAP_EVT_ADV_REPORT;
            event.conn_handle = TKL_BLE_GATT_INVALID_HANDLE;
            event.gap_event.adv_report.adv_type =
                report->event_type == 4 ? TKL_BLE_RSP_DATA : TKL_BLE_ADV_DATA;
            event.gap_event.adv_report.peer_addr.type = report->address_type;
            memcpy(event.gap_event.adv_report.peer_addr.addr, report->address, 6);
            event.gap_event.adv_report.rssi = report->rssi;
            event.gap_event.adv_report.channel_index = 0;
            event.gap_event.adv_report.data.length = report->length;
            event.gap_event.adv_report.data.p_data = report->data;
            s_gap_callback(&event);
        }
        break;
    default:
        break;
    }
}

static OPERATE_RET jieli_ble_set_data(TKL_BLE_DATA_T const *data, uint8_t is_scan_rsp)
{
    if (data == NULL || data->p_data == NULL || data->length > TKL_BLE_GAP_ADV_SET_DATA_SIZE_MAX) {
        return OPRT_INVALID_PARM;
    }

    if (is_scan_rsp) {
        gap_scan_response_set_data((uint8_t)data->length, data->p_data);
    } else {
        gap_advertisements_set_data((uint8_t)data->length, data->p_data);
    }
    return OPRT_OK;
}

OPERATE_RET tkl_ble_stack_init(uint8_t role)
{
    if (role != TKL_BLE_ROLE_SERVER && role != TKL_BLE_ROLE_CLIENT) {
        return OPRT_INVALID_PARM;
    }
    ble_stack_gatt_role(role == TKL_BLE_ROLE_SERVER ? 0 : 1);
    hci_event_callback_set(jieli_hci_event_handler);
    return jieli_ble_cmd_result((ble_cmd_ret_e)btstack_init());
}

OPERATE_RET tkl_ble_stack_deinit(uint8_t role)
{
    if (role != TKL_BLE_ROLE_SERVER && role != TKL_BLE_ROLE_CLIENT) {
        return OPRT_INVALID_PARM;
    }
    return jieli_ble_cmd_result((ble_cmd_ret_e)btstack_exit());
}

OPERATE_RET tkl_ble_stack_gatt_link(uint16_t *p_link)
{
    if (p_link == NULL) {
        return OPRT_INVALID_PARM;
    }
    *p_link = 1;
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_callback_register(const TKL_BLE_GAP_EVT_FUNC_CB gap_evt)
{
    s_gap_callback = gap_evt;
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gatt_callback_register(const TKL_BLE_GATT_EVT_FUNC_CB gatt_evt)
{
    s_gatt_callback = gatt_evt;
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_addr_set(TKL_BLE_GAP_ADDR_T const *p_peer_addr)
{
    if (p_peer_addr == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (p_peer_addr->type == TKL_BLE_GAP_ADDR_TYPE_RANDOM) {
        return le_controller_set_random_mac((void *)p_peer_addr->addr) == 0 ? OPRT_OK : OPRT_COM_ERROR;
    }
    if (p_peer_addr->type == TKL_BLE_GAP_ADDR_TYPE_PUBLIC) {
        return le_controller_set_mac((void *)p_peer_addr->addr) == 0 ? OPRT_OK : OPRT_COM_ERROR;
    }
    return OPRT_INVALID_PARM;
}

OPERATE_RET tkl_ble_gap_address_get(TKL_BLE_GAP_ADDR_T *p_peer_addr)
{
    if (p_peer_addr == NULL) {
        return OPRT_INVALID_PARM;
    }
    p_peer_addr->type = TKL_BLE_GAP_ADDR_TYPE_PUBLIC;
    return le_controller_get_mac((void *)p_peer_addr->addr) == 0 ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET tkl_ble_gap_adv_start(TKL_BLE_GAP_ADV_PARAMS_T const *p_adv_params)
{
    if (p_adv_params == NULL || p_adv_params->adv_interval_min == 0 ||
        p_adv_params->adv_interval_min > p_adv_params->adv_interval_max) {
        return OPRT_INVALID_PARM;
    }
    gap_advertisements_set_params(p_adv_params->adv_interval_min, p_adv_params->adv_interval_max,
                                  p_adv_params->adv_type, p_adv_params->direct_addr.type,
                                  (uint8_t *)p_adv_params->direct_addr.addr, p_adv_params->adv_channel_map, 0);
    gap_advertisements_enable(1);
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_adv_stop(void)
{
    gap_advertisements_enable(0);
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_adv_rsp_data_set(TKL_BLE_DATA_T const *p_adv, TKL_BLE_DATA_T const *p_scan_rsp)
{
    OPERATE_RET result = OPRT_OK;
    if (p_adv != NULL && p_adv->p_data != NULL && p_adv->length != 0) {
        result = jieli_ble_set_data(p_adv, 0);
        if (result != OPRT_OK) {
            return result;
        }
    }
    if (p_scan_rsp != NULL && p_scan_rsp->p_data != NULL && p_scan_rsp->length != 0) {
        result = jieli_ble_set_data(p_scan_rsp, 1);
    }
    return result;
}

OPERATE_RET tkl_ble_gap_adv_rsp_data_update(TKL_BLE_DATA_T const *p_adv, TKL_BLE_DATA_T const *p_scan_rsp)
{
    return tkl_ble_gap_adv_rsp_data_set(p_adv, p_scan_rsp);
}

OPERATE_RET tkl_ble_gap_scan_start(TKL_BLE_GAP_SCAN_PARAMS_T const *p_scan_params)
{
    ble_cmd_ret_e result;
    if (p_scan_params == NULL || p_scan_params->interval == 0 || p_scan_params->window == 0 ||
        p_scan_params->window > p_scan_params->interval) {
        return OPRT_INVALID_PARM;
    }
    result = ble_user_cmd_prepare(BLE_CMD_SCAN_PARAM, 3, p_scan_params->active ? 0 : 1,
                                   p_scan_params->interval, p_scan_params->window);
    if (result != BLE_CMD_RET_SUCESS) {
        return jieli_ble_cmd_result(result);
    }
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_SCAN_ENABLE, 1, 1));
}

OPERATE_RET tkl_ble_gap_scan_stop(void)
{
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_SCAN_ENABLE, 1, 0));
}

OPERATE_RET tkl_ble_gap_connect(TKL_BLE_GAP_ADDR_T const *p_peer_addr,
                                TKL_BLE_GAP_SCAN_PARAMS_T const *p_scan_params,
                                TKL_BLE_GAP_CONN_PARAMS_T const *p_conn_params)
{
    struct create_conn_param_t param;
    if (p_peer_addr == NULL || p_conn_params == NULL) {
        return OPRT_INVALID_PARM;
    }
    (void)p_scan_params;
    memset(&param, 0, sizeof(param));
    param.conn_interval = p_conn_params->conn_interval_max;
    param.conn_latency = p_conn_params->conn_latency;
    param.supervision_timeout = p_conn_params->conn_sup_timeout;
    param.peer_address_type = p_peer_addr->type;
    memcpy(param.peer_address, p_peer_addr->addr, sizeof(param.peer_address));
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_CREATE_CONN, 1, &param));
}

OPERATE_RET tkl_ble_gap_disconnect(uint16_t conn_handle, uint8_t hci_reason)
{
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_DISCONNECT_EXT, 2, conn_handle, hci_reason));
}

OPERATE_RET tkl_ble_gap_conn_param_update(uint16_t conn_handle, TKL_BLE_GAP_CONN_PARAMS_T const *p_conn_params)
{
    struct conn_update_param_t param;
    if (p_conn_params == NULL) {
        return OPRT_INVALID_PARM;
    }
    param.interval_min = p_conn_params->conn_interval_min;
    param.interval_max = p_conn_params->conn_interval_max;
    param.latency = p_conn_params->conn_latency;
    param.timeout = p_conn_params->conn_sup_timeout;
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_REQ_CONN_PARAM_UPDATE, 2, conn_handle, &param));
}

OPERATE_RET tkl_ble_gap_tx_power_set(uint8_t role, int tx_power)
{
    (void)role;
    (void)tx_power;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_gap_rssi_get(uint16_t conn_handle)
{
    (void)conn_handle;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_gap_name_set(char *p_name)
{
    (void)p_name;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_gatts_service_add(TKL_BLE_GATTS_PARAMS_T *p_service)
{
    (void)p_service;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_gatts_service_change(uint16_t conn_handle, uint16_t start_handle, uint16_t end_handle)
{
    (void)conn_handle;
    (void)start_handle;
    (void)end_handle;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_gatts_value_set(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    (void)conn_handle;
    (void)char_handle;
    (void)p_data;
    (void)length;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_gatts_value_get(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    (void)conn_handle;
    (void)char_handle;
    (void)p_data;
    (void)length;
    return OPRT_NOT_SUPPORTED;
}

static OPERATE_RET jieli_ble_att_send(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data,
                                      uint16_t length, uint8_t operation)
{
    if (p_data == NULL || length == 0 || length > 512) {
        return OPRT_INVALID_PARM;
    }
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_MULTI_ATT_SEND_DATA, 5, conn_handle, char_handle,
                                                     p_data, length, operation));
}

OPERATE_RET tkl_ble_gatts_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    return jieli_ble_att_send(conn_handle, char_handle, p_data, length, ATT_OP_NOTIFY);
}

OPERATE_RET tkl_ble_gatts_value_indicate(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    return jieli_ble_att_send(conn_handle, char_handle, p_data, length, ATT_OP_INDICATE);
}

OPERATE_RET tkl_ble_gatts_exchange_mtu_reply(uint16_t conn_handle, uint16_t server_rx_mtu)
{
    if (server_rx_mtu < ATT_DEFAULT_MTU) {
        return OPRT_INVALID_PARM;
    }
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_MULTI_ATT_MTU_SIZE, 2, conn_handle, server_rx_mtu));
}

OPERATE_RET tkl_ble_gattc_all_service_discovery(uint16_t conn_handle)
{
    (void)conn_handle;
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_SEARCH_PROFILE, 2, PFL_SERVER_ALL, 0));
}

OPERATE_RET tkl_ble_gattc_all_char_discovery(uint16_t conn_handle, uint16_t start_handle, uint16_t end_handle)
{
    (void)conn_handle;
    (void)start_handle;
    (void)end_handle;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_gattc_char_desc_discovery(uint16_t conn_handle, uint16_t start_handle, uint16_t end_handle)
{
    (void)conn_handle;
    (void)start_handle;
    (void)end_handle;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_gattc_write_without_rsp(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data,
                                            uint16_t length)
{
    if (p_data == NULL || length == 0) {
        return OPRT_INVALID_PARM;
    }
    return gatt_client_write_value_of_characteristic_without_response(conn_handle, char_handle, length, p_data) == 0
               ? OPRT_OK
               : OPRT_COM_ERROR;
}

OPERATE_RET tkl_ble_gattc_write(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    if (p_data == NULL || length == 0) {
        return OPRT_INVALID_PARM;
    }
    return gatt_client_write_value_of_characteristic(jieli_hci_event_handler, conn_handle, char_handle, length, p_data) == 0
               ? OPRT_OK
               : OPRT_COM_ERROR;
}

OPERATE_RET tkl_ble_gattc_read(uint16_t conn_handle, uint16_t char_handle)
{
    return gatt_client_read_value_of_characteristic_using_value_handle(jieli_hci_event_handler, conn_handle, char_handle) == 0
               ? OPRT_OK
               : OPRT_COM_ERROR;
}

OPERATE_RET tkl_ble_gattc_exchange_mtu_request(uint16_t conn_handle, uint16_t client_rx_mtu)
{
    if (client_rx_mtu < ATT_DEFAULT_MTU) {
        return OPRT_INVALID_PARM;
    }
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_MULTI_ATT_MTU_SIZE, 2, conn_handle, client_rx_mtu));
}

OPERATE_RET tkl_ble_vendor_command_control(uint16_t opcode, void *user_data, uint16_t data_len)
{
    (void)opcode;
    (void)user_data;
    (void)data_len;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ble_set_mode(const BOOL_T enable, const uint8_t mode)
{
    (void)mode;
    return jieli_ble_cmd_result(ble_user_cmd_prepare(BLE_CMD_ADV_ENABLE, 1, enable ? 1 : 0));
}

/* The vendor BR/EDR glue expects this hook even for LE-only applications. */
int bt_event_notify(enum bt_event_from from, struct bt_event *event)
{
    (void)from;
    (void)event;
    return 0;
}
