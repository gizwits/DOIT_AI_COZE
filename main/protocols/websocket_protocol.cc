#include "websocket_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"
#include "mbedtls/base64.h"
#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"
#include "protocols/mcp.h"

#define TAG "WS"

// Received message on topic: llm/nd7ec83a/config/response
// in_str:{"method":"websocket.auth.response","body":{"platform_type":1,"token_quota":500000,"coze_websocket":{"bot_id":"7483788991729270847","voice_id":"7426720361753968677","user_id":"nd7ec83a","conv_id":"7486307379559104521","access_token":"czs_qNqGYuaxk7GQXXz5l6RwjaUYyE6y0sqCuRUl1enbJUPkMYQWgosyTdLpCDOZcEZOr","expires_in":3540}}}

// CRC lookup table for Ogg checksum calculation
const uint32_t crc_table[256] = {
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
    0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
    0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
    0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
    0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
    0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
    0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
    0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
    0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
    0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
    0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
    0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
    0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
    0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
    0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
    0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
    0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
    0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
    0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
    0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
    0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
    0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
    0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
    0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
    0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
    0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
    0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
    0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
    0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
    0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
    0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
    0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
    0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
    0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
    0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
    0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
    0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
    0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
    0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
    0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
    0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
    0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
    0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
    0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
    0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
    0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
    0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
    0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
    0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
    0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
    0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
    0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
    0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
    0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
    0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
    0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
    0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
    0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
    0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
    0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
    0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
    if (websocket_ != nullptr) {
        delete websocket_;
    }
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start() {
    return true;
}

// Calculate Ogg checksum
uint32_t CalculateOggChecksum(const uint8_t* data, size_t length) {
    uint32_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc = (crc << 8) ^ crc_table[((crc >> 24) & 0xff) ^ data[i]];
    }
    return crc;
}

void WebsocketProtocol::SendAudio(const std::vector<uint8_t>& data) {
    if (websocket_ == nullptr || !websocket_->IsConnected() || data.empty()) {
        return;
    }

    if (is_first_packet_) {
        // Create and send OpusHead
        ogg_header_.header_type = 2;  // BOS (Beginning of Stream)
        ogg_header_.bitstream_serial_number = 0x12345678;
        ogg_header_.page_sequence_number = 0;
        ogg_header_.page_segments = 1;
        ogg_header_.segment_table[0] = sizeof(OpusHead);

        // Calculate total size
        const size_t total_size = sizeof(OggPageHeader) + sizeof(OpusHead);
        ogg_data_buffer_.resize(total_size);

        // Copy headers
        memcpy(ogg_data_buffer_.data(), &ogg_header_, sizeof(OggPageHeader));
        memcpy(ogg_data_buffer_.data() + sizeof(OggPageHeader), &opus_head_, sizeof(OpusHead));

        // Calculate and set checksum
        uint32_t checksum = CalculateOggChecksum(ogg_data_buffer_.data(), total_size);
        memcpy(ogg_data_buffer_.data() + 22, &checksum, sizeof(uint32_t));

        // Send OpusHead
        SendOggData(ogg_data_buffer_);

        // Create and send OpusTags
        ogg_header_.header_type = 0;
        ogg_header_.page_sequence_number = 1;
        ogg_header_.page_segments = 1;
        ogg_header_.segment_table[0] = sizeof(OpusTags);

        // Calculate total size
        const size_t tags_size = sizeof(OggPageHeader) + sizeof(OpusTags);
        ogg_data_buffer_.resize(tags_size);

        // Copy headers
        memcpy(ogg_data_buffer_.data(), &ogg_header_, sizeof(OggPageHeader));
        memcpy(ogg_data_buffer_.data() + sizeof(OggPageHeader), &opus_tags_, sizeof(OpusTags));

        // Calculate and set checksum
        checksum = CalculateOggChecksum(ogg_data_buffer_.data(), tags_size);
        memcpy(ogg_data_buffer_.data() + 22, &checksum, sizeof(uint32_t));

        // Send OpusTags
        SendOggData(ogg_data_buffer_);

        is_first_packet_ = false;
    }

    // Create Ogg page for Opus data
    ogg_header_.header_type = 0;
    ogg_header_.granule_position = data.size();  // Simplified granule position
    ogg_header_.bitstream_serial_number = 0x12345678;
    ogg_header_.page_sequence_number++;
    ogg_header_.page_segments = 1;
    ogg_header_.segment_table[0] = data.size();

    // Calculate total size
    const size_t total_size = sizeof(OggPageHeader) + data.size();
    ogg_data_buffer_.resize(total_size);

    // Copy headers and data
    memcpy(ogg_data_buffer_.data(), &ogg_header_, sizeof(OggPageHeader));
    memcpy(ogg_data_buffer_.data() + sizeof(OggPageHeader), data.data(), data.size());

    // Calculate and set checksum
    uint32_t checksum = CalculateOggChecksum(ogg_data_buffer_.data(), total_size);
    memcpy(ogg_data_buffer_.data() + 22, &checksum, sizeof(uint32_t));

    // Send Opus data
    SendOggData(ogg_data_buffer_);
}

