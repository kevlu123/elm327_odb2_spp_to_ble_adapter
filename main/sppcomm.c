#include "sppcomm.h"
#include "app.h"

#include <esp_bt.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <esp_log.h>

#define TAG "SPPCOMM"

#define INQUIRY_TIMEOUT_SECS       15
#define SEARCH_NAME                "V-LINK"
static esp_bt_pin_code_t PINCODE = "1234";

static struct
{
    uint8_t peer_bd_addr[6];
    uint32_t conn_handle;
} ctx;

#define CONN_HANDLE_INVALID 0xFFFFFFFF

static void start_scan(void)
{
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                               INQUIRY_TIMEOUT_SECS * 100 / 128,
                                               0);
    if (err)
    {
        ESP_LOGE(TAG, "esp_bt_gap_start_discovery failed: %d", err);
        panic(0);
    }
}

static bool is_eir_match(uint8_t *eir, int eir_len)
{
    if (eir == NULL)
    {
        return false;
    }

    uint8_t len;
    uint8_t *name = esp_bt_gap_resolve_eir_data(eir,
                                                ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,
                                                &len);
    if (name == NULL)
    {
        name = esp_bt_gap_resolve_eir_data(eir,
                                           ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME,
                                           &len);
    }
    if (name == NULL)
    {
        return false;
    }

    if (len > ESP_BT_GAP_MAX_BDNAME_LEN)
    {
        len = ESP_BT_GAP_MAX_BDNAME_LEN;
    }

    return len == sizeof(SEARCH_NAME) - 1
        && memcmp((const char *)name, SEARCH_NAME, len) == 0;
}

static bool is_device_found(void)
{
    return memcmp(ctx.peer_bd_addr, "\0\0\0\0\0\0", sizeof(ctx.peer_bd_addr)) != 0;
}

static void gap_event_handler(esp_bt_gap_cb_event_t event,
                              esp_bt_gap_cb_param_t *param)
{
    esp_err_t err;

    switch(event)
    {
    case ESP_BT_GAP_DISC_RES_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_DISC_RES_EVT: %02x:%02x:%02x:%02x:%02x:%02x",
                 param->disc_res.bda[0],
                 param->disc_res.bda[1],
                 param->disc_res.bda[2],
                 param->disc_res.bda[3],
                 param->disc_res.bda[4],
                 param->disc_res.bda[5]);
        for (int i = 0; i < param->disc_res.num_prop; i++)
        {
            if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR
                && is_eir_match(param->disc_res.prop[i].val, param->disc_res.prop[i].len))
            {
                ESP_LOGI(TAG, "Found target device");
                memcpy(ctx.peer_bd_addr, param->disc_res.bda, sizeof(ctx.peer_bd_addr));
                esp_bt_gap_cancel_discovery();
                err = esp_spp_start_discovery(ctx.peer_bd_addr);
                if (err)
                {
                    ESP_LOGW(TAG, "esp_spp_start_discovery failed: %d", err);
                    app_on_spp_connect_error();
                    sppcomm_disconnect();
                }
                break;
            }
        }
        break;

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_DISC_STATE_CHANGED_EVT: state=%d",
                 param->disc_st_chg.state);
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED
            && !is_device_found())
        {
            ESP_LOGI(TAG, "Device not found");
            app_on_spp_connect_error();
            sppcomm_disconnect();
        }
        break;

    default:
        break;
    }
}

