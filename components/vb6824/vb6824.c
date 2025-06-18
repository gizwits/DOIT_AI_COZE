#include "vb6824.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "freertos/ringbuf.h"
#include <esp_timer.h>

static const char *TAG = "vb6824";

#define UART_NUM                UART_NUM_1

#if defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS)
#define AUDIO_RECV_CHENK_LEN     40
#define AUDIO_SEND_CHENK_LEN     40
#define AUDIO_SEND_CHENK_MS      20
#define SEND_BUF_LENGTH          AUDIO_SEND_CHENK_LEN*10
#define RECV_BUF_LENGTH          AUDIO_RECV_CHENK_LEN*10
#elif defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS_PCM_16K)
#define AUDIO_RECV_CHENK_LEN     40
#define AUDIO_SEND_CHENK_LEN     320
#define AUDIO_SEND_CHENK_MS      10
#define SEND_BUF_LENGTH          1920*3
#define RECV_BUF_LENGTH          AUDIO_RECV_CHENK_LEN*10
#else
#define AUDIO_RECV_CHENK_LEN     512
#define AUDIO_SEND_CHENK_LEN     320
#define AUDIO_SEND_CHENK_MS      10
#define SEND_BUF_LENGTH          1920*3
#define RECV_BUF_LENGTH          1920*3
#endif

#define UART_QUEUE_SIZE         16
#define UART_RX_BUFFER_SIZE     AUDIO_SEND_CHENK_LEN*10
#define UART_TX_BUFFER_SIZE     AUDIO_SEND_CHENK_LEN*10

#define FRAME_MIN_LIN     (7)
#define FRAME_MAX_LIN     (512+7)

typedef struct{
    uint16_t head;
    uint16_t len;
    uint16_t cmd;
}__attribute__ ((packed))frame_head_t;

typedef struct{
    uint8_t sum;
}__attribute__ ((packed))frame_last_t;

#define SWAP_16(x)          ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define SUM8(bytes, size)   (__sum_bytes((uint8_t*)bytes, (uint16_t)size)%256)

#define FRAME_HEAD          (SWAP_16(0x55AA))
#define FRAME_DATA_LEN(h)   (SWAP_16(h->len))
#define FRAME_CHECK(h, l)   (SUM8(h, sizeof(frame_head_t) + SWAP_16(h->len)) == l->sum)

typedef struct{
    uint16_t head;
    uint16_t len;
    uint16_t cmd;
    uint8_t data[0];
}__attribute__ ((packed))vb6824_frame_t;

typedef enum
{
    VB6824_CMD_RECV_PCM = 0x2080,
    VB6824_CMD_RECV_CTL = 0x0180,
    VB6824_CMD_SEND_PCM = 0x2081,
    VB6824_CMD_SEND_PCM_EOF = 0x0201,
    VB6824_CMD_SEND_CTL = 0x0202,
    VB6824_CMD_SEND_VOLUM = 0x0203,
    VB6824_CMD_SEND_SHUTDOWN = 0x0208,
}vb6824_cmd_t;

static QueueHandle_t g_uart_queue = NULL;
static RingbufHandle_t g_rx_ringbuffer = NULL;
static RingbufHandle_t g_tx_ringbuffer = NULL;

static void *g_voice_command_cb_arg = NULL;
static vb_voice_command_cb_t g_voice_command_cb = NULL;

static bool g_input_enabled = false;
static bool g_output_enabled = false;

void __vb6824_frame_cb(uint8_t *frame, uint16_t frame_len);

static inline int __sum_bytes(const uint8_t* bytes, uint16_t size) {
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += bytes[i];
    }
    return sum;
}

