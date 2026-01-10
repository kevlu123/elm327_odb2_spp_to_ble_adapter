#pragma once
#include <stdint.h>

typedef enum
{
    PANIC_ID_GAP_REGISTER_FAILED = 1,
    PANIC_ID_GATTS_REGISTER_FAILED,
    PANIC_ID_GATTS_APP_REGISTER_FAILED,
    PANIC_ID_SET_LOCAL_MTU_FAILED,
    PANIC_ID_ADV_START_FAILED,
    PANIC_ID_ADV_START_FAILED2,
    PANIC_ID_GATTS_REG_EVT_FAILED,
    PANIC_ID_SET_DEVICE_NAME_FAILED,
    PANIC_ID_CONFIG_ADV_DATA_FAILED,
    PANIC_ID_CONFIG_SCAN_RSP_DATA_FAILED,
    PANIC_ID_CREATE_SERVICE_FAILED,
    PANIC_ID_START_SERVICE_FAILED,
    PANIC_ID_ADD_CHAR_FAILED,
} panic_id_t;

__attribute__((noreturn)) void panic(panic_id_t id);

void app_on_gatt_connected(void);
void app_on_gatt_disconnected(void);
void app_on_gatt_rx(const uint8_t *data, uint16_t length);

void app_on_spp_connected(void);
void app_on_spp_connect_error(void);
void app_on_spp_disconnected(void);
void app_on_spp_rx(const uint8_t *data, uint16_t length);
