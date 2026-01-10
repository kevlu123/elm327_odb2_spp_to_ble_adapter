#include "app.h"
#include "gattcomm.h"
#include "sppcomm.h"
#include <string.h>
#include "freertos/FreeRTOS.h"

typedef enum
{
    STATE_DISCONNECTED,
    STATE_GATT_CONNECTED,
    STATE_GATT_SPP_CONNECTED,
} app_state_t;

struct
{
    app_state_t state;
    uint8_t initial_spp_tx_buffer[512];
    uint16_t initial_spp_tx_buffer_len;
} ctx;

static void set_state(app_state_t new_state)
{
    ctx.state = new_state;
}

void panic(panic_id_t id)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_on_gatt_connected(void)
{
    switch (ctx.state)
    {
    case STATE_DISCONNECTED:
        set_state(STATE_GATT_CONNECTED);
        sppcomm_connect();
        break;
    case STATE_GATT_CONNECTED:
    case STATE_GATT_SPP_CONNECTED:
        ctx.initial_spp_tx_buffer_len = 0;
        break;
    }
}

void app_on_gatt_disconnected(void)
{
    set_state(STATE_DISCONNECTED);
    sppcomm_disconnect();
}

void app_on_gatt_rx(const uint8_t *data, uint16_t length)
{
    switch (ctx.state)
    {
    case STATE_DISCONNECTED:
        break;
    case STATE_GATT_CONNECTED:
        if (ctx.initial_spp_tx_buffer_len + length > sizeof(ctx.initial_spp_tx_buffer))
        {
            sppcomm_disconnect();
            gattcomm_disconnect();
            set_state(STATE_DISCONNECTED);
        }
        else
        {
            memcpy(ctx.initial_spp_tx_buffer + ctx.initial_spp_tx_buffer_len,
                   data,
                   length);
            ctx.initial_spp_tx_buffer_len += length;
        }
        break;
    case STATE_GATT_SPP_CONNECTED:
        sppcomm_tx(data, length);
        break;
    }
}

void app_on_spp_connected(void)
{
    switch (ctx.state)
    {
    case STATE_DISCONNECTED:
        sppcomm_disconnect();
        break;
    case STATE_GATT_CONNECTED:
        set_state(STATE_GATT_SPP_CONNECTED);
        if (ctx.initial_spp_tx_buffer_len > 0)
        {
            sppcomm_tx(ctx.initial_spp_tx_buffer,
                       ctx.initial_spp_tx_buffer_len);
            ctx.initial_spp_tx_buffer_len = 0;
        }
        break;
    case STATE_GATT_SPP_CONNECTED:
        break;
    }
}

void app_on_spp_connect_error(void)
{
    switch (ctx.state)
    {
    case STATE_DISCONNECTED:
        break;
    case STATE_GATT_CONNECTED:
    case STATE_GATT_SPP_CONNECTED:
        set_state(STATE_DISCONNECTED);
        gattcomm_disconnect();
        break;
    }
}

void app_on_spp_disconnected(void)
{
    switch (ctx.state)
    {
    case STATE_DISCONNECTED:
        break;
    case STATE_GATT_CONNECTED:
    case STATE_GATT_SPP_CONNECTED:
        set_state(STATE_DISCONNECTED);
        gattcomm_disconnect();
        break;
    }
}

void app_on_spp_rx(const uint8_t *data, uint16_t length)
{
    if (ctx.state == STATE_GATT_SPP_CONNECTED)
    {
        gattcomm_tx(data, length);
    }
}
