#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "fec.h"

class Fec_Codec
{
public:
    Fec_Codec();

    static const uint8_t MAX_CODING_K = 16;
    static const uint8_t MAX_CODING_N = 32;
    static const size_t PACKET_OVERHEAD = 6;

    enum class Core
    {
        Any,
        Core_0,
        Core_1
    };

    struct Descriptor
    {
        uint8_t coding_k = 2;
        uint8_t coding_n = 4;
        size_t mtu = 512;
        Core core = Core::Any;
        uint8_t priority = configMAX_PRIORITIES - 1;
    };

    bool init_encoder(const Descriptor& descriptor);
    bool init_decoder(const Descriptor& descriptor);
    IRAM_ATTR bool is_initialized() const;

    const Descriptor& get_descriptor() const;
    bool is_encoder() const;

    //Callback for when an encoded packet is available
    //NOTE: this is called form another thread!!!
    void set_data_encoded_cb(void (*cb)(void* data, size_t size));

    //Add here data that will be encoded.
    //Size dosn't have to be a full packet. Can be anything > 0, even bigger than a packet
    //NOTE: This has to be called from a single thread only (any thread, as long as it's just one)
    IRAM_ATTR bool encode_data(const void* data, size_t size, bool block);
    IRAM_ATTR bool flush_encode_packet(bool block);
    IRAM_ATTR uint8_t* get_encode_packet_data(bool block);
    IRAM_ATTR bool is_encode_packet_empty();

    //Callback for when a decoded packet is ready.
    //NOTE: this is called form another thread!!!
    void set_data_decoded_cb(void (*cb)(void* data, size_t size));

    //Add here data that will be decoded.
    //Size dosn't have to be a full packet. Can be anything > 0, even bigger than a packet
    //NOTE: This has to be called from a single thread only (any thread, as long as it's just one)
    IRAM_ATTR bool decode_data(const void* data, size_t size, bool block);

private:
    bool init(const Descriptor& descriptor, bool is_encoder);
    void stop_tasks();
    bool start_tasks();

    void encoder_task_proc();
    void decoder_task_proc();
    static void static_encoder_task_proc(void* params);
    static void static_decoder_task_proc(void* params);

    Descriptor m_descriptor;

    size_t m_encoder_pool_size = 0;
    size_t m_decoder_pool_size = 0;

    size_t m_encoded_packet_size = 0;

    fec_t* m_fec = nullptr;
    bool m_is_encoder = false;

    struct Encoder
    {
        struct Packet
        {
            uint32_t size = 0;
            uint8_t* data = nullptr;
        };
        QueueHandle_t packet_queue = nullptr;
        QueueHandle_t packet_pool = nullptr;
        TaskHandle_t task = nullptr;

        uint32_t last_block_index = 0;

        std::vector<Packet> block_packets;
        std::vector<Packet> block_fec_packets; //these are owned by the array

        std::vector<uint8_t const*> fec_src_ptrs;
        std::vector<uint8_t*> fec_dst_ptrs;

        std::vector<Packet> packet_pool_owned;

        Packet crt_packet;

        void (*cb)(void* data, size_t size);
    } m_encoder;

    void seal_packet(Encoder::Packet& packet, uint32_t block_index, uint8_t packet_index);

    struct Decoder
    {
        struct Packet
        {
            bool received_header = false;
            bool is_processed = false;
            uint32_t size = 0;
            uint32_t block_index = 0;
            uint32_t packet_index = 0;
            uint8_t* data = nullptr;
        };
        QueueHandle_t packet_queue = nullptr;
        QueueHandle_t packet_pool = nullptr;
        TaskHandle_t task = nullptr;

        uint32_t crt_block_index = 0;
        std::vector<Packet> block_packets;
        std::vector<Packet> block_fec_packets;

        std::vector<uint8_t const*> fec_src_ptrs;
        std::vector<uint8_t*> fec_dst_ptrs;

        std::vector<Packet> fec_decoded_packets;
        std::vector<Packet> packet_pool_owned;

        Packet crt_packet;

        void (*cb)(void* data, size_t size);
    } m_decoder;

    Encoder::Packet* pop_encoder_packet_from_pool();
    void push_encoder_packet_to_pool(Encoder::Packet* packet);
    Decoder::Packet* pop_decoder_packet_from_pool();
    void push_decoder_packet_to_pool(Decoder::Packet* packet);
};
