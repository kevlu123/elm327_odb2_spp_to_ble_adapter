#include "ledmgr.h"

#include <stdbool.h>

#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

#define TAG             "LEDMGR"
#define LEDC_GPIO       2
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_BRIGHTNESS 32

static const ledc_timer_config_t LEDC_TIMER_CONFIG = {
    .speed_mode = LEDC_MODE,
    .duty_resolution = LEDC_TIMER_10_BIT,
    .timer_num = LEDC_TIMER,
    .freq_hz = 1000,
};

static const ledc_channel_config_t LEDC_CHANNEL_CONFIG = {
    .gpio_num = LEDC_GPIO,
    .speed_mode = LEDC_MODE,
    .channel = LEDC_CHANNEL,
    .timer_sel = LEDC_TIMER,
    .duty = 0,
};

typedef enum
{
    // LED off
    LEDMGR_STATE_DISCONNECTED,
    // LED breathing
    LEDMGR_STATE_SPP_CONNECTING,
    // LED on
    LEDMGR_STATE_CONNECTED,
    // LED quick blink
    LEDMGR_STATE_ACTIVITY,
    // LED morse/binary blink.
    // LSB first, 0 = short blink, 1 = long blink.
    LEDMGR_STATE_PANIC,
} ledmgr_state_t;

static struct
{
    QueueHandle_t msg_queue;
    TickType_t queue_wait_time;
    ledmgr_state_t state;
    ledmgr_state_t prev_state;
    uint32_t led_level;

    int connecting_breath_seq;

    volatile TickType_t activity_timestamp;

    panic_id_t panic_id;
    bool panic_blink_paused;
    int panic_blink_bit_index;
} ctx;

_Static_assert(sizeof(TickType_t) == 4, "sizeof(TickType_t) == 4 required for atomicity");

static void set_led_level(uint32_t level)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, level);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    ctx.led_level = level;
}

static void toggle_led_level(void)
{
    set_led_level(ctx.led_level > 0 ? 0 : LEDC_BRIGHTNESS);
}

static bool try_change_state(ledmgr_state_t new_state, TickType_t waited_time)
{
    bool allow_state_change = ctx.state != new_state
                           && ctx.state != LEDMGR_STATE_PANIC;
    if (allow_state_change)
    {
        ctx.prev_state = ctx.state;
        ctx.state = new_state;
        return true;
    }

    if (ctx.queue_wait_time != portMAX_DELAY)
    {
        if (waited_time < ctx.queue_wait_time)
        {
            ctx.queue_wait_time -= waited_time;
        }
        else
        {
            ctx.queue_wait_time = 0;
        }
    }
    return false;
}

static void handle_disconnected(void)
{
    ctx.queue_wait_time = portMAX_DELAY;
    set_led_level(0);
}

static void handle_spp_connecting(bool state_changed)
{
    ctx.queue_wait_time = pdMS_TO_TICKS(50);

    if (state_changed)
    {
        ctx.connecting_breath_seq = 0;
    }

    uint32_t level = ctx.connecting_breath_seq < 10
        ? ctx.connecting_breath_seq
        : 19 - ctx.connecting_breath_seq;
    set_led_level(level);
    ctx.connecting_breath_seq = (ctx.connecting_breath_seq + 1) % 20;
}

static void handle_connected(void)
{
    ctx.queue_wait_time = portMAX_DELAY;
    set_led_level(LEDC_BRIGHTNESS);
}

static void handle_activity(bool state_changed)
{
    ctx.queue_wait_time = pdMS_TO_TICKS(50);
    if (state_changed)
    {
        set_led_level(0);
    }
    else if (xTaskGetTickCount() - ctx.activity_timestamp > pdMS_TO_TICKS(100))
    {
        xQueueSend(ctx.msg_queue, &ctx.prev_state, portMAX_DELAY);
    }
    else
    {
        toggle_led_level();
    }
}

