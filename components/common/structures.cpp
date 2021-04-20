#include "structures.h"

SemaphoreHandle_t s_wlan_incoming_mux = xSemaphoreCreateBinary();
SemaphoreHandle_t s_wlan_outgoing_mux = xSemaphoreCreateBinary();

auto _init_result = []() -> bool
{
  xSemaphoreGive(s_wlan_incoming_mux);
  xSemaphoreGive(s_wlan_outgoing_mux);
  return true;
}();


uint8_t* s_wlan_outgoing_buffer = nullptr;
uint8_t* s_wlan_incoming_buffer = nullptr;

Queue s_wlan_incoming_queue;
Queue s_wlan_outgoing_queue;

bool init_queues(size_t incoming_queue_size, size_t outgoing_queue_size)
{
  s_wlan_outgoing_buffer = new uint8_t[outgoing_queue_size];
  s_wlan_incoming_buffer = new uint8_t[incoming_queue_size];

  s_wlan_outgoing_queue.init(s_wlan_outgoing_buffer, outgoing_queue_size);
  s_wlan_incoming_queue.init(s_wlan_incoming_buffer, incoming_queue_size);

  return true;
}

////////////////////////////////////////////////////////////////////////////////////

IRAM_ATTR bool start_writing_wlan_outgoing_packet(Wlan_Outgoing_Packet& packet, size_t size)
{
  size_t real_size = WLAN_IEEE_HEADER_SIZE + size;
  uint8_t* buffer = s_wlan_outgoing_queue.start_writing(real_size);
  if (!buffer)
  {
    packet.ptr = nullptr;
    return false;
  }
  packet.offset = 0;
  packet.size = size;
  packet.ptr = buffer;
  packet.payload_ptr = buffer + WLAN_IEEE_HEADER_SIZE;
  return true;
}
IRAM_ATTR void end_writing_wlan_outgoing_packet(Wlan_Outgoing_Packet& packet)
{
  s_wlan_outgoing_queue.end_writing();
  packet.ptr = nullptr;
}
IRAM_ATTR void cancel_writing_wlan_outgoing_packet(Wlan_Outgoing_Packet& packet)
{
  s_wlan_outgoing_queue.cancel_writing();
  packet.ptr = nullptr;
}

IRAM_ATTR bool start_reading_wlan_outgoing_packet(Wlan_Outgoing_Packet& packet)
{
  size_t real_size = 0;
  uint8_t* buffer = s_wlan_outgoing_queue.start_reading(real_size);
  if (!buffer)
  {
    packet.ptr = nullptr;
    return false;
  }
  packet.offset = 0;
  packet.size = real_size - WLAN_IEEE_HEADER_SIZE;
  packet.ptr = buffer;
  packet.payload_ptr = buffer + WLAN_IEEE_HEADER_SIZE;
  return true;
}
IRAM_ATTR void end_reading_wlan_outgoing_packet(Wlan_Outgoing_Packet& packet)
{
  s_wlan_outgoing_queue.end_reading();
  packet.ptr = nullptr;
}
IRAM_ATTR void cancel_reading_wlan_outgoing_packet(Wlan_Outgoing_Packet& packet)
{
  s_wlan_outgoing_queue.cancel_reading();
  packet.ptr = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////

IRAM_ATTR bool start_writing_wlan_incoming_packet(Wlan_Incoming_Packet& packet, size_t size)
{
  uint8_t* buffer = s_wlan_incoming_queue.start_writing(size);
  if (!buffer)
  {
    packet.ptr = nullptr;
    return false;
  }
  packet.offset = 0;
  packet.size = size;
  packet.ptr = buffer;
  return true;
}
IRAM_ATTR void end_writing_wlan_incoming_packet(Wlan_Incoming_Packet& packet)
{
  s_wlan_incoming_queue.end_writing();
  packet.ptr = nullptr;
}
IRAM_ATTR void cancel_writing_wlan_incoming_packet(Wlan_Incoming_Packet& packet)
{
  s_wlan_incoming_queue.cancel_writing();
  packet.ptr = nullptr;
}

IRAM_ATTR bool start_reading_wlan_incoming_packet(Wlan_Incoming_Packet& packet)
{
  size_t size = 0;
  uint8_t* buffer = s_wlan_incoming_queue.start_reading(size);
  if (!buffer)
  {
    packet.ptr = nullptr;
    return false;
  }
  packet.offset = 0;
  packet.size = size;
  packet.ptr = buffer;
  return true;
}
IRAM_ATTR void end_reading_wlan_incoming_packet(Wlan_Incoming_Packet& packet)
{
  s_wlan_incoming_queue.end_reading();
  packet.ptr = nullptr;
}
IRAM_ATTR void cancel_reading_wlan_incoming_packet(Wlan_Incoming_Packet& packet)
{
  s_wlan_incoming_queue.cancel_reading();
  packet.ptr = nullptr;
}
