#include "protocol.h"

#include <esp_log.h>

#define TAG "Protocol"
#include <esp_random.h>

void Protocol::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    on_incoming_json_ = callback;
}

void Protocol::OnIncomingAudio(std::function<void(std::vector<uint8_t>&& data)> callback) {
    on_incoming_audio_ = callback;
}

void Protocol::OnAudioChannelOpened(std::function<void()> callback) {
    on_audio_channel_opened_ = callback;
}

void Protocol::OnAudioChannelClosed(std::function<void()> callback) {
    on_audio_channel_closed_ = callback;
}

void Protocol::OnNetworkError(std::function<void(const std::string& message)> callback) {
    on_network_error_ = callback;
}

void Protocol::SetError(const std::string& message) {
    error_occurred_ = true;
    if (on_network_error_ != nullptr) {
        on_network_error_(message);
    }
}

void Protocol::SendAbortSpeaking(AbortReason reason) {

    char event_id[32];
    uint32_t random_value = esp_random();
    snprintf(event_id, sizeof(event_id), "%lu", random_value);
    
    std::string message = "{\"id\":\"" + std::string(event_id) + "\",\"event_type\":\"conversation.chat.cancel\"}";

    SendText(message);
}

void Protocol::SendWakeWordDetected(const std::string& wake_word) {
    // std::string json = "{\"session_id\":\"" + session_id_ + 
    //                   "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + wake_word + "\"}";
    // SendText(json);
}

void Protocol::SendStartListening(ListeningMode mode) {
    // std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    // message += ",\"type\":\"listen\",\"state\":\"start\"";
    // if (mode == kListeningModeRealtime) {
    //     message += ",\"mode\":\"realtime\"";
    // } else if (mode == kListeningModeAutoStop) {
    //     message += ",\"mode\":\"auto\"";
    // } else {
    //     message += ",\"mode\":\"manual\"";
    // }
    // message += "}";
    // SendText(message);
}

void Protocol::SendStopListening() {
    // std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"listen\",\"state\":\"stop\"}";
    // SendText(message);
}

void Protocol::SendIotDescriptors(const std::string& descriptors) {
    cJSON* root = cJSON_Parse(descriptors.c_str());
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse IoT descriptors: %s", descriptors.c_str());
        return;
    }

    if (!cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "IoT descriptors should be an array");
        cJSON_Delete(root);
        return;
    }

    int arraySize = cJSON_GetArraySize(root);
    for (int i = 0; i < arraySize; ++i) {
        cJSON* descriptor = cJSON_GetArrayItem(root, i);
        if (descriptor == nullptr) {
            ESP_LOGE(TAG, "Failed to get IoT descriptor at index %d", i);
            continue;
        }

        cJSON* messageRoot = cJSON_CreateObject();
        cJSON_AddStringToObject(messageRoot, "session_id", session_id_.c_str());
        cJSON_AddStringToObject(messageRoot, "type", "iot");
        cJSON_AddBoolToObject(messageRoot, "update", true);

        cJSON* descriptorArray = cJSON_CreateArray();
        cJSON_AddItemToArray(descriptorArray, cJSON_Duplicate(descriptor, 1));
        cJSON_AddItemToObject(messageRoot, "descriptors", descriptorArray);

        char* message = cJSON_PrintUnformatted(messageRoot);
        if (message == nullptr) {
            ESP_LOGE(TAG, "Failed to print JSON message for IoT descriptor at index %d", i);
            cJSON_Delete(messageRoot);
            continue;
        }

        SendText(std::string(message));
        cJSON_free(message);
        cJSON_Delete(messageRoot);
    }

    cJSON_Delete(root);
}

void Protocol::SendIotStates(const std::string& states) {
    // std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"iot\",\"update\":true,\"states\":" + states + "}";
    // SendText(message);
}

bool Protocol::IsTimeout() const {
    const int kTimeoutSeconds = 120;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_incoming_time_);
    bool timeout = duration.count() > kTimeoutSeconds;
    if (timeout) {
        ESP_LOGE(TAG, "Channel timeout %lld seconds", duration.count());
    }
    return timeout;
}

bool Protocol::IsAudioChannelBusy() const {
    return busy_sending_audio_;
}


void Protocol::UpdateRoomParams(const websocket_config_t* ws_config) {
    if (!ws_config) {
        ESP_LOGE(TAG, "Invalid websocket config pointer");
        return;
    }

    ESP_LOGI(TAG, "Updating WebSocket parameters:");
    ESP_LOGI(TAG, "  Platform type: %d", ws_config->platform_type);
    ESP_LOGI(TAG, "  Token quota: %d", ws_config->token_quota);
    ESP_LOGI(TAG, "  API Domain: %s", ws_config->coze_websocket.api_domain);
    ESP_LOGI(TAG, "  Access Token: %s", ws_config->coze_websocket.access_token);
    ESP_LOGI(TAG, "  Expires in: %d", ws_config->coze_websocket.expires_in);
    ESP_LOGI(TAG, "  Bot ID: %s", ws_config->coze_websocket.bot_id);
    ESP_LOGI(TAG, "  Voice ID: %s", ws_config->coze_websocket.voice_id);
    ESP_LOGI(TAG, "  Voice Lang: %s", ws_config->coze_websocket.voice_lang);
    ESP_LOGI(TAG, "  User ID: %s", ws_config->coze_websocket.user_id);
    ESP_LOGI(TAG, "  Conv ID: %s", ws_config->coze_websocket.conv_id);

    // 保存
    conversation_id_ = ws_config->coze_websocket.conv_id;
    access_token_ = ws_config->coze_websocket.access_token;
    bot_id_ = ws_config->coze_websocket.bot_id;
    voice_id_ = ws_config->coze_websocket.voice_id;
    api_domain_ = ws_config->coze_websocket.api_domain;
    expires_in_ = ws_config->coze_websocket.expires_in;
    voice_lang_ = ws_config->coze_websocket.voice_lang;
    user_id_ = ws_config->coze_websocket.user_id;
}