void WebsocketProtocol::SendOggData(const std::vector<uint8_t>& ogg_data) {
    // Calculate required base64 buffer size
    size_t out_len = 4 * ((ogg_data.size() + 2) / 3);
    
    // Resize base64 buffer if needed
    if (out_len + 1 > base64_buffer_size_) {
        base64_buffer_.reset(new char[out_len + 1]);
        base64_buffer_size_ = out_len + 1;
        if (!base64_buffer_) {
            ESP_LOGE(TAG, "Failed to allocate base64 buffer");
            return;
        }
    }

    size_t encoded_len;
    mbedtls_base64_encode((unsigned char *)base64_buffer_.get(), base64_buffer_size_, &encoded_len,
                         (const unsigned char*)ogg_data.data(), ogg_data.size());
    base64_buffer_[encoded_len] = '\0';

    // Create event ID
    char event_id[32];
    uint32_t random_value = esp_random();
    snprintf(event_id, sizeof(event_id), "%lu", random_value);

    // Reuse message buffer
    message_buffer_.clear();
    message_buffer_.reserve(256 + out_len);  // Pre-allocate space
    message_buffer_ = "{";
    message_buffer_ += "\"id\":\"" + std::string(event_id) + "\",";
    message_buffer_ += "\"event_type\":\"input_audio_buffer.append\",";
    message_buffer_ += "\"data\":{";
    message_buffer_ += "\"delta\":\"" + std::string(base64_buffer_.get()) + "\"";
    message_buffer_ += "}";
    message_buffer_ += "}";

    // Send message
    websocket_->Send(message_buffer_);
}

void WebsocketProtocol::SendAudio(const std::vector<int16_t>& data) {
    if (websocket_ == nullptr || !websocket_->IsConnected() || data.empty()) {
        return;
    }
    // ESP_LOGI(TAG, "Send audio data size: %d", data.size());
    // 将 int16_t 数据转换为 base64
    size_t data_size = data.size() * sizeof(int16_t);
    size_t out_len = 4 * ((data_size + 2) / 3);  // base64 编码后的长度
    std::unique_ptr<char[]> base64_buffer(new char[out_len + 1]);
    if (!base64_buffer) {
        ESP_LOGE(TAG, "Failed to allocate base64 buffer");
        return;
    }

    size_t encoded_len;
    mbedtls_base64_encode((unsigned char *)base64_buffer.get(), out_len + 1, &encoded_len,
                         (const unsigned char*)data.data(), data_size);
    base64_buffer[encoded_len] = '\0';

    // 创建事件 ID (使用随机数，确保为正数)
    char event_id[32];
    uint32_t random_value = esp_random();
    snprintf(event_id, sizeof(event_id), "%lu", random_value);

    // 构建消息
    std::string message = "{";
    message += "\"id\":\"" + std::string(event_id) + "\",";
    message += "\"event_type\":\"input_audio_buffer.append\",";
    message += "\"data\":{";
    message += "\"delta\":\"" + std::string(base64_buffer.get()) + "\"";
    message += "}";
    message += "}";

    // 发送消息
    websocket_->Send(message);
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr) {
        return false;
    }
    websocket_->Send(text);
    return true;
}

