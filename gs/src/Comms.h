#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include "Clock.h"

struct fec_t;

class Comms
{
public:
    Comms();
    ~Comms();

    struct TX_Descriptor
    {
        std::string interface;
        uint32_t coding_k = 12;
        uint32_t coding_n = 20;
        size_t mtu = 1200;
    };

    struct RX_Descriptor
    {
        std::vector<std::string> interfaces;
        Clock::duration max_latency = std::chrono::milliseconds(500);
        Clock::duration reset_duration = std::chrono::milliseconds(1000);
        uint32_t coding_k = 12;
        uint32_t coding_n = 20;
        size_t mtu = 1200;
    };

    bool init(RX_Descriptor const& rx_descriptor, TX_Descriptor const& tx_descriptor);

    void process();

    void send(void const* data, size_t size, bool flush);
    //std::function<void(void const* data, size_t size)> on_data_received;
    bool receive(void* data, size_t& size);

    size_t get_data_rate() const;
    int get_input_dBm() const;

    static std::vector<std::string> enumerate_interfaces();

    struct PCap;
    struct RX;
    struct TX;

private:
    bool prepare_pcap(std::string const& interface, PCap& pcap);

    bool prepare_filter(PCap& pcap);
    void prepare_radiotap_header(size_t rate_hz);
    void prepare_tx_packet_header(uint8_t* buffer);
    bool process_rx_packet(PCap& pcap);
    void process_rx_packets();

    void tx_thread_proc();
    void rx_thread_proc(size_t index);

    TX_Descriptor m_tx_descriptor;
    RX_Descriptor m_rx_descriptor;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_exit = false;

    size_t m_packet_header_offset = 0;
    size_t m_payload_offset = 0;

    std::atomic_int m_best_input_dBm = {0};
    std::atomic_int m_latched_input_dBm = {0};

    size_t m_data_stats_rate = 0;
    size_t m_data_stats_data_accumulated = 0;
    Clock::time_point m_data_stats_last_tp = Clock::now();
};
