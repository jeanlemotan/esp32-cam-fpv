#include "Comms.h"
#include <pcap.h>
#include "radiotap/radiotap.h"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <set>
#include <cassert>
#include <atomic>
#include <iostream>
#include "fec.h"
#include "Log.h"
#include "Pool.h"
#include "structures.h"

//#define DEBUG_PCAP

static constexpr unsigned BLOCK_NUMS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                          10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                          21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

static constexpr size_t MAX_PACKET_SIZE = 4192;
static constexpr size_t MAX_USER_PACKET_SIZE = 1470;

static constexpr size_t DEFAULT_RATE_HZ = 26000000;

static std::vector<uint8_t> RADIOTAP_HEADER;

static constexpr size_t SRC_MAC_LASTBYTE = 15;
static constexpr size_t DST_MAC_LASTBYTE = 21;

// Penumbra IEEE80211 header
static uint8_t IEEE_HEADER[] =
{
    0x08,
    0x01,
    0x00,
    0x00,
    0x13,
    0x22,
    0x33,
    0x44,
    0x55,
    0x66,
    0x13,
    0x22,
    0x33,
    0x44,
    0x55,
    0x66,
    0x13,
    0x22,
    0x33,
    0x44,
    0x55,
    0x66,
    0x10,
    0x86,
};


#pragma pack(push, 1)

struct Penumbra_Radiotap_Header
{
    int32_t channel = 0;
    int32_t channel_flags = 0;
    int32_t rate = 0;
    int32_t input_dBm = 0;
    int32_t radiotap_flags = 0;
};

struct Packet_Header
{
    uint32_t block_index : 24;
    uint32_t packet_index : 8;
    uint16_t size = 0;
};

#pragma pack(pop)

static_assert(sizeof(Packet_Header) == 6);

//A     B       C       D       E       F
//A     Bx      Cx      Dx      Ex      Fx

struct Comms::PCap
{
    std::mutex mutex;
    pcap_t* pcap = nullptr;
    char error_buffer[PCAP_ERRBUF_SIZE] = {0};
    int rx_pcap_selectable_fd = 0;

    size_t _80211_header_length = 0;
};

struct Comms::TX
{
    std::thread thread;

    fec_t* fec = nullptr;
    std::array<uint8_t const*, 16> fec_src_packet_ptrs;
    std::array<uint8_t*, 32> fec_dst_packet_ptrs;

    PCap* pcap = nullptr;

    struct Packet 
    {
        std::vector<uint8_t> data;
    };
    using Packet_ptr = Pool<Packet>::Ptr;
    Pool<Packet> packet_pool;

    size_t transport_packet_size = 0;
    size_t streaming_packet_size = 0;
    size_t payload_size = 0;

    ////////
    //These are accessed by both the TX thread and the main thread
    std::mutex packet_queue_mutex;
    std::deque<Packet_ptr> packet_queue;
    std::condition_variable packet_queue_cv;
    ////////

    ////////
    //these live in the TX thread only
    std::deque<Packet_ptr> ready_packet_queue;
    std::vector<Packet_ptr> block_packets;
    std::vector<Packet_ptr> block_fec_packets;
    ///////

    Packet_ptr crt_packet;

    uint32_t last_block_index = 1;
};

struct Comms::RX
{
    std::thread thread;

    fec_t* fec = nullptr;
    std::array<uint8_t const*, 16> fec_src_packet_ptrs;
    std::array<uint8_t*, 32> fec_dst_packet_ptrs;

    std::vector<PCap*> pcaps;

    size_t transport_packet_size = 0;
    size_t streaming_packet_size = 0;
    size_t payload_size = 0;


    struct Packet
    {
        bool is_processed = false;
        uint32_t index = 0;
        std::vector<uint8_t> data;
    };

    using Packet_ptr = Pool<Packet>::Ptr;
    Pool<Packet> packet_pool;

    struct Block
    {
        uint32_t index = 0;

        std::vector<Packet_ptr> packets;
        std::vector<Packet_ptr> fec_packets;
    };

    using Block_ptr = Pool<Block>::Ptr;
    Pool<Block> block_pool;

    ////////////////////////////////////////
    std::mutex block_queue_mutex;
    std::deque<Block_ptr> block_queue;

    Clock::time_point last_block_tp = Clock::now();
    Clock::time_point last_packet_tp = Clock::now();

    uint32_t next_block_index = 0;
    ////////////////////////////////////////

    std::mutex ready_packet_queue_mutex;
    std::deque<Packet_ptr> ready_packet_queue;
};

static void seal_packet(Comms::TX::Packet& packet, size_t header_offset, uint32_t block_index, uint8_t packet_index)
{
    assert(packet.data.size() >= header_offset + sizeof(Comms::TX::Packet));

    Packet_Header& header = *reinterpret_cast<Packet_Header*>(packet.data.data() + header_offset);
    header.size = packet.data.size() - header_offset;
    header.block_index = block_index;
    header.packet_index = packet_index;
}

struct Comms::Impl
{
    size_t tx_packet_header_length = 0;
    std::vector<std::unique_ptr<PCap>> pcaps;

    TX tx;
    RX rx;
};

////////////////////////////////////////////////////////////////////////////////////////////

