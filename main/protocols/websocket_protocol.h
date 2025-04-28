#ifndef _WEBSOCKET_PROTOCOL_H_
#define _WEBSOCKET_PROTOCOL_H_

#include "protocol.h"
#include "esp_heap_caps.h"
#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

// Opus header structures
struct OpusHead {
    char magic[8] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
    uint8_t version = 1;
    uint8_t channels = 1;
    uint16_t preskip = 0;
    uint32_t sample_rate = 16000;
    int16_t output_gain = 0;
    uint8_t channel_mapping = 0;
} __attribute__((packed));

struct OpusTags {
    char magic[8] = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'};
    uint32_t vendor_string_length = 7;  // Length of "DOIT_AI"
    char vendor_string[32] = "DOIT_AI";
    uint32_t comment_list_length = 0;
} __attribute__((packed));

// Ogg page header structure
struct OggPageHeader {
    char capture_pattern[4] = {'O', 'g', 'g', 'S'};
    uint8_t version = 0;
    uint8_t header_type = 0;
    uint64_t granule_position = 0;
    uint32_t bitstream_serial_number = 0;
    uint32_t page_sequence_number = 0;
    uint32_t checksum = 0;
    uint8_t page_segments = 1;
    uint8_t segment_table[1];
} __attribute__((packed));

class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();

    virtual ~WebsocketProtocol();

    virtual bool Start() override;
    virtual bool OpenAudioChannel() override;
    virtual void SendStopListening() override;
    virtual void CloseAudioChannel() override;
    virtual bool IsAudioChannelOpened() const override;
    virtual void SendAudio(const std::vector<int16_t>& data) override;
    virtual void SendAudio(const std::vector<uint8_t>& data) override;

private:
    WebSocket* websocket_ = nullptr;
    EventGroupHandle_t event_group_handle_;
    std::string message_cache_;
    bool is_first_packet_ = true;
    std::vector<uint8_t> ogg_data_buffer_;  // Reuse buffer for Ogg data
    std::unique_ptr<char[]> base64_buffer_;  // Reuse buffer for base64 encoding
    size_t base64_buffer_size_ = 0;  // Current size of base64 buffer
    std::string message_buffer_;  // Reuse buffer for message construction
    OggPageHeader ogg_header_;  // Reuse Ogg header
    OpusHead opus_head_;  // Reuse Opus head
    OpusTags opus_tags_;  // Reuse Opus tags
   

    void ParseServerHello(const cJSON* root);
    bool SendText(const std::string& text) override;
    void SendOggData(const std::vector<uint8_t>& ogg_data);
};

#endif
