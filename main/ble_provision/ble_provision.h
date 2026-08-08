/*
 * Module: ble_provision
 * ---------------------
 * Nạp SSID + password WiFi cho mạch qua BLE, thay cho SoftAP captive portal
 * (phần cứng phát AP lệch băng tần 2.4GHz nên điện thoại không thấy AP).
 *
 * Điện thoại cài app chính hãng Espressif "ESP BLE Provisioning", quét thấy
 * thiết bị tên "PROV_xxxxxx", nhập PIN rồi gửi SSID/password xuống.
 *
 * Module này ĐỘC LẬP với wifi_config: nó chỉ lo phần BLE. Credentials nhận được
 * lưu vào NVS của esp_wifi (cơ chế persistent của arduino-esp32), nên
 * wifi_config_connect() (WiFiManager.autoConnect) ở lần kết nối sau vẫn dùng lại
 * bình thường. Sau khi nhận creds thành công, thiết bị tự khởi động lại.
 *
 * Yêu cầu build: CONFIG_BT_ENABLED + CONFIG_BT_BLUEDROID_ENABLED
 * (WiFiProv của arduino-esp32 chỉ mở scheme BLE khi Bluedroid bật).
 */
#ifndef BLE_PROVISION_H
#define BLE_PROVISION_H

#include <stdint.h>

/**
 * @brief Mở BLE provisioning, hiển thị hướng dẫn trên OLED và chờ điện thoại.
 *        Blocking. Thoát khi: nhận creds thành công (tự reboot), người dùng bấm
 *        OK để hủy, hoặc hết thời gian chờ.
 *
 * @param btn_ok_pin  GPIO nút OK để hủy giữa chừng (active LOW).
 * @return true nếu đã nhận credentials thành công (thiết bị sẽ reboot),
 *         false nếu hủy / timeout.
 */
bool ble_provision_run(uint8_t btn_ok_pin);

#endif // BLE_PROVISION_H
