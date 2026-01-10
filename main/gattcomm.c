#include "gattcomm.h"
#include "app.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_system.h"
#include "sdkconfig.h"

#define TAG                "GATT_COMM"
#define ADV_NAME           "VLINK ADAPTER"
#define SERVICE_UUID_BYTES 0xad, 0xe6, 0x50, 0xf7, 0x81, 0x56, 0xa9, 0xeb, 0x49, 0x64, 0x6d, 0x6d, 0x9f, 0xca, 0x89, 0xfe,
#define CHAR_UUID_BYTES    0xe2, 0x05, 0xe7, 0x16, 0x87, 0x58, 0x5d, 0xb7, 0x34, 0x4a, 0x01, 0xd3, 0xcb, 0x0d, 0x20, 0x8a,

static struct
{
    bool adv_data_complete;
    bool scan_rsp_data_complete;
    esp_gatt_if_t gatts_if;
    esp_gatt_srvc_id_t service_id;
    uint16_t service_handle;
    uint16_t char_handle;
    uint16_t cccd_handle;
    uint16_t conn_id;
    bool notify_enabled;
} ctx;

#define CONN_ID_INVALID 0xFFFF

static esp_gatt_srvc_id_t SERVICE_ID = {
    .is_primary = true,
    .id.inst_id = 0,
    .id.uuid.len = ESP_UUID_LEN_128,
    .id.uuid.uuid.uuid128 = { SERVICE_UUID_BYTES }
};

static esp_bt_uuid_t CHAR_UUID = {
    .len = ESP_UUID_LEN_128,
    .uuid.uuid128 = { CHAR_UUID_BYTES }
};

static esp_bt_uuid_t CCCD_UUID = {
    .len = ESP_UUID_LEN_16,
    .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,
};

static esp_ble_adv_data_t ADV_DATA = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

static esp_ble_adv_data_t SCAN_RSP_DATA = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

static esp_ble_adv_params_t ADV_PARAMS = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void start_advertising(void)
{
    esp_err_t err;

    err = esp_ble_gap_start_advertising(&ADV_PARAMS);
    if (err)
    {
        ESP_LOGE(TAG, "esp_ble_gap_start_advertising failed: %d", err);
        panic(PANIC_ID_ADV_START_FAILED);
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT");
        ctx.adv_data_complete = true;
        if (ctx.adv_data_complete && ctx.scan_rsp_data_complete)
        {
            start_advertising();
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT");
        ctx.scan_rsp_data_complete = true;
        if (ctx.adv_data_complete && ctx.scan_rsp_data_complete)
        {
            start_advertising();
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_START_COMPLETE_EVT");
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "ESP_GAP_BLE_ADV_START_COMPLETE_EVT failed: %d",
                     param->adv_start_cmpl.status);
            panic(PANIC_ID_ADV_START_FAILED2);
        }
        break;

    default:
        break;
    }
}

static void handle_cccd_write(esp_gatt_if_t gatts_if,
                              esp_ble_gatts_cb_param_t *param)
{
    esp_err_t err;

    if (param->write.len != 2)
    {
        ESP_LOGE(TAG, "ESP_GATTS_WRITE_EVT invalid cccd length");
        return;
    }

    if ((param->write.value[0] & 1))
    {
        ESP_LOGI(TAG, "ESP_GATTS_WRITE_EVT notify enabled");
        ctx.notify_enabled = true;
        err = esp_ble_gatts_send_indicate(gatts_if,
                                            param->write.conn_id,
                                            ctx.char_handle,
                                            0,
                                            NULL,
                                            false);
        if (err)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_send_indicate failed: %d", err);
        }
    }
    else
    {
        ESP_LOGI(TAG, "ESP_GATTS_WRITE_EVT notify disabled");
        ctx.notify_enabled = false;
    }
}