void WebsocketProtocol::SendStopListening() {
    if (websocket_ == nullptr) {
        return;
    }

    // Reset is_first_packet for next audio stream
    is_first_packet_ = true;

    // 创建事件 ID (使用随机数，确保为正数)
    char event_id[32];
    uint32_t random_value = esp_random();
    snprintf(event_id, sizeof(event_id), "%lu", random_value);

    // 构建完整的消息
    char message[256];
    snprintf(message, sizeof(message),
        "{"
            "\"id\":\"%s\","
            "\"event_type\":\"input_audio_buffer.complete\","
            "\"data\":{}"
        "}", event_id);

    // 发送消息
    websocket_->Send(message);
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() {
    if (websocket_ != nullptr) {
        delete websocket_;
        websocket_ = nullptr;
    }
}

bool WebsocketProtocol::OpenAudioChannel() {
    if (websocket_ != nullptr) {
        delete websocket_;
    }
    if (bot_id_.empty() || access_token_.empty() || voice_id_.empty()) {
        ESP_LOGE(TAG, "Bot ID or access token or voice id is empty");
        return false;
    }

    error_occurred_ = false;
    std::string url = std::string("?ws://") + api_domain_ + std::string("?bot_id=") + std::string(bot_id_);
    std::string token = "Bearer " + std::string(access_token_);

    message_cache_ = "";
    websocket_ = Board::GetInstance().CreateWebSocket();
    websocket_->SetHeader("Authorization", token.c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (!data || len == 0) {
            return;
        }

        // Reuse string view for data
        std::string_view str_data(data, len);

        constexpr std::string_view key = "\"event_type\":\"";
        size_t event_start = str_data.find(key);
        if (event_start == std::string_view::npos) {
            return;
        }

        event_start += key.length();
        size_t event_end = str_data.find('"', event_start);
        if (event_end == std::string_view::npos) {
            return;
        }

        std::string_view event_type = str_data.substr(event_start, event_end - event_start);
        if (event_type.empty() || event_type.length() >= 64) {
            return;
        }

        if(event_type == "conversation.audio.delta") {
            constexpr std::string_view content_key = "\"content\":\"";
            size_t content_start = str_data.find(content_key);
            if (content_start != std::string_view::npos) {
                content_start += content_key.length();
                
                size_t content_end = content_start;
                bool escaped = false;
                
                while (content_end < str_data.length()) {
                    if (str_data[content_end] == '\\') {
                        escaped = !escaped;
                    } else if (str_data[content_end] == '"' && !escaped) {
                        break;
                    } else {
                        escaped = false;
                    }
                    content_end++;
                }
                
                if (content_end < str_data.length()) {
                    std::string_view base64_content = str_data.substr(content_start, content_end - content_start);
                    
                    // Calculate decoded size
                    size_t output_len = 0;
                    mbedtls_base64_decode(nullptr, 0, &output_len, 
                        (const unsigned char*)base64_content.data(), 
                        base64_content.length());

                    // Reuse buffer for decoded data
                    if (output_len > ogg_data_buffer_.capacity()) {
                        ogg_data_buffer_.reserve(output_len);
                    }
                    ogg_data_buffer_.resize(output_len);

                    size_t actual_len = 0;
                    int ret = mbedtls_base64_decode(
                        ogg_data_buffer_.data(), ogg_data_buffer_.size(), &actual_len,
                        (const unsigned char*)base64_content.data(), 
                        base64_content.length());

                    if (ret == 0 && actual_len > 0) {
                        if (on_incoming_audio_ != nullptr) {
                            std::vector<uint8_t> audio_data(ogg_data_buffer_.begin(), ogg_data_buffer_.begin() + actual_len);
                            on_incoming_audio_(std::move(audio_data));
                        }
                    }
                }
            }
        } else {
            // Reuse cJSON root
            cJSON* root = cJSON_Parse(data);
            if (!root) {
                return;
            }

            if (event_type == "chat.created") {
                ParseServerHello(root);
            } else if (event_type == "conversation.audio_transcript.update") {
                auto data_json = cJSON_GetObjectItem(root, "data");
                auto content_json = cJSON_GetObjectItem(data_json, "content");
                
                // Reuse message buffer
                message_buffer_.clear();
                message_buffer_ = "{";
                message_buffer_ += "\"type\":\"stt\",";
                message_buffer_ += "\"text\":\"" + std::string(content_json->valuestring) + "\"";
                message_buffer_ += "}";
                
                auto message_json = cJSON_Parse(message_buffer_.c_str());
                if (message_json) {
                    on_incoming_json_(message_json);
                    cJSON_Delete(message_json);
                }
            } else if (event_type == "conversation.chat.in_progress") {
                message_cache_.clear();
                message_buffer_.clear();
                message_buffer_ = "{";
                message_buffer_ += "\"type\":\"tts\",";
                message_buffer_ += "\"state\":\"start\"";
                message_buffer_ += "}";
                
                auto message_json = cJSON_Parse(message_buffer_.c_str());
                if (message_json) {
                    on_incoming_json_(message_json);
                    cJSON_Delete(message_json);
                }
            } else if (event_type == "conversation.audio.completed") {
                message_buffer_.clear();
                message_buffer_ = "{";
                message_buffer_ += "\"type\":\"tts\",";
                message_buffer_ += "\"state\":\"stop\"";
                message_buffer_ += "}";
                
                auto message_json = cJSON_Parse(message_buffer_.c_str());
                if (message_json) {
                    on_incoming_json_(message_json);
                    cJSON_Delete(message_json);
                }
            } else if (event_type == "conversation.message.delta") {
                auto data_json = cJSON_GetObjectItem(root, "data");
                auto content_json = cJSON_GetObjectItem(data_json, "content");

                message_cache_ += std::string(content_json->valuestring);
                message_buffer_.clear();
                message_buffer_ = "{";
                message_buffer_ += "\"type\":\"tts\",";
                message_buffer_ += "\"state\":\"sentence_start\",";
                message_buffer_ += "\"text\":\"" + message_cache_ + "\"";
                message_buffer_ += "}";
                
                auto message_json = cJSON_Parse(message_buffer_.c_str());
                if (message_json) {
                    on_incoming_json_(message_json);
                    cJSON_Delete(message_json);
                }
            } else if (event_type == "conversation.chat.requires_action") {
                CozeMCPParser::getInstance().handle_mcp(str_data);
            } else if (event_type == "error") {
                ESP_LOGE(TAG, "Error: %s", str_data.data());
            }
            
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server");
        SetError(Lang::Strings::SERVER_NOT_FOUND);
        return false;
    }

    // Wait for server hello
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    char event_id[32];
    uint32_t random_value = esp_random();
    snprintf(event_id, sizeof(event_id), "%lu", random_value);
    
    std::string user_id = "nd7ec83a";  // You may want to set this appropriately
    std::string codec = "opus";
    std::string message = "{";
    message += "\"id\":\"" + std::string(event_id) + "\",";
    message += "\"event_type\":\"chat.update\",";
    message += "\"data\":{";
    message += "\"event_subscriptions\": [";
    message += "\"chat.created\",";
    message += "\"chat.updated\",";
    message += "\"conversation.chat.created\",";
    message += "\"conversation.chat.in_progress\",";
    message += "\"conversation.audio.delta\",";
    message += "\"conversation.audio.completed\",";
    message += "\"conversation.chat.completed\",";
    message += "\"conversation.chat.failed\",";
    message += "\"error\",";
    message += "\"input_audio_buffer.completed\",";
    message += "\"input_audio_buffer.cleared\",";
    message += "\"conversation.cleared\",";
    message += "\"conversation.chat.canceled\",";
    message += "\"conversation.audio_transcript.completed\",";
    message += "\"conversation.chat.requires_action\",";
    message += "\"input_audio_buffer.speech_started\",";
    message += "\"input_audio_buffer.speech_stopped\"";
    message += "],";
    message += "\"turn_detection\": {";
    message += "\"type\": \"client_vad\",";  // 判停类型，client_vad/server_vad，默认为 client_vad
    message += "\"prefix_padding_ms\": 300,"; // server_vad模式下，VAD 检测到语音之前要包含的音频量，单位为 ms。默认为 600ms
    message += "\"silence_duration_ms\": 300"; // server_vad模式下，检测语音停止的静音持续时间，单位为 ms。默认为 800ms
    message += "},";
    message += "\"chat_config\":{";
    message += "\"auto_save_history\":true,";
    message += "\"conversation_id\":\"" + conversation_id_ + "\",";
    message += "\"user_id\":\"" + user_id + "\",";
    message += "\"meta_data\":{},";
    message += "\"custom_variables\":{},";
    message += "\"extra_params\":{}";
    message += "},";
    message += "\"input_audio\":{";
    message += "\"format\":\"ogg\",";
    message += "\"codec\":\"opus\",";
    message += "\"sample_rate\":16000,";
    message += "\"channel\":1,";
    message += "\"bit_depth\":16";
    message += "},";
    message += "\"output_audio\":{";
    message += "\"codec\":\"" + codec + "\",";
    message += "\"opus_config\":{";
    message += "\"sample_rate\":16000,";
    message += "\"use_cbr\":false,";
    message += "\"frame_size_ms\":60,";
    message += "\"limit_config\":{";
    message += "\"period\":1,";
    message += "\"max_frame_num\":17";
    message += "}";
    message += "},";
    message += "\"speech_rate\":0,";
    message += "\"voice_id\":\"" + std::string(voice_id_) + "\"";
    message += "}";
    message += "}";
    message += "}";
    
    websocket_->Send(message);

    // ESP_LOGI(TAG, "Send message: %s", message.c_str());

    return true;
}

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    // COZE 的音频信息是由设备发起的，因此这里直接返回
    server_sample_rate_ = 16000;
    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}