void __frame_parse_data(uint8_t *data, uint16_t len){
    uint16_t parse_len = 0;
    uint8_t *parse_data = NULL;

    static uint16_t tmp_len = {0};
    static uint8_t tmp_data[(FRAME_MAX_LIN) * 2] = {0};

    if(tmp_len == 0 && parse_len >= FRAME_MIN_LIN){
        parse_data = data;
        parse_len = len;
    }else{
        memcpy(tmp_data + tmp_len, data, len);
        tmp_len = tmp_len + len;
        parse_data = tmp_data;
        parse_len = tmp_len;
    }

re_parse:      
    if(parse_len < FRAME_MIN_LIN){
        return;
    }

    for (size_t i = 0; i < parse_len; i++){
        uint16_t left_len = parse_len - i;
        if(left_len >= FRAME_MIN_LIN){
            frame_head_t *head = (frame_head_t *)&parse_data[i];
            if(head->head == FRAME_HEAD){
                frame_last_t *last = (frame_last_t *)&parse_data[i + sizeof(frame_head_t) + FRAME_DATA_LEN(head)];
                uint16_t fr_len = sizeof(frame_head_t) + FRAME_DATA_LEN(head) + sizeof(frame_last_t);
                if(left_len >= fr_len){
                    uint16_t sum = 0;
                    uint8_t *p = (uint8_t *)head;
                    for (size_t j = 0; j < sizeof(frame_head_t) + FRAME_DATA_LEN(head); j++){
                        sum += p[j];
                    }
                    if(FRAME_CHECK(head, last)){
                        __vb6824_frame_cb(&parse_data[i], fr_len);
                        memcpy(tmp_data, &parse_data[i + fr_len], left_len - fr_len);
                        tmp_len = left_len-fr_len;
                        if(tmp_len > FRAME_MIN_LIN){
                            parse_data = tmp_data;
                            parse_len = tmp_len;
                            goto re_parse;
                        }else{
                            return;
                        }
                    }
                }else if(fr_len <= FRAME_MAX_LIN){
                    if(i != 0){
                        memcpy(tmp_data, &parse_data[i], left_len);
                        tmp_len = left_len;
                    }
                    break;
                }
            }
        }else{
            memcpy(tmp_data, &parse_data[i], left_len);
            tmp_len = left_len;
        }
    }
}

void __frame_send(vb6824_cmd_t cmd, uint8_t *data, uint16_t len){
    uint16_t packet_len = 0;
    uint8_t packet[AUDIO_SEND_CHENK_LEN + 7] = {0};

    int16_t idx = 0;
    uint16_t send_len = len;

    while (idx < len)
    {
        memset(packet, 0, sizeof(packet));
        vb6824_frame_t *frame = (vb6824_frame_t *)packet;
        frame->head = FRAME_HEAD;
        frame->len = SWAP_16(send_len);
        frame->cmd = SWAP_16(cmd);
        
        if (data != NULL) {
            memcpy(frame->data, data + idx, (send_len>(len-idx))?(len-idx):send_len);
        }
        idx += send_len;
        packet_len = 6 + send_len + 1;
        uint8_t checksum = 0;
        for (size_t i = 0; i < packet_len - 1; i++) {
            checksum += packet[i];
        }
        packet[packet_len - 1] = checksum;
        // ESP_LOGW(TAG, "write_bytes: %d", packet_len);
        uart_write_bytes(UART_NUM, packet, packet_len);
    }
}

void __uart_task(void *arg) {
    uart_event_t event;
    uint8_t temp_buf[1024];  // 中间缓冲
    while (true) {
        // 等待队列事件
        if (xQueueReceive(g_uart_queue, &event, pdMS_TO_TICKS(10))) {
            switch (event.type) {
                case UART_DATA:{
                        // 尝试多次读取，直到本次事件里可读字节消耗完为止
                        int len = uart_read_bytes(UART_NUM, temp_buf, sizeof(temp_buf), 0);
                        if (len > 0) {
                            __frame_parse_data(temp_buf, len);
                        }
                    }
                    break;
                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "HW FIFO overflow, flushing UART.");
                    uart_flush_input(UART_NUM);
                    xQueueReset(g_uart_queue);
                    break;
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "Ring buffer full, flushing UART.");
                    uart_flush_input(UART_NUM);
                    xQueueReset(g_uart_queue);
                    break;
                default:
                    // ESP_LOGI(TAG, "UART event: %d", event.type);
                    break;
            }
        }
    }
    vTaskDelete(NULL);
}

void __uart_init(gpio_num_t tx, gpio_num_t rx){
    const uart_config_t uart_config = {
        .baud_rate = 2000000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        // .source_clk = UART_SCLK_DEFAULT,
    };

    int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif
    uart_driver_install(UART_NUM, UART_RX_BUFFER_SIZE, UART_TX_BUFFER_SIZE, UART_QUEUE_SIZE, &g_uart_queue, intr_alloc_flags);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#ifdef CONFIG_IDF_TARGET_ESP32C2
    xTaskCreate(__uart_task, "__uart_task", 1024 * 3, NULL, 9, NULL);
    // xTaskCreate(__uart_task, "__uart_task", 1024 * 2 + 256, NULL, 9, NULL);
#else
    xTaskCreate(__uart_task, "__uart_task", 1024 * 4, NULL, 9, NULL);
#endif
}