static void handle_panic(bool state_changed)
{
    if (state_changed)
    {
        ctx.panic_blink_paused = true;
        ctx.panic_blink_bit_index = 0;
        set_led_level(0);
    }

    if (ctx.led_level > 0)
    {
        set_led_level(0);
        ctx.panic_blink_bit_index++;
        if ((ctx.panic_id & (0xFFFFFFFF << ctx.panic_blink_bit_index)) == 0)
        {
            ctx.panic_blink_paused = true;
            ctx.panic_blink_bit_index = 0;
            ctx.queue_wait_time = pdMS_TO_TICKS(3000);
        }
        else
        {
            ctx.queue_wait_time = pdMS_TO_TICKS(250);
        }
    }
    else
    {
        ctx.panic_blink_paused = false;
        set_led_level(LEDC_BRIGHTNESS);
        bool long_blink = (ctx.panic_id >> ctx.panic_blink_bit_index) & 1;
        ctx.queue_wait_time = pdMS_TO_TICKS(long_blink ? 500 : 250);
    }
}

static void ledmgr_thread(void *arg)
{
    while (1)
    {
        ledmgr_state_t new_state;
        TickType_t wait_begin_time = xTaskGetTickCount();
        bool state_changed = xQueueReceive(ctx.msg_queue,
                                           &new_state,
                                           ctx.queue_wait_time) == pdPASS;
        TickType_t waited_time = xTaskGetTickCount() - wait_begin_time;

        if (state_changed && !try_change_state(new_state, waited_time))
        {
            continue;
        }

        switch (ctx.state)
        {
        case LEDMGR_STATE_DISCONNECTED:
            handle_disconnected();
            break;
        case LEDMGR_STATE_SPP_CONNECTING:
            handle_spp_connecting(state_changed);
            break;
        case LEDMGR_STATE_CONNECTED:
            handle_connected();
            break;
        case LEDMGR_STATE_ACTIVITY:
            handle_activity(state_changed);
            break;
        case LEDMGR_STATE_PANIC:
            handle_panic(state_changed);
            break;
        }
    }
}

void ledmgr_init(void)
{
    esp_err_t err;
    
    ctx.queue_wait_time = portMAX_DELAY;
    
    err = ledc_timer_config(&LEDC_TIMER_CONFIG);
    if (err)
    {
        ESP_LOGE(TAG, "ledc_timer_config failed: %d", err);
        panic(PANIC_ID_LEDMGR_LEDC_TIMER_CONFIG_FAILED);
    }

    err = ledc_channel_config(&LEDC_CHANNEL_CONFIG);
    if (err)
    {
        ESP_LOGE(TAG, "ledc_channel_config failed: %d", err);
        panic(PANIC_ID_LEDMGR_LEDC_CHANNEL_CONFIG_FAILED);
    }

    set_led_level(0);

    ctx.msg_queue = xQueueCreate(64, sizeof(ledmgr_state_t));
    if (ctx.msg_queue == NULL)
    {
        ESP_LOGE(TAG, "xQueueCreate failed");
        panic(PANIC_ID_LEDMGR_CREATE_QUEUE_FAILED);
    }

    BaseType_t ret = xTaskCreate(ledmgr_thread,
                                 TAG,
                                 4096,
                                 NULL,
                                 5,
                                 NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate failed: %d", (int)ret);
        panic(PANIC_ID_LEDMGR_TASK_CREATE_FAILED);
    }
}

void ledmgr_on_disconnected(void)
{
    ledmgr_state_t new_state = LEDMGR_STATE_DISCONNECTED;
    xQueueSend(ctx.msg_queue, &new_state, portMAX_DELAY);
}

void ledmgr_on_connecting(void)
{
    ledmgr_state_t new_state = LEDMGR_STATE_SPP_CONNECTING;
    xQueueSend(ctx.msg_queue, &new_state, portMAX_DELAY);
}

void ledmgr_on_connected(void)
{
    ledmgr_state_t new_state = LEDMGR_STATE_CONNECTED;
    xQueueSend(ctx.msg_queue, &new_state, portMAX_DELAY);
}

void ledmgr_on_activity(void)
{
    ctx.activity_timestamp = xTaskGetTickCount();
    ledmgr_state_t new_state = LEDMGR_STATE_ACTIVITY;
    xQueueSend(ctx.msg_queue, &new_state, portMAX_DELAY);
}

void ledmgr_on_panic(panic_id_t id)
{
    ctx.panic_id = id;
    ledmgr_state_t new_state = LEDMGR_STATE_PANIC;
    xQueueSend(ctx.msg_queue, &new_state, portMAX_DELAY);
}