std::vector<std::string> Comms::enumerate_interfaces()
{
    std::vector<std::string> res;

    char error_buf[PCAP_ERRBUF_SIZE];
    pcap_if_t* head = nullptr;
    if (pcap_findalldevs(&head, error_buf) == -1)
    {
        LOGE("Error enumerating rfmon interfaces: {}", error_buf);
        return {};
    }

    pcap_if_t* crt = head;
    while (crt)
    {
        if (crt->flags & PCAP_IF_LOOPBACK)
            LOGI("Skipping {} because it's a loppback interface", crt->name);
        else if ((crt->flags & PCAP_IF_UP) == 0)
            LOGI("Skipping {} because it's down", crt->name);
        else
            res.push_back(crt->name);
        crt = crt->next;
    }

    pcap_freealldevs(head);

    return res;
}

////////////////////////////////////////////////////////////////////////////////////////////

Comms::Comms()
{
}

////////////////////////////////////////////////////////////////////////////////////////////

Comms::~Comms()
{
    m_exit = true;

    m_impl->tx.packet_queue_cv.notify_all();

    if (m_impl->rx.thread.joinable())
        m_impl->rx.thread.join();

    if (m_impl->tx.thread.joinable())
        m_impl->tx.thread.join();

    fec_free(m_impl->rx.fec);
    fec_free(m_impl->tx.fec);
}

////////////////////////////////////////////////////////////////////////////////////////////