// void __send_timer_cb(void* arg){
//     size_t item_size = 0;
// #if defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS)
//     uint8_t *item = (uint8_t *)xRingbufferReceive(g_tx_ringbuffer, &item_size, 0);
// #else
//     uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(g_tx_ringbuffer, &item_size, 0, AUDIO_SEND_CHENK_LEN);
// #endif
//     if (item != NULL) {
//         __frame_send(VB6824_CMD_SEND_PCM, (uint8_t *)item, item_size);
//         vRingbufferReturnItem(g_tx_ringbuffer, (void *)item);
//     }
// }

void __send_task(void *arg) {
    TickType_t last_time = xTaskGetTickCount();
    while (1)
    {
        if(g_output_enabled){
            size_t item_size = 0;
#if defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS)
            uint8_t *item = (uint8_t *)xRingbufferReceive(g_tx_ringbuffer, &item_size, portMAX_DELAY);
#else
            uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(g_tx_ringbuffer, &item_size, portMAX_DELAY, AUDIO_SEND_CHENK_LEN);
#endif
            if (item != NULL) {
                TickType_t now_time = xTaskGetTickCount();
                if((now_time - last_time) >= pdMS_TO_TICKS(AUDIO_SEND_CHENK_MS)){
                    last_time = xTaskGetTickCount();
                }
                __frame_send(VB6824_CMD_SEND_PCM, (uint8_t *)item, item_size);
                vRingbufferReturnItem(g_tx_ringbuffer, (void *)item);
                vTaskDelayUntil(&last_time, pdMS_TO_TICKS(AUDIO_SEND_CHENK_MS));
            }
        }else{
            vTaskDelay(10);
        }
    }
}

void __vb6824_frame_cb(uint8_t *data, uint16_t len){
    vb6824_frame_t *frame = (vb6824_frame_t *)data;
    frame->len = SWAP_16(frame->len);
    frame->cmd = SWAP_16(frame->cmd);
    frame->data[frame->len] = 0;
    // ESP_LOGE(TAG, "vb6824 recv cmd: %04x, len: %d :%.*s", frame->cmd, frame->len, frame->len, frame->data);

    switch (frame->cmd)
    {
    case VB6824_CMD_RECV_PCM:{
        if(g_input_enabled){
            while(xRingbufferGetCurFreeSize(g_rx_ringbuffer) < frame->len){
                size_t item_size = 0;
#if (defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS) || defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS_PCM_16K))
                uint8_t *item = (uint8_t *)xRingbufferReceive(g_rx_ringbuffer, &item_size, 0);
#else
                uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(g_rx_ringbuffer, &item_size, 0, size);
#endif
                if (item != NULL) {
                    vRingbufferReturnItem(g_rx_ringbuffer, (void *)item);
                }else{
                    break;
                }
            }
            xRingbufferSend(g_rx_ringbuffer, (void *)frame->data, frame->len, portMAX_DELAY);
        }
        break;
    } 
    case VB6824_CMD_RECV_CTL:{
        // ESP_LOGI(TAG, "vb6824 recv cmd: %04x, len: %d :%.*s", frame->cmd, frame->len, frame->len, frame->data);
        if(g_voice_command_cb){
            g_voice_command_cb((char *)frame->data, frame->len, g_voice_command_cb_arg);
        }
        break;
    }
    default:
        break;
    }
}

void vb6824_audio_enable_input(bool enable){
    if (enable == g_input_enabled) {
        return;
    }

    g_input_enabled = enable;

    if(g_input_enabled == false){
        while(1){
            size_t item_size = 0;
#if (defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS) || defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS_PCM_16K))
            uint8_t *item = (uint8_t *)xRingbufferReceive(g_rx_ringbuffer, &item_size, 0);
#else
            uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(g_rx_ringbuffer, &item_size, 0, size);
#endif
            if (item != NULL) {
                vRingbufferReturnItem(g_rx_ringbuffer, (void *)item);
            }else{
                break;
            }
        }
    }

}