static void handle_char_write(esp_gatt_if_t gatts_if,
                              esp_ble_gatts_cb_param_t *param)
{
    app_on_gatt_rx(param->write.value, param->write.len);

    static const char ATZ[4] = "ATZ\r";
    if (param->write.len == sizeof(ATZ) && 
        memcmp(param->write.value, ATZ, sizeof(ATZ)) == 0)
    {
        ESP_LOGI(TAG, "Received ATZ command, waiting 500ms");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    esp_err_t err;

    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_REG_EVT");
        if (param->reg.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "ESP_GATTS_REG_EVT failed: app_id=%d status=%d",
                     param->reg.app_id,
                     param->reg.status);
            panic(PANIC_ID_GATTS_REG_EVT_FAILED);
        }
        ctx.gatts_if = gatts_if;

        err = esp_ble_gap_set_device_name(ADV_NAME);
        if (err)
        {
            ESP_LOGE(TAG, "esp_ble_gap_set_device_name failed: %d", err);
            panic(PANIC_ID_SET_DEVICE_NAME_FAILED);
        }

        err = esp_ble_gap_config_adv_data(&ADV_DATA);
        if (err)
        {
            ESP_LOGE(TAG, "esp_ble_gap_config_adv_data failed: %d", err);
            panic(PANIC_ID_CONFIG_ADV_DATA_FAILED);
        }

        err = esp_ble_gap_config_adv_data(&SCAN_RSP_DATA);
        if (err)
        {
            ESP_LOGE(TAG, "esp_ble_gap_config_adv_data failed: %d", err);
            panic(PANIC_ID_CONFIG_SCAN_RSP_DATA_FAILED);
        }

        err = esp_ble_gatts_create_service(gatts_if, &SERVICE_ID, 16);
        if (err)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_create_service failed: %d", err);
            panic(PANIC_ID_CREATE_SERVICE_FAILED);
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_CREATE_EVT");
        ctx.service_handle = param->create.service_handle;

        err = esp_ble_gatts_start_service(ctx.service_handle);
        if (err)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_start_service failed: %d", err);
            panic(PANIC_ID_START_SERVICE_FAILED);
        }

        err = esp_ble_gatts_add_char(ctx.service_handle,
                                     &CHAR_UUID,
                                     ESP_GATT_PERM_WRITE,
                                     ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                     NULL,
                                     NULL);
        if (err)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_add_char failed: %d", err);
            panic(PANIC_ID_ADD_CHAR_FAILED);
        }
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_ADD_CHAR_EVT");
        ctx.char_handle = param->add_char.attr_handle;

        err = esp_ble_gatts_add_char_descr(ctx.service_handle,
                                           &CCCD_UUID,
                                           ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                           NULL,
                                           NULL);
        if (err)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_add_char_descr failed: %d", err);
        }
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_ADD_CHAR_DESCR_EVT");
        ctx.cccd_handle = param->add_char_descr.attr_handle;
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_CONNECT_EVT: conn_id=%d remote=%02x:%02x:%02x:%02x:%02x:%02x",
                 param->connect.conn_id,
                 param->connect.remote_bda[0],
                 param->connect.remote_bda[1],
                 param->connect.remote_bda[2],
                 param->connect.remote_bda[3],
                 param->connect.remote_bda[4],
                 param->connect.remote_bda[5]);
        if (ctx.conn_id != CONN_ID_INVALID)
        {
            ESP_LOGW(TAG, "Already connected, disconnecting new connection");
            esp_ble_gatts_close(gatts_if, param->connect.conn_id);
            break;
        }
        ctx.conn_id = param->connect.conn_id;
        ctx.notify_enabled = false;
        app_on_gatt_connected();
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_DISCONNECT_EVT: disconnect reason=%d",
                 param->disconnect.reason);
        ctx.conn_id = CONN_ID_INVALID;
        start_advertising();
        app_on_gatt_disconnected();
        break;

    case ESP_GATTS_READ_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_READ_EVT: conn_id=%d trans_id=%"PRIu32" handle=%d offset=%d",
                 param->read.conn_id,
                 param->read.trans_id,
                 param->read.handle,
                 param->read.offset);
        esp_gatt_rsp_t rsp = {
            .attr_value.handle = param->read.handle,
            .attr_value.len = 2,
            .attr_value.value = { ctx.notify_enabled, 0 }, 
        };
        err = esp_ble_gatts_send_response(gatts_if,
                                          param->read.conn_id,
                                          param->read.trans_id,
                                          ESP_GATT_OK,
                                          &rsp);
        if (err)
        {
            ESP_LOGW(TAG, "esp_ble_gatts_send_response failed: %d", err);
            gattcomm_disconnect();
        }
        break;

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_WRITE_EVT: conn_id=%d trans_id=%"PRIu32" handle=%d write_len=%d",
                 param->write.conn_id,
                 param->write.trans_id,
                 param->write.handle,
                 param->write.len);
        ESP_LOG_BUFFER_HEX(TAG, param->write.value, param->write.len);

        if (param->write.is_prep)
        {
            ESP_LOGW(TAG, "ESP_GATTS_WRITE_EVT prepare write not supported");
            gattcomm_disconnect();
            break;
        }

        if (param->write.handle == ctx.cccd_handle)
        {
            handle_cccd_write(gatts_if, param);
        }
        else if (param->write.handle == ctx.char_handle)
        {
            handle_char_write(gatts_if, param);
        }

        err = esp_ble_gatts_send_response(gatts_if,
                                          param->write.conn_id,
                                          param->write.trans_id,
                                          ESP_GATT_OK,
                                          NULL);
        if (err)
        {
            ESP_LOGW(TAG, "esp_ble_gatts_send_response failed: %d", err);
            gattcomm_disconnect();
        }
        break;

    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGW(TAG, "ESP_GATTS_EXEC_WRITE_EVT not supported");
        gattcomm_disconnect();
        break;

    default:
        break;
    }
}

