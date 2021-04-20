#include "crc.h"

static uint8_t s_crc8_table[256]; /* 8-bit table */
void init_crc8_table()
{
    static constexpr uint8_t DI = 0x07;
    for (uint16_t i = 0; i < 256; i++)
    {
        uint8_t crc = (uint8_t)i;
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc << 1) ^ ((crc & 0x80) ? DI : 0);
        s_crc8_table[i] = crc & 0xFF;
    }
}

IRAM_ATTR uint8_t crc8(uint8_t crc, const void *c_ptr, size_t len)
{
    const uint8_t *c = reinterpret_cast<const uint8_t *>(c_ptr);
    size_t n = (len + 7) >> 3;
    switch (len & 7)
    {
    case 0:
        do
        {
            crc = s_crc8_table[crc ^ (*c++)];
        case 7:
            crc = s_crc8_table[crc ^ (*c++)];
        case 6:
            crc = s_crc8_table[crc ^ (*c++)];
        case 5:
            crc = s_crc8_table[crc ^ (*c++)];
        case 4:
            crc = s_crc8_table[crc ^ (*c++)];
        case 3:
            crc = s_crc8_table[crc ^ (*c++)];
        case 2:
            crc = s_crc8_table[crc ^ (*c++)];
        case 1:
            crc = s_crc8_table[crc ^ (*c++)];
        } while (--n > 0);
    }
    return crc;
}