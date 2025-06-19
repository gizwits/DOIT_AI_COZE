#include "vb6824_audio_codec.h"

#include <cstring>
#include <cmath>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "settings.h"
#include <esp_timer.h>

#include "esp_log.h"

#include "vb6824.h"

static const char *TAG = "VbAduioCodec";

#define VB_PLAY_SAMPLE_RATE     16 * 1000
#define VB_RECO_SAMPLE_RATE     16 * 1000

void VbAduioCodec::WakeUp(std::string command) {
    if (on_wake_up_) {
        ESP_LOGE(TAG, "on_wake_up_");
        on_wake_up_(command);
    }
}

void VbAduioCodec::OnWakeUp(std::function<void(std::string)> callback) {
    on_wake_up_ = callback;
}

VbAduioCodec::VbAduioCodec(gpio_num_t tx, gpio_num_t rx) {

    input_sample_rate_ = VB_RECO_SAMPLE_RATE;
    output_sample_rate_ = VB_RECO_SAMPLE_RATE;

    vb6824_init(tx, rx);

    vb6824_register_voice_command_cb([](char *command, uint16_t len, void *arg){
        auto this_ = (VbAduioCodec*)arg;
        ESP_LOGE(TAG, "vb6824 recv cmd");
        this_->WakeUp(command);
    }, this);
}

void VbAduioCodec::Start() {
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);

    EnableInput(true);
    EnableOutput(true);
}

#ifdef CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS
bool VbAduioCodec::InputData(std::vector<uint8_t>& opus) {
    opus.resize(40);
    int samples = Read((uint8_t *)opus.data(), opus.size());
    if (samples > 0) {
        return true;
    }
    return false;
}
#endif

void VbAduioCodec::SetOutputVolume(int volume){
    vb6824_audio_set_output_volume(volume);
    AudioCodec::SetOutputVolume(volume);
}

void VbAduioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    vb6824_audio_enable_input(enable);
    input_enabled_ = enable;
    ESP_LOGI(TAG, "Set input enable to %s", enable ? "true" : "false");
}

void VbAduioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    vb6824_audio_enable_output(enable);
    output_enabled_ = enable;
    ESP_LOGI(TAG, "Set output enable to %s", enable ? "true" : "false");
}

int VbAduioCodec::Read(int16_t* dest, int samples) {
    int read_len = vb6824_audio_read((uint8_t *)dest, 2 * samples);
    return read_len / 2;
}

#ifdef CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS
int VbAduioCodec::Read(uint8_t* dest, int samples) {
    int read_len = vb6824_audio_read((uint8_t *)dest, samples);
    return read_len;
}
#endif

int VbAduioCodec::Write(const int16_t* data, int samples) {
    if(frist_volume_is_set == false){
        frist_volume_is_set = true;
        SetOutputVolume(output_volume_);
    }
    vb6824_audio_write((uint8_t *)data, 2 * samples);
    return samples;
}

#ifdef CONFIG_USE_AUDIO_CODEC_DECODE_OPUS
int VbAduioCodec::Write(uint8_t* opus, int samples) {
    if(frist_volume_is_set == false){
        frist_volume_is_set = true;
        SetOutputVolume(output_volume_);
    }
    vb6824_audio_write((uint8_t *)data, samples);
    return samples;
}

bool ConfigDecode(int sample_rate, int channels, int duration_ms) {
    input_sample_rate_ = sample_rate;
    input_channels_ = channels;
    duration_ms_ = duration_ms;
    return true;
}
#endif
