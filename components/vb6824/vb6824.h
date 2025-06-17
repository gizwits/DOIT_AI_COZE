#ifndef __VB6824_H__
#define __VB6824_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "driver/gpio.h"

typedef void (*vb_voice_command_cb_t)(char *command, uint16_t len, void *arg);

void vb6824_register_voice_command_cb(vb_voice_command_cb_t cb, void *arg);

void vb6824_audio_enable_input(bool enable);
void vb6824_audio_enable_output(bool enable);

void vb6824_audio_set_output_volume(uint8_t volume);

void vb6824_audio_write(uint8_t *data, uint16_t len);
uint16_t vb6824_audio_read(uint8_t *data, uint16_t size);
void vb6824_shutdown(void);

void vb6824_init(gpio_num_t tx, gpio_num_t rx);

#ifdef __cplusplus
}
#endif

#endif //__VB6824_H__