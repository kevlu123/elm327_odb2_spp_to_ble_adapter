/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "app.h"
#include "ledmgr.h"
#include "gattcomm.h"
#include "sppcomm.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>

#define TAG "MAIN"

void app_main(void)
{
    esp_err_t err;

    ledmgr_init();

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err)
        {
            ESP_LOGE(TAG, "nvs_flash_erase failed: %d", err);
            panic(PANIC_ID_MAIN_NVS_FLASH_ERASE_FAILED);
        }
        err = nvs_flash_init();
        if (err)
        {
            ESP_LOGE(TAG, "nvs_flash_init failed: %d", err);
            panic(PANIC_ID_MAIN_NVS_FLASH_INIT_FAILED);
        }
    }
    else if (err)
    {
        ESP_LOGE(TAG, "nvs_flash_init failed: %d", err);
        panic(PANIC_ID_MAIN_NVS_FLASH_INIT_FAILED2);
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err)
    {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", err);
        panic(PANIC_ID_MAIN_BT_CONTROLLER_INIT_FAILED);
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (err)
    {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", err);
        panic(PANIC_ID_MAIN_BT_CONTROLLER_ENABLE_FAILED);
    }

    err = esp_bluedroid_init();
    if (err)
    {
        ESP_LOGE(TAG, "esp_bluedroid_init failed: %d", err);
        panic(PANIC_ID_MAIN_BLUEDROID_INIT_FAILED);
    }

    err = esp_bluedroid_enable();
    if (err)
    {
        ESP_LOGE(TAG, "esp_bluedroid_enable failed: %d", err);
        panic(PANIC_ID_MAIN_BLUEDROID_ENABLE_FAILED);
    }

    gattcomm_init();
    sppcomm_init();
}
