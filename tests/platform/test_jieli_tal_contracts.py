import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ADAPTER = ROOT / "platform/JIELI/tuyaos/tuyaos_adapter"


class JieliTalContractTest(unittest.TestCase):
    def test_system_and_uart_layers_are_present(self):
        for relative in (
            "include/tal_system_port.h",
            "include/tal_uart_port.h",
            "src/tal_system.c",
            "src/tal_uart.c",
            "src/tkl_thread.c",
            "src/tkl_mutex.c",
            "src/tkl_semaphore.c",
            "src/tkl_queue.c",
            "src/tkl_sleep.c",
        ):
            self.assertTrue((ADAPTER / relative).is_file(), relative)

    def test_tal_sources_do_not_depend_on_tuya_types(self):
        for source in (ADAPTER / "src").glob("tal_*.c"):
            text = source.read_text(encoding="utf-8")
            self.assertNotIn("tuya_cloud_types.h", text, source.name)

    def test_uart_contract_keeps_jieli_device_api_private(self):
        source = (ADAPTER / "src/tal_uart.c").read_text(encoding="utf-8")
        self.assertIn("dev_open", source)
        self.assertIn("dev_read", source)
        self.assertIn("dev_write", source)
        self.assertNotIn("tkl_uart.h", source)

    def test_network_layer_uses_lwip_socket_api(self):
        source = (ADAPTER / "src/tkl_network.c").read_text(encoding="utf-8")
        self.assertIn("<lwip/sockets.h>", source)
        for symbol in ("tkl_net_socket_create", "tkl_net_connect", "tkl_net_send", "tkl_net_recv",
                       "tkl_net_gethostbyname"):
            self.assertIn(symbol, source)

    def test_wifi_layer_uses_wl82_native_station_api(self):
        source = (ADAPTER / "src/tkl_wifi.c").read_text(encoding="utf-8")
        for symbol in ("wifi_set_event_callback", "wifi_enter_sta_mode", "wifi_get_sta_connect_state",
                       "wifi_scan_req", "lwip_get_netif_info"):
            self.assertIn(symbol, source)

    def test_bluetooth_layer_uses_wl82_native_le_api(self):
        source = (ADAPTER / "src/tkl_bluetooth.c").read_text(encoding="utf-8")
        for symbol in ("btstack_init", "ble_user_cmd_prepare", "gap_advertisements_enable",
                       "gatt_client_write_value_of_characteristic"):
            self.assertIn(symbol, source)


if __name__ == "__main__":
    unittest.main()