void gattcomm_init(void)
{
    esp_err_t err;

    ctx.conn_id = CONN_ID_INVALID;

    err = esp_ble_gap_register_callback(gap_event_handler);
    if (err)
    {
        ESP_LOGE(TAG, "esp_ble_gap_register_callback failed: %d", err);
        panic(PANIC_ID_GAP_REGISTER_FAILED);
    }

    err = esp_ble_gatts_register_callback(gatts_event_handler);
    if (err)
    {
        ESP_LOGE(TAG, "esp_ble_gatts_register_callback failed: %d", err);
        panic(PANIC_ID_GATTS_REGISTER_FAILED);
    }

    err = esp_ble_gatts_app_register(0);
    if (err)
    {
        ESP_LOGE(TAG, "esp_ble_gatts_app_register failed: %d", err);
        panic(PANIC_ID_GATTS_APP_REGISTER_FAILED);
    }

    err = esp_ble_gatt_set_local_mtu(500);
    if (err)
    {
        ESP_LOGE(TAG, "esp_ble_gatt_set_local_mtu failed: %d", err);
        panic(PANIC_ID_SET_LOCAL_MTU_FAILED);
    }

    ESP_LOGI(TAG, "gattcomm_init success");
}

void gattcomm_disconnect(void)
{
    if (ctx.conn_id != CONN_ID_INVALID)
    {
        esp_ble_gatts_close(ctx.gatts_if, ctx.conn_id);
    }
}

void gattcomm_tx(const uint8_t *data, uint16_t length)
{
    if (ctx.conn_id != CONN_ID_INVALID && ctx.notify_enabled)
    {
        esp_err_t err = esp_ble_gatts_send_indicate(ctx.gatts_if,
                                                    ctx.conn_id,
                                                    ctx.char_handle,
                                                    length,
                                                    (uint8_t *)data,
                                                    false);
        if (err)
        {
            ESP_LOGW(TAG, "esp_ble_gatts_send_indicate failed: %d", err);
            gattcomm_disconnect();
        }
    }
}
