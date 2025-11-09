#include "nvs_flash.h"
#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include <wifi_connect.h>
#include "esp_wifi.h"

extern "C" void wifi_connect_start()
{
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for Wi-Fi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Get the Wi-Fi configuration
    auto &ssid_list = SsidManager::GetInstance().GetSsidList();
    if (ssid_list.empty())
    {
        // Start the Wi-Fi configuration AP
        auto &ap = WifiConfigurationAp::GetInstance();
        ap.SetSsidPrefix("CryptoTag");
        ap.Start();
        return;
    }

    // Otherwise, connect to the Wi-Fi network
    WifiStation::GetInstance().Start();
}



extern "C" int check_wifi_status(void)
{
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);

    if (ret == ESP_OK)
    {
        // ESP_LOGI(TAG, "Connected to SSID: %s", ap_info.ssid);
        // ESP_LOGI(TAG, "RSSI: %d", ap_info.rssi);
        return 1;
    }
    else
    {
        // ESP_LOGI(TAG, "Not connected to any AP");
        return 0;
    }
}