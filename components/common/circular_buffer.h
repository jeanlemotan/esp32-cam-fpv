#pragma once

#include <cassert>
#include <cstring>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#else
#define IRAM_ATTR
#endif

class Circular_Buffer
{
public:
    Circular_Buffer(uint8_t* buffer, size_t size)
        : m_data(buffer)
        , m_capacity(size)
    {
    }
    
    IRAM_ATTR size_t size() const
    {
        return m_size;
    }
    IRAM_ATTR bool empty() const
    {
        return m_size == 0;
    }

    IRAM_ATTR size_t get_space_left() const
    {
        return m_capacity - m_size;
    }
    IRAM_ATTR size_t capacity() const
    {
        return m_capacity;
    }

    IRAM_ATTR void resize(size_t size)
    {
        assert(size < capacity());
        m_size = size;
    }

    IRAM_ATTR bool write(const void* data, size_t size)
    {
        if (size >= get_space_left())
            return false;

        size_t idx = (m_start + m_size) % m_capacity;

        if (idx + size <= m_capacity) //no wrap
            memcpy(m_data + idx, data, size);
        else //wrap
        {
            size_t first = m_capacity - idx;
            memcpy(m_data + idx, data, first);
            memcpy(m_data, (uint8_t*)data + first, size - first);
        }
        m_size += size;
        return true;
    }
    IRAM_ATTR bool read(void* dst, size_t size)
    {
        if (m_size < size)
            return false;

        if (m_start + size <= m_capacity) //no wrap around
        {
            memcpy(dst, m_data + m_start, size);
            m_start = (m_start + size) % m_capacity;
            m_size -= size;
            return true;
        }

        //wrap around, 2 steps
        size_t first = m_capacity - m_start;
        memcpy(dst, m_data + m_start, first);
        memcpy((uint8_t*)dst + first, m_data, size - (first));
        m_start = (m_start + size) % m_capacity;
        m_size -= size;
        return true;
    }
    IRAM_ATTR const void* start_reading(size_t& size)
    {
        if (m_size == 0)
        {
            size = 0;
            return nullptr;
        }

        if (size > m_size) //clamp to the actual size
            size = m_size;

        if (m_start + size > m_capacity) //wrap around
            size = m_capacity - m_start;
        
        return m_data + m_start;
    }
    IRAM_ATTR void end_reading(size_t size) //call with the same size as the one returned by start_reading
    {
        if (size == 0)
            return;

        assert(size <= m_size);

        m_start = (m_start + size) % m_capacity;
        m_size -= size;
    }
    IRAM_ATTR void clear()
    {
        m_start = 0;
        m_size = 0;
    }

private:
    uint8_t* m_data;
    size_t m_capacity;
    size_t m_start = 0;
    size_t m_size = 0;
};