bool Comms::prepare_filter(PCap& pcap)
{
    struct bpf_program program;
    char program_src[512];

    int link_encap = pcap_datalink(pcap.pcap);

    switch (link_encap)
    {
    case DLT_PRISM_HEADER:
        LOGI("DLT_PRISM_HEADER Encap");
        pcap._80211_header_length = 0x20; // ieee80211 comes after this
        sprintf(program_src, "radio[0x4a:4]==0x11223344 && radio[0x4e:2] == 0x5566");
        break;

    case DLT_IEEE802_11_RADIO:
        LOGI("DLT_IEEE802_11_RADIO Encap");
        pcap._80211_header_length = 0x18; // ieee80211 comes after this
        sprintf(program_src, "ether[0x0a:4]==0x11223344 && ether[0x0e:2] == 0x5566");
        break;

    default:
        LOGE("!!! unknown encapsulation");
        return false;
    }

    if (pcap_compile(pcap.pcap, &program, program_src, 1, 0) == -1)
    {
        LOGE("Failed to compile program: {} : {}", program_src, pcap_geterr(pcap.pcap));
        return false;
    }
    if (pcap_setfilter(pcap.pcap, &program) == -1)
    {
        pcap_freecode(&program);
        LOGE("Failed to set program: {} : {}", program_src, pcap_geterr(pcap.pcap));
        return false;
    }
    pcap_freecode(&program);

    pcap.rx_pcap_selectable_fd = pcap_get_selectable_fd(pcap.pcap);

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////

static void radiotap_add_u8(uint8_t*& dst, size_t& idx, uint8_t data)
{
    *dst++ = data;
    idx++;
}

////////////////////////////////////////////////////////////////////////////////////////////

static void radiotap_add_u16(uint8_t*& dst, size_t& idx, uint16_t data)
{
    if ((idx & 1) == 1) //not aligned, pad first
    {
        radiotap_add_u8(dst, idx, 0);
    }
    *reinterpret_cast<uint16_t*>(dst) = data;
    dst += 2;
    idx += 2;
}

////////////////////////////////////////////////////////////////////////////////////////////

void Comms::prepare_radiotap_header(size_t rate_hz)
{
    RADIOTAP_HEADER.resize(1024);
    ieee80211_radiotap_header& hdr = reinterpret_cast<ieee80211_radiotap_header& >(*RADIOTAP_HEADER.data());
    hdr.it_version = 0;
    hdr.it_present = 0
                     //                    | (1 << IEEE80211_RADIOTAP_RATE)
                     | (1 << IEEE80211_RADIOTAP_TX_FLAGS)
                     //                    | (1 << IEEE80211_RADIOTAP_RTS_RETRIES)
                     | (1 << IEEE80211_RADIOTAP_DATA_RETRIES)
        //                    | (1 << IEEE80211_RADIOTAP_CHANNEL)
        //                    | (1 << IEEE80211_RADIOTAP_MCS)
        ;

    auto* dst = RADIOTAP_HEADER.data() + sizeof(ieee80211_radiotap_header);
    size_t idx = dst - RADIOTAP_HEADER.data();

    if (hdr.it_present & (1 << IEEE80211_RADIOTAP_RATE))
        radiotap_add_u8(dst, idx, std::min(static_cast<uint8_t>(rate_hz / 500000), uint8_t(1)));
    if (hdr.it_present & (1 << IEEE80211_RADIOTAP_TX_FLAGS))
        radiotap_add_u16(dst, idx, IEEE80211_RADIOTAP_F_TX_NOACK); //used to be 0x18
    if (hdr.it_present & (1 << IEEE80211_RADIOTAP_RTS_RETRIES))
        radiotap_add_u8(dst, idx, 0x0);
    if (hdr.it_present & (1 << IEEE80211_RADIOTAP_DATA_RETRIES))
        radiotap_add_u8(dst, idx, 0x0);
    if (hdr.it_present & (1 << IEEE80211_RADIOTAP_MCS))
    {
        radiotap_add_u8(dst, idx, IEEE80211_RADIOTAP_MCS_HAVE_MCS);
        radiotap_add_u8(dst, idx, 0);
        radiotap_add_u8(dst, idx, 1);
    }
    if (hdr.it_present & (1 << IEEE80211_RADIOTAP_CHANNEL))
    {
        radiotap_add_u16(dst, idx, 2467);
        radiotap_add_u16(dst, idx, 0);
    }

    //finish it
    hdr.it_len = static_cast<__le16>(idx);
    RADIOTAP_HEADER.resize(idx);

    //    RADIOTAP_HEADER.resize(sizeof(RADIOTAP_HEADER_original));
    //    memcpy(RADIOTAP_HEADER.data(), RADIOTAP_HEADER_original, sizeof(RADIOTAP_HEADER_original));
}

////////////////////////////////////////////////////////////////////////////////////////////

void Comms::prepare_tx_packet_header(uint8_t* buffer)
{
    //prepare the buffers with headers
    uint8_t* pu8 = buffer;

    memcpy(pu8, RADIOTAP_HEADER.data(), RADIOTAP_HEADER.size());
    pu8 += RADIOTAP_HEADER.size();

    memcpy(pu8, WLAN_IEEE_HEADER_GROUND2AIR, sizeof(WLAN_IEEE_HEADER_GROUND2AIR));
    pu8 += sizeof(WLAN_IEEE_HEADER_GROUND2AIR);
}

////////////////////////////////////////////////////////////////////////////////////////////

bool Comms::process_rx_packet(PCap& pcap)
{
    struct pcap_pkthdr* pcap_packet_header = nullptr;

    uint8_t payload_buffer[MAX_PACKET_SIZE];
    uint8_t* payload = payload_buffer;

    while (true)
    {
        {
            std::lock_guard<std::mutex> lg(pcap.mutex);
            int retval = pcap_next_ex(pcap.pcap, &pcap_packet_header, (const u_char**)&payload);
            if (retval < 0)
            {
                LOGE("Socket broken: {}", pcap_geterr(pcap.pcap));
                return false;
            }
            if (retval != 1)
                break;
        }

        size_t header_len = (payload[2] + (payload[3] << 8));
        if (pcap_packet_header->len < (header_len + pcap._80211_header_length))
        {
            LOGW("packet too small");
            return true;
        }

        size_t bytes = pcap_packet_header->len - (header_len + pcap._80211_header_length);

        ieee80211_radiotap_iterator rti;
        if (ieee80211_radiotap_iterator_init(&rti, (struct ieee80211_radiotap_header*)payload, pcap_packet_header->len) < 0)
        {
            LOGE("iterator null");
            return true;
        }

        int n = 0;
        Penumbra_Radiotap_Header prh;
        while ((n = ieee80211_radiotap_iterator_next(&rti)) == 0)
        {

            switch (rti.this_arg_index)
            {
            case IEEE80211_RADIOTAP_RATE:
                prh.rate = (*rti.this_arg);
                break;

            case IEEE80211_RADIOTAP_CHANNEL:
                prh.channel = (*((uint16_t*)rti.this_arg));
                prh.channel_flags = (*((uint16_t*)(rti.this_arg + 2)));
                break;

            case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
                prh.input_dBm = *(int8_t*)rti.this_arg;
                break;
            case IEEE80211_RADIOTAP_FLAGS:
                prh.radiotap_flags = *rti.this_arg;
                break;
            }
        }
        payload += header_len + pcap._80211_header_length;

        if (prh.radiotap_flags & IEEE80211_RADIOTAP_F_FCS)
            bytes -= 4;

        bool checksum_correct = (prh.radiotap_flags & 0x40) == 0;

        //    block_num = seq_nr / param_retransmission_block_size;//if retr_block_size would be limited to powers of two, this could be replaced by a logical AND operation

        //printf("rec %x bytes %d crc %d\n", seq_nr, bytes, checksum_correct);

#ifdef DEBUG_PCAP
        std::cout << "PCAP RX>>";
        std::copy(payload, payload + bytes, std::ostream_iterator<uint8_t>(std::cout));
        std::cout << "<<PCAP RX";
#endif
        if (!checksum_correct)
        {
            LOGW("invalid checksum.");
            return true;
        }

        {
            int best_input_dBm = m_best_input_dBm;
            m_best_input_dBm = std::max(best_input_dBm, prh.input_dBm);
        }

        {
            RX& rx = m_impl->rx;

            Packet_Header& header = *reinterpret_cast<Packet_Header*>(payload);
            uint32_t block_index = header.block_index;
            uint32_t packet_index = header.packet_index;
            if (packet_index >= m_rx_descriptor.coding_n)
            {
                LOGE("packet index out of range: {} > {}", packet_index, m_rx_descriptor.coding_n);
                return true;
            }

            std::lock_guard<std::mutex> lg(rx.block_queue_mutex);

            if (block_index < rx.next_block_index)
            {
                //LOGW("Old packet: {} < {}", block_index, rx.next_block_index);
                return true;
            }

            RX::Block_ptr block;

            //find the block
            {
                auto iter = std::lower_bound(rx.block_queue.begin(), rx.block_queue.end(), block_index, [](RX::Block_ptr const& l, uint32_t index) { return l->index < index; });
                if (iter != rx.block_queue.end() && (*iter)->index == block_index)
                    block = *iter;
                else
                {
                    block = rx.block_pool.acquire();
                    block->index = block_index;
                    rx.block_queue.insert(iter, block);
                }
            }

            RX::Packet_ptr packet = rx.packet_pool.acquire();
            packet->data.resize(bytes - sizeof(Packet_Header));
            packet->index = packet_index;
            memcpy(packet->data.data(), payload + sizeof(Packet_Header), bytes - sizeof(Packet_Header));

            //store packet
            if (packet_index >= m_rx_descriptor.coding_k)
            {
                auto iter = std::lower_bound(block->fec_packets.begin(), block->fec_packets.end(), packet_index, [](RX::Packet_ptr const& l, uint32_t index) { return l->index < index; });
                if (iter != block->fec_packets.end() && (*iter)->index == packet_index)
                {
                    //LOGW("Duplicated packet {} from block {} (index {})", packet_index, block_index, block_index * m_coding_k + packet_index);
                    return true;
                }
                else
                    block->fec_packets.insert(iter, packet);
            }
            else
            {
                auto iter = std::lower_bound(block->packets.begin(), block->packets.end(), packet_index, [](RX::Packet_ptr const& l, uint32_t index) { return l->index < index; });
                if (iter != block->packets.end() && (*iter)->index == packet_index)
                {
                    //LOGW("Duplicated packet {} from block {} (index {})", packet_index, block_index, block_index * m_coding_k + packet_index);
                    return true;
                }
                else
                    block->packets.insert(iter, packet);
            }
        }

        //m_impl->rx_queue.enqueue(payload, bytes);
        //        if (receive_callback && bytes > 0)
        //        {
        //            receive_callback(payload, bytes);
        //        }

#ifdef DEBUG_THROUGHPUT
        {
            static int xxx_data = 0;
            static std::chrono::system_clock::time_point xxx_last_tp = std::chrono::system_clock::now();
            xxx_data += bytes;
            auto now = std::chrono::system_clock::now();
            if (now - xxx_last_tp >= std::chrono::seconds(1))
            {
                float r = std::chrono::duration<float>(now - xxx_last_tp).count();
                LOGI("Received: {} KB/s", float(xxx_data) / r / 1024.f);
                xxx_data = 0;
                xxx_last_tp = now;
            }
        }
#endif
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////

bool Comms::prepare_pcap(std::string const& interface, PCap& pcap)
{
    LOGI("Opening interface {} in monitor mode", interface);

    pcap.pcap = pcap_create(interface.c_str(), pcap.error_buffer);
    if (pcap.pcap == nullptr)
    {
        LOGE("Unable to open interface {}: {}", interface, pcap_geterr(pcap.pcap));
        return false;
    }
    if (pcap_set_snaplen(pcap.pcap, 1800) < 0)
    {
        LOGE("Error setting pcap_set_snaplen: {}", pcap_geterr(pcap.pcap));
        return false;
    }
    if (pcap_set_promisc(pcap.pcap, 1) < 0)
    {
        LOGE("Error setting pcap_set_promisc: {}", pcap_geterr(pcap.pcap));
        return false;
    }
    if (pcap_set_rfmon(pcap.pcap, 1) < 0)
    {
        LOGE("Error setting pcap_set_rfmon: {}", pcap_geterr(pcap.pcap));
        return false;
    }
    if (pcap_set_timeout(pcap.pcap, -1) < 0)
    {
        LOGE("Error setting pcap_set_timeout: {}", pcap_geterr(pcap.pcap));
        return false;
    }
    if (pcap_set_immediate_mode(pcap.pcap, 1) < 0)
    {
        LOGE("Error setting pcap_set_immediate_mode: {}", pcap_geterr(pcap.pcap));
        return false;
    }
    if (pcap_set_buffer_size(pcap.pcap, 16000000) < 0)
    {
        LOGE("Error setting pcap_set_buffer_size: {}", pcap_geterr(pcap.pcap));
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int res = pcap_activate(pcap.pcap);
    if (res < 0)
    {
        LOGE("Error in pcap_activate: {}", pcap_geterr(pcap.pcap));
        return false;
    }
    else if (res == PCAP_WARNING_PROMISC_NOTSUP)
    {
        LOGE("Error in pcap_activate - not promiscous: {}", pcap_geterr(pcap.pcap));
        return false;
    }
    else if (res == PCAP_WARNING_TSTAMP_TYPE_NOTSUP)
    {
        //nothing, we don't care about timestamps
    }
    else if (res == PCAP_WARNING)
    {
        LOGW("Warning in pcap_activate: {}", pcap_geterr(pcap.pcap));
    }

    //    if (pcap_setnonblock(pcap.pcap, 1, pcap_error) < 0)
    //    {
    //        LOGE("Error setting pcap_set_snaplen: {}", pcap_geterr(pcap.pcap));
    //        return false;
    //    }
    if (pcap_setdirection(pcap.pcap, PCAP_D_IN) < 0)
    {
        LOGE("Error setting pcap_setdirection: {}", pcap_geterr(pcap.pcap));
        return false;
    }

    if (!prepare_filter(pcap))
        return false;

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////

bool Comms::init(RX_Descriptor const& rx_descriptor, TX_Descriptor const& tx_descriptor)
{
    if (tx_descriptor.interface.empty())
    {
        LOGE("Invalid TX interface");
        return false;
    }

    m_impl.reset(new Impl);

    m_tx_descriptor = tx_descriptor;
    m_tx_descriptor.mtu = std::min(tx_descriptor.mtu, MAX_USER_PACKET_SIZE);

    if (m_tx_descriptor.coding_k == 0 || 
        m_tx_descriptor.coding_n < m_tx_descriptor.coding_k || 
        m_tx_descriptor.coding_k > m_impl->tx.fec_src_packet_ptrs.size() || 
        m_tx_descriptor.coding_n > m_impl->tx.fec_dst_packet_ptrs.size())
    {
        LOGE("Invalid coding params: {} / {}", m_tx_descriptor.coding_k, m_tx_descriptor.coding_n);
        return false;
    }

    if (m_impl->tx.fec)
        fec_free(m_impl->tx.fec);

    m_impl->tx.fec = fec_new(m_tx_descriptor.coding_k, m_tx_descriptor.coding_n);

    /////////
    
    if (rx_descriptor.interfaces.empty())
    {
        LOGE("Invalid RX interfaces");
        return false;
    }

    m_rx_descriptor = rx_descriptor;
    m_rx_descriptor.mtu = std::min(rx_descriptor.mtu, MAX_USER_PACKET_SIZE);

    if (m_rx_descriptor.coding_k == 0 || 
        m_rx_descriptor.coding_n < m_rx_descriptor.coding_k || 
        m_rx_descriptor.coding_k > m_impl->rx.fec_src_packet_ptrs.size() || 
        m_rx_descriptor.coding_n > m_impl->rx.fec_dst_packet_ptrs.size())
    {
        LOGE("Invalid coding params: {} / {}", m_rx_descriptor.coding_k, m_rx_descriptor.coding_n);
        return false;
    }

    if (m_impl->rx.fec)
        fec_free(m_impl->rx.fec);

    m_impl->rx.fec = fec_new(m_rx_descriptor.coding_k, m_rx_descriptor.coding_n);

    /////////

    //    IEEE_HEADER[SRC_MAC_LASTBYTE] = 0;
    //    IEEE_HEADER[DST_MAC_LASTBYTE] = 0;

    prepare_radiotap_header(DEFAULT_RATE_HZ);
    m_impl->tx_packet_header_length = RADIOTAP_HEADER.size() + sizeof(WLAN_IEEE_HEADER_GROUND2AIR);
    LOGI("Radiocap header size: {}, IEEE header size: {}", RADIOTAP_HEADER.size(), sizeof(WLAN_IEEE_HEADER_GROUND2AIR));

    /////////////////////
    //calculate some offsets and sizes
    m_packet_header_offset = m_impl->tx_packet_header_length;
    m_payload_offset = m_packet_header_offset + sizeof(Packet_Header);

    m_impl->rx.transport_packet_size = m_payload_offset + m_rx_descriptor.mtu;
    m_impl->rx.streaming_packet_size = m_impl->rx.transport_packet_size - m_impl->tx_packet_header_length;
    m_impl->rx.payload_size = m_rx_descriptor.mtu;

    m_impl->tx.transport_packet_size = m_payload_offset + m_tx_descriptor.mtu;
    m_impl->tx.streaming_packet_size = m_impl->tx.transport_packet_size - m_impl->tx_packet_header_length;
    m_impl->tx.payload_size = m_tx_descriptor.mtu;

    /////////////////////

    m_impl->tx.packet_pool.on_acquire = [this](TX::Packet& packet) 
    {
        if (packet.data.empty())
        {
            packet.data.resize(m_payload_offset);
            prepare_tx_packet_header(packet.data.data());
        }
        else
            packet.data.resize(m_payload_offset);
    };

    m_impl->rx.packet_pool.on_acquire = [this](RX::Packet& packet) 
    {
        packet.index = 0;
        packet.is_processed = false;
        packet.data.clear();
        packet.data.reserve(m_impl->rx.transport_packet_size);
    };
    m_impl->rx.block_pool.on_acquire = [this](RX::Block& block) 
    {
        block.index = 0;

        block.packets.clear();
        block.packets.reserve(m_rx_descriptor.coding_k);

        block.fec_packets.clear();
        block.fec_packets.reserve(m_rx_descriptor.coding_n - m_rx_descriptor.coding_k);
    };
    m_impl->rx.block_pool.on_release = [this](RX::Block& block) 
    {
        block.packets.clear();
        block.fec_packets.clear();
    };

    //    m_impl->pcap = pcap_open_live(m_interface.c_str(), 2048, 1, -1, pcap_error);
    //    if (m_impl->pcap == nullptr)
    //    {
    //        LOGE("Unable to open interface {} in pcap: {}", m_interface, pcap_error);
    //        return false;
    //    }

    ////    if (pcap_setnonblock(m_impl->pcap, 1, pcap_error) < 0)
    ////    {
    ////        LOGE("Error setting {} to nonblocking mode: {}", m_interface, pcap_error);
    ////        return false;
    ////    }

    //    if (pcap_setdirection(m_impl->pcap, PCAP_D_IN) < 0)
    //    {
    //        LOGE("Error setting {} to IN capture only: {}", m_interface, pcap_geterr(m_impl->pcap));
    //        return false;
    //    }

    std::set<std::string> interfaces;
    for (auto i: m_rx_descriptor.interfaces)
        interfaces.insert(i);
    interfaces.insert(m_tx_descriptor.interface);

    m_impl->rx.pcaps.resize(interfaces.size());
    m_impl->pcaps.resize(interfaces.size());
    size_t index = 0;
    for (auto& i: interfaces)
    {
        m_impl->pcaps[index] = std::make_unique<PCap>();
        if (!prepare_pcap(i, *m_impl->pcaps[index]))
            return false;

        if (m_tx_descriptor.interface == i)
            m_impl->tx.pcap = m_impl->pcaps[index].get();

        for (size_t j = 0; j < m_rx_descriptor.interfaces.size(); j++)
        {
            if (m_rx_descriptor.interfaces[j] == i)
            {
                m_impl->rx.pcaps[j] = m_impl->pcaps[index].get();
                break;
            }
        }
        index++;
    }

    m_impl->tx.thread = std::thread([this]() { tx_thread_proc(); });
    m_impl->rx.thread = std::thread([this]() { rx_thread_proc(); });

#if defined RASPBERRY_PI_XXX
    {
        //        int policy = SCHED_OTHER;
        //        struct sched_param param;
        //        param.sched_priority = 0;
        int policy = SCHED_FIFO;
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(policy);
        if (pthread_setschedparam(m_impl->tx.thread.native_handle(), policy, &param) != 0)
            perror("Cannot set TX thread priority - using normal");
        if (pthread_setschedparam(m_impl->rx.thread.native_handle(), policy, &param) != 0)
            perror("Cannot set TX thread priority - using normal");
    }
#endif

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////

void Comms::rx_thread_proc()
{
    RX& rx = m_impl->rx;

    while (!m_exit)
    {
        fd_set readset;
        struct timeval to;

        to.tv_sec = 0;
        to.tv_usec = 30000;

        FD_ZERO(&readset);
        for (size_t i = 0; i < m_rx_descriptor.interfaces.size(); i++)
            FD_SET(rx.pcaps[i]->rx_pcap_selectable_fd, &readset);

        int n = select(30, &readset, nullptr, nullptr, &to);
        if (n != 0)
        {
            for (size_t i = 0; i < m_rx_descriptor.interfaces.size(); i++)
            {
                if (FD_ISSET(rx.pcaps[i]->rx_pcap_selectable_fd, &readset))
                    process_rx_packet(*rx.pcaps[i]);
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

void Comms::tx_thread_proc()
{
    TX& tx = m_impl->tx;
    uint32_t coding_k = m_tx_descriptor.coding_k;
    uint32_t coding_n = m_tx_descriptor.coding_n;

#if 0 //TEST THROUGHPUT
     TX::Packet_ptr packet = tx.packet_pool.acquire();

     size_t s = std::min(8192u, m_transport_packet_size - packet->data.size());
     size_t offset = packet->data.size();
     packet->data.resize(offset + s);

     auto start = Clock::now();

     size_t packets = 0;
     size_t total_size = 0;

     while (!m_exit)
     {
         int isize = static_cast<int>(packet->data.size());

         std::lock_guard<std::mutex> lg(tx.pcap.mutex);
         int r = pcap_inject(tx.pcap.pcap, packet->data.data(), isize);
         if (r <= 0)
         {
             LOGW("Trouble injecting packet: {} / {}: {}", r, isize, pcap_geterr(tx.pcap.pcap));
             //result = Result::ERROR;
         }
         if (r > 0)
         {
             if (r != isize)
             {
                 LOGW("Incomplete packet sent: {} / {}", r, isize);
             }
             else
             {
                 packets++;
                 total_size += isize;
             }
         }

         auto now = Clock::now();
         if (now - start > std::chrono::seconds(1))
         {
             float f = std::chrono::duration<float>(now - start).count();
             start = now;

             LOGI("Packets: {}, Size: {}MB", packets / f, (total_size / (1024.f * 1024.f)) / f);
             packets = 0;
             total_size = 0;
         }
     }
#endif

    while (!m_exit)
    {
        {
            //wait for data
            std::unique_lock<std::mutex> lg(tx.packet_queue_mutex);
            if (tx.packet_queue.empty())
                tx.packet_queue_cv.wait(lg, [this, &tx] { return tx.packet_queue.empty() == false || m_exit == true; });

            if (m_exit)
                break;

            TX::Packet_ptr packet;
            if (!tx.packet_queue.empty())
            {
                packet = tx.packet_queue.front();
                tx.packet_queue.pop_front();
            }

            if (packet)
            {
                seal_packet(*packet, m_packet_header_offset, tx.last_block_index, tx.block_packets.size());
                tx.ready_packet_queue.push_back(packet); //ready to send
                tx.block_packets.push_back(packet);
            }
        }

        //compute fec packets
        if (tx.block_packets.size() >= coding_k)
        {
            if (1)
            {
                //auto start = Clock::now();

                //init data for the fec_encode
                for (size_t i = 0; i < coding_k; i++)
                    tx.fec_src_packet_ptrs[i] = tx.block_packets[i]->data.data() + m_payload_offset;

                size_t fec_count = coding_n - coding_k;
                tx.block_fec_packets.resize(fec_count);
                for (size_t i = 0; i < fec_count; i++)
                {
                    tx.block_fec_packets[i] = tx.packet_pool.acquire();
                    tx.block_fec_packets[i]->data.resize(tx.transport_packet_size);
                    tx.fec_dst_packet_ptrs[i] = tx.block_fec_packets[i]->data.data() + m_payload_offset;
                }

                //encode
                fec_encode(tx.fec, tx.fec_src_packet_ptrs.data(), tx.fec_dst_packet_ptrs.data(), BLOCK_NUMS + coding_k, coding_n - coding_k, tx.payload_size);

                //seal the result
                for (size_t i = 0; i < fec_count; i++)
                {
                    seal_packet(*tx.block_fec_packets[i], m_packet_header_offset, tx.last_block_index, coding_k + i);
                    tx.ready_packet_queue.push_back(tx.block_fec_packets[i]); //ready to send
                }

                //LOGI("Encoded fec: {}", Clock::now() - start);
            }

            tx.block_packets.clear();
            tx.block_fec_packets.clear();
            tx.last_block_index++;
        }

        while (!tx.ready_packet_queue.empty())
        {
            TX::Packet_ptr packet = tx.ready_packet_queue.front();
            tx.ready_packet_queue.pop_front();

            std::lock_guard<std::mutex> lg(tx.pcap->mutex);

            int isize = static_cast<int>(packet->data.size());
            int r = pcap_inject(tx.pcap->pcap, packet->data.data(), isize);
            //                    if (r <= 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            //                    {
            //                        break;
            //                    }
            //                    else
            {
                if (r <= 0)
                {
                    LOGW("Trouble injecting packet: {} / {}: {}", r, isize, pcap_geterr(tx.pcap->pcap));
                    //result = Result::ERROR;
                }
                if (r > 0 && r != isize)
                {
                    LOGW("Incomplete packet sent: {} / {}", r, isize);
                    //result = Result::ERROR;
                }
            }
        }

        {
            //std::this_thread::sleep_for(std::chrono::milliseconds(1));
            //std::this_thread::yield();

#ifdef DEBUG_THROUGHPUT
            {
                static size_t xxx_data = 0;
                static size_t xxx_real_data = 0;
                static std::chrono::system_clock::time_point xxx_last_tp = std::chrono::system_clock::now();
                xxx_data += tx_buffer.size();
                xxx_real_data += MAX_USER_PACKET_SIZE;
                auto now = std::chrono::system_clock::now();
                if (now - xxx_last_tp >= std::chrono::seconds(1))
                {
                    float r = std::chrono::duration<float>(now - xxx_last_tp).count();
                    LOGI("Sent: {} KB/s / {} KB/s", float(xxx_data) / r / 1024.f, float(xxx_real_data) / r / 1024.f);
                    xxx_data = 0;
                    xxx_real_data = 0;
                    xxx_last_tp = now;
                }
            }
#endif
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

void Comms::send(void const* _data, size_t size, bool flush)
{
    TX& tx = m_impl->tx;

    TX::Packet_ptr& packet = tx.crt_packet;

    uint8_t const* data = reinterpret_cast<uint8_t const*>(_data);

    while (size > 0)
    {
        if (!packet)
            packet = tx.packet_pool.acquire();

        size_t s = std::min(size, tx.transport_packet_size - packet->data.size());
        size_t offset = packet->data.size();
        packet->data.resize(offset + s);
        memcpy(packet->data.data() + offset, data, s);
        data += s;
        size -= s;

        if (packet->data.size() >= tx.transport_packet_size || flush)
        {
            if (packet->data.size() < tx.transport_packet_size)
                packet->data.resize(tx.transport_packet_size);

            //send the current packet
            {
                std::unique_lock<std::mutex> lg(tx.packet_queue_mutex);
                tx.packet_queue.push_back(packet);
            }
            packet = tx.packet_pool.acquire();

            tx.packet_queue_cv.notify_all();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

size_t Comms::get_data_rate() const
{
    return m_data_stats_rate;
}

////////////////////////////////////////////////////////////////////////////////////////////

int Comms::get_input_dBm() const
{
    return m_latched_input_dBm;
}

////////////////////////////////////////////////////////////////////////////////////////////

void Comms::process_rx_packets()
{
    RX& rx = m_impl->rx;
    uint32_t coding_k = m_rx_descriptor.coding_k;
    uint32_t coding_n = m_rx_descriptor.coding_n;

    std::lock_guard<std::mutex> lg(rx.block_queue_mutex);

    if (Clock::now() - rx.last_packet_tp > m_rx_descriptor.reset_duration)
        rx.next_block_index = 0;

    while (!rx.block_queue.empty())
    {
        RX::Block_ptr block = rx.block_queue.front();

        //try to process consecutive packets before the block is finished to minimize latency
        for (size_t i = 0; i < block->packets.size(); i++)
        {
            RX::Packet_ptr const& d = block->packets[i];
            if (d->index == i)
            {
                if (!d->is_processed)
                {
                    //LOGI("Packet {}", block->index * coding_k + d->index);
                    m_data_stats_data_accumulated += d->data.size();
                    {
                        std::lock_guard<std::mutex> lg2(rx.ready_packet_queue_mutex);   
                        rx.ready_packet_queue.push_back(d);
                    }
                    rx.last_packet_tp = Clock::now();
                    d->is_processed = true;
                }
            }
            else
                break;
        }

        //entire block received
        if (block->packets.size() >= coding_k)
        {
            rx.last_block_tp = Clock::now();
            rx.next_block_index = block->index + 1;
            rx.block_queue.pop_front();
            continue; //next packet
        }

        //can we fec decode?
        if (block->packets.size() + block->fec_packets.size() >= coding_k)
        {
            //auto start = Clock::now();

            std::array<unsigned int, 32> indices;
            size_t primary_index = 0;
            size_t used_fec_index = 0;
            for (size_t i = 0; i < coding_k; i++)
            {
                if (primary_index < block->packets.size() && i == block->packets[primary_index]->index)
                {
                    rx.fec_src_packet_ptrs[i] = block->packets[primary_index]->data.data();
                    indices[i] = block->packets[primary_index]->index;
                    primary_index++;
                }
                else
                {
                    rx.fec_src_packet_ptrs[i] = block->fec_packets[used_fec_index]->data.data();
                    indices[i] = block->fec_packets[used_fec_index]->index;
                    used_fec_index++;
                }
            }

            //insert the missing packets, they will be filled with data by the fec_decode below
            size_t fec_index = 0;
            for (size_t i = 0; i < coding_k; i++)
            {
                if (i >= block->packets.size() || i != block->packets[i]->index)
                {
                    block->packets.insert(block->packets.begin() + i, rx.packet_pool.acquire());
                    block->packets[i]->data.resize(rx.payload_size);
                    block->packets[i]->index = i;
                    rx.fec_dst_packet_ptrs[fec_index++] = block->packets[i]->data.data();
                }
            }

            fec_decode(rx.fec, rx.fec_src_packet_ptrs.data(), rx.fec_dst_packet_ptrs.data(), indices.data(), rx.payload_size);

            //now dispatch them
            for (size_t i = 0; i < block->packets.size(); i++)
            {
                RX::Packet_ptr const& d = block->packets[i];
                if (!d->is_processed)
                {
                    //LOGI("Packet F {}", block->index * coding_k + d->index);
                    m_data_stats_data_accumulated += d->data.size();
                    {
                        std::lock_guard<std::mutex> lg2(rx.ready_packet_queue_mutex);   
                        rx.ready_packet_queue.push_back(d);
                    }
                    rx.last_packet_tp = Clock::now();
                    d->is_processed = true;
                }
            }

            //LOGI("Decoded fac: {}", Clock::now() - start);

            rx.last_block_tp = Clock::now();
            rx.next_block_index = block->index + 1;
            rx.block_queue.pop_front();
            continue; //next packet
        }

        //skip if too much buffering
        if (rx.block_queue.size() > 3)
        {
            for (size_t i = 0; i < block->packets.size(); i++)
            {
                RX::Packet_ptr const& d = block->packets[i];
                if (!d->is_processed)
                    ;//LOGI("Skipping {}", block->index * coding_k + d->index);
            }
            rx.next_block_index = block->index + 1;
            rx.block_queue.pop_front();
        }

        //crt packet is not complete, stop until we get more packets
        break;
    }
}

bool Comms::receive(void* data, size_t& size)
{
    RX& rx = m_impl->rx;
    
    std::lock_guard<std::mutex> lg2(rx.ready_packet_queue_mutex);

    if (rx.ready_packet_queue.empty())
        return false;

    auto d = rx.ready_packet_queue.front();
    size = d->data.size();
    if (size > 0)
        memcpy(data, d->data.data(), d->data.size());

    rx.ready_packet_queue.pop_front();
    return true;
}

void Comms::process()
{
    if (!m_impl)
        return;

    process_rx_packets();

    if (m_best_input_dBm != std::numeric_limits<int>::lowest())
        m_latched_input_dBm = m_best_input_dBm.load();
    m_best_input_dBm = std::numeric_limits<int>::lowest();

    Clock::time_point now = Clock::now();

    RX& rx = m_impl->rx;
    if (now - rx.last_block_tp > std::chrono::seconds(2))
        m_latched_input_dBm = 0;

    if (now - m_data_stats_last_tp >= std::chrono::seconds(1))
    {
        float d = std::chrono::duration<float>(now - m_data_stats_last_tp).count();
        m_data_stats_rate = static_cast<size_t>(static_cast<float>(m_data_stats_data_accumulated) / d);
        m_data_stats_data_accumulated = 0;
        m_data_stats_last_tp = now;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////
