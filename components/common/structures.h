#pragma once

#include <cassert>
#include <cstring>
#include <cstdint>

constexpr uint8_t WLAN_IEEE_HEADER_AIR2GROUND[] =
{
    0x08, 0x01, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    0x10, 0x86
};
constexpr uint8_t WLAN_IEEE_HEADER_GROUND2AIR[] =
{
    0x08, 0x01, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
    0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
    0x10, 0x86
};

constexpr size_t WLAN_IEEE_HEADER_SIZE = sizeof(WLAN_IEEE_HEADER_AIR2GROUND);
constexpr size_t WLAN_MAX_PACKET_SIZE = 1500;
constexpr size_t WLAN_MAX_PAYLOAD_SIZE = WLAN_MAX_PACKET_SIZE - WLAN_IEEE_HEADER_SIZE;

static_assert(WLAN_IEEE_HEADER_SIZE == 24, "");

struct Wlan_Outgoing_Packet
{
  uint8_t* ptr = nullptr;
  uint8_t* payload_ptr = nullptr;
  uint16_t size = 0;
  uint16_t offset = 0;
};

struct Wlan_Incoming_Packet
{
  uint8_t* ptr = nullptr;
  uint16_t size = 0;
  uint16_t offset = 0;
};

////////////////////////////////////////////////////////////////////////////////////