void vb6824_audio_enable_output(bool enable){
    if (enable == g_output_enabled) {
        return;
    }

    g_output_enabled = enable;

    if(g_output_enabled == false){
        while(1){
            size_t item_size = 0;
#if defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS)
            uint8_t *item = (uint8_t *)xRingbufferReceive(g_tx_ringbuffer, &item_size, 0);
#else
            uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(g_tx_ringbuffer, &item_size, 0, AUDIO_SEND_CHENK_LEN);
#endif
            if (item != NULL) {
                vRingbufferReturnItem(g_rx_ringbuffer, (void *)item);
            }else{
                break;
            }
        }
    }
}

void vb6824_register_voice_command_cb(vb_voice_command_cb_t cb, void *arg){
    g_voice_command_cb = cb;
    g_voice_command_cb_arg = arg;
}

void vb6824_audio_set_output_volume(uint8_t volume){
    uint8_t vol = (uint8_t)((int)(volume * 31) / 100);
    __frame_send(VB6824_CMD_SEND_VOLUM, &vol, 1);
}

void vb6824_shutdown(void){
    __frame_send(VB6824_CMD_SEND_SHUTDOWN, NULL, 1);
}

void vb6824_audio_write(uint8_t *data, uint16_t len){
    xRingbufferSend(g_tx_ringbuffer, (void *)data, len, portMAX_DELAY);
}

uint16_t vb6824_audio_read(uint8_t *data, uint16_t size){
    size_t item_size = 0;
    size_t items_waiting = 0;
    vRingbufferGetInfo(g_rx_ringbuffer, NULL, NULL, NULL, NULL, &items_waiting);
#if (defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS) || defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS_PCM_16K))
    // if(items_waiting > 0){
        char *item = (char *)xRingbufferReceive(g_rx_ringbuffer, &item_size, portMAX_DELAY);
        if (item != NULL) {
            if(size >= item_size){
                memcpy(data, item, item_size);
            }else{
                ESP_LOGE(TAG, "size is too small");
                item_size = 0;
            }
            vRingbufferReturnItem(g_rx_ringbuffer, (void *)item);
        }
    // }
#else
    // if(items_waiting > size){
        char *item = (uint8_t *)xRingbufferReceiveUpTo(g_rx_ringbuffer, &item_size, portMAX_DELAY, size);
        if(item_size > 0){
            memcpy(data, item, item_size);
            vRingbufferReturnItem(g_rx_ringbuffer, (void *)item);
        }
    // }
#endif
    return item_size;
}

void vb6824_init(gpio_num_t tx, gpio_num_t rx){
    __uart_init(tx, rx);

#if defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS)
    g_rx_ringbuffer = xRingbufferCreate(RECV_BUF_LENGTH, RINGBUF_TYPE_NOSPLIT);
    g_tx_ringbuffer = xRingbufferCreate(SEND_BUF_LENGTH, RINGBUF_TYPE_NOSPLIT);
#elif defined(CONFIG_VB6824_TYPE_OPUS_16K_20MS_PCM_16K)
    g_rx_ringbuffer = xRingbufferCreate(RECV_BUF_LENGTH, RINGBUF_TYPE_NOSPLIT);
    g_tx_ringbuffer = xRingbufferCreate(SEND_BUF_LENGTH, RINGBUF_TYPE_BYTEBUF);
#else
    g_rx_ringbuffer = xRingbufferCreate(RECV_BUF_LENGTH, RINGBUF_TYPE_BYTEBUF);
    g_tx_ringbuffer = xRingbufferCreate(SEND_BUF_LENGTH, RINGBUF_TYPE_BYTEBUF);
#endif

    xTaskCreate(__send_task, "__send_task", 2048, NULL, 9, NULL);
    // xTaskCreate(__send_task, "__send_task", 1024, NULL, 9, NULL);

    // esp_timer_handle_t send_timer = NULL;
    // esp_timer_create_args_t timer_args = {
    //     .callback = __send_timer_cb,
    //     .dispatch_method = ESP_TIMER_TASK,
    //     .name = "vb_send",
    //     .skip_unhandled_events = true,
    // };
    // esp_timer_create(&timer_args, &send_timer);
    // if(send_timer){
    //     esp_timer_start_periodic(send_timer, AUDIO_SEND_CHENK_MS*1000);
    // }else{
    //     ESP_LOGE(TAG, "send_timer is null");
    // }
}