static void spp_event_handler(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    esp_err_t err;

    switch (event)
    {
    case ESP_SPP_INIT_EVT:
        ESP_LOGI(TAG, "ESP_SPP_INIT_EVT");
        if (param->init.status != ESP_SPP_SUCCESS)
        {
            ESP_LOGE(TAG, "ESP_SPP_INIT_EVT failed: %d", param->init.status);
            panic(0);
        }

        err = esp_bt_gap_set_device_name(BT_DEVICE_NAME);
        if (err)
        {
            ESP_LOGE(TAG, "esp_bt_gap_set_device_name failed: %d", err);
            panic(0);
        }

        err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                       ESP_BT_GENERAL_DISCOVERABLE);
        if (err)
        {
            ESP_LOGE(TAG, "esp_bt_gap_set_scan_mode failed: %d", err);
            panic(0);
        }
        break;

    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(TAG, "ESP_SPP_DISCOVERY_COMP_EVT");
        if (param->disc_comp.status != ESP_SPP_SUCCESS)
        {
            ESP_LOGW(TAG, "ESP_SPP_DISCOVERY_COMP_EVT failed: %d",
                     param->disc_comp.status);
            app_on_spp_connect_error();
            sppcomm_disconnect();
            break;
        }

        err = esp_spp_connect(ESP_SPP_SEC_NONE,
                              ESP_SPP_ROLE_MASTER,
                              param->disc_comp.scn[0],
                              ctx.peer_bd_addr);
        if (err)
        {
            ESP_LOGW(TAG, "esp_spp_connect failed: %d", err);
            app_on_spp_connect_error();
            sppcomm_disconnect();
        }
        break;

    case ESP_SPP_OPEN_EVT:
        ESP_LOGI(TAG, "========== ESP_SPP_OPEN_EVT ==========");
        if (param->open.status != ESP_SPP_SUCCESS)
        {
            ESP_LOGE(TAG, "ESP_SPP_OPEN_EVT: %d", param->open.status);
            app_on_spp_connect_error();
            sppcomm_disconnect();
            break;
        }

        ctx.conn_handle = param->open.handle;
        app_on_spp_connected();
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "~~~~~~~~~~ ESP_SPP_CLOSE_EVT ~~~~~~~~~~");
        ctx.conn_handle = CONN_HANDLE_INVALID;
        memset(ctx.peer_bd_addr, 0, sizeof(ctx.peer_bd_addr));
        app_on_spp_disconnected();
        break;

    case ESP_SPP_WRITE_EVT:
        ESP_LOGD(TAG, "ESP_SPP_WRITE_EVT");
        if (param->write.status != ESP_SPP_SUCCESS)
        {
            ESP_LOGE(TAG, "ESP_SPP_WRITE_EVT: %d", param->write.status);
            sppcomm_disconnect();
        }
        break;

    case ESP_SPP_DATA_IND_EVT:
        ESP_LOGD(TAG, "ESP_SPP_DATA_IND_EVT");
        app_on_spp_rx(param->data_ind.data, param->data_ind.len);
        break;

    default:
        break;
    }
}

void sppcomm_init(void)
{
    esp_err_t err;

    ctx.conn_handle = CONN_HANDLE_INVALID;

    err = esp_bt_gap_register_callback(gap_event_handler);
    if (err)
    {
        ESP_LOGE(TAG, "esp_bt_gap_register_callback failed: %d", err);
        panic(0);
    }

    err = esp_spp_register_callback(spp_event_handler);
    if (err)
    {
        ESP_LOGE(TAG, "esp_spp_register_callback failed: %d", err);
        panic(0);
    }

    const esp_spp_cfg_t bt_spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
    };
    err = esp_spp_enhanced_init(&bt_spp_cfg);
    if (err)
    {
        ESP_LOGE(TAG, "esp_spp_enhanced_init failed: %d", err);
        panic(0);
    }

    err = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED,
                             strlen((char *)PINCODE),
                             PINCODE);
    if (err)
    {
        ESP_LOGE(TAG, "esp_bt_gap_set_pin failed: %d", err);
        panic(0);
    }
}

void sppcomm_connect(void)
{
    start_scan();
}

void sppcomm_disconnect(void)
{
    if (ctx.conn_handle != CONN_HANDLE_INVALID)
    {
        esp_spp_disconnect(ctx.conn_handle);
        ctx.conn_handle = CONN_HANDLE_INVALID;
        memset(ctx.peer_bd_addr, 0, sizeof(ctx.peer_bd_addr));
    }
}

void sppcomm_tx(const uint8_t *data, uint16_t length)
{
    esp_err_t err = esp_spp_write(ctx.conn_handle, length, (uint8_t *)data);
    if (err)
    {
        ESP_LOGE(TAG, "esp_spp_write failed: %d", err);
        panic(0);
    }
}
