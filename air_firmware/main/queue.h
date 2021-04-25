#pragma once

#include <cassert>
#include <cstring>
#include "structures.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#else
#define IRAM_ATTR
#endif

/////////////////////////////////////////////////////////////////////////

struct Queue
{
  void init(uint8_t* buffer, size_t size)
  {
    m_buffer = buffer;
    m_capacity = size;
  }

  IRAM_ATTR inline size_t count() const
  {
    return m_count;
  }

  IRAM_ATTR inline size_t next_reading_size()
  {
    if (m_read_start != m_read_end)
      return 0;

    if (m_read_start == m_write_start)
      return 0;

    size_t size;
    memcpy(&size, m_buffer + m_read_start, sizeof(uint32_t)); //read the size
    return size;
  }

  IRAM_ATTR inline size_t size() const
  {
    if (m_read_start == m_write_start)
      return 0;

    if (m_write_start > m_read_start) //no wrap
      return m_write_end - m_read_start;

    if (m_write_start < m_read_start) //wrap
      return (m_capacity - m_read_start) + m_write_end;

    return 0;
  }

  IRAM_ATTR inline size_t capacity() const
  {
    return m_capacity;
  }

  IRAM_ATTR inline uint8_t* start_writing(size_t size) __attribute__((always_inline))
  {
    if (m_write_start != m_write_end)
      return nullptr;
    
    //size_t aligned_size = (size + (CHUNK_SIZE - 1)) & ~CHUNK_MASK; //align the size
    size_t end = m_write_start + sizeof(uint32_t) + size;
    if (end <= m_capacity) //no wrap
    {
      //check read collisions
      if (m_write_start < m_read_start && end >= m_read_start)
      {
//        Serial.printf("\tf1: %d < %d && %d >= %d\n", start, m_read_start, end, m_read_start);
        return nullptr;
      }

//      Serial.printf("\tw1: %d, %d, %d, %d\n", start, end, m_read_start, m_read_end);
      memcpy(m_buffer + m_write_start, &size, sizeof(uint32_t)); //write the size before wrapping
      m_write_end = end;
      return m_buffer + m_write_start + sizeof(uint32_t);
    }
    else //wrap
    {
      //check read collisions
      if (m_read_start > m_write_start) //if the read offset is between start and the end of the buffer
      {
//        Serial.printf("\tf2: %d > %d\n", m_read_start, start);
        return nullptr;
      }
      end = size;
      //check read collisions
      if (end >= m_read_start)
      {
//        Serial.printf("\tf3: %d >= %d\n", end, m_read_start);
        return nullptr;
      }

//      Serial.printf("\tw2: %d, %d, %d, %d\n", start, end, m_read_start, m_read_end);
      memcpy(m_buffer + m_write_start, &size, sizeof(uint32_t)); //write the size before wrapping
      m_write_end = end;
      return m_buffer;
    }
  }

  IRAM_ATTR inline void end_writing() __attribute__((always_inline))
  {
    m_write_start = m_write_end;
    m_count++;
  }
  IRAM_ATTR inline void cancel_writing() __attribute__((always_inline))
  {
    m_write_end = m_write_start;
  }

  IRAM_ATTR inline uint8_t* start_reading(size_t& size) __attribute__((always_inline))
  {
    if (m_read_start != m_read_end)
      return nullptr;

    if (m_read_start == m_write_start)
    {
//      Serial.printf("\tf4: %d == %d\n", m_read_start, m_write_start);
      size = 0;
      return nullptr;
    }
    size_t start = m_read_start;
    memcpy(&size, m_buffer + start, sizeof(uint32_t)); //read the size
    //size_t aligned_size = (size + (CHUNK_SIZE - 1)) & ~CHUNK_MASK; //align the size
    size_t end = start + sizeof(uint32_t) + size;
    if (end <= m_capacity)
    {
      m_read_end = end;
      return m_buffer + start + sizeof(uint32_t);
    }
    else
    {
      //m_read_start = 0;
      m_read_end = size;
      return m_buffer;
    }
  }

  IRAM_ATTR inline void end_reading() __attribute__((always_inline))
  {
    m_read_start = m_read_end;
    assert(m_count > 0);
    m_count--;
  }
  IRAM_ATTR inline void cancel_reading()  __attribute__((always_inline))
  {
    m_read_end = m_read_start;
  }
  
private:
  uint8_t* m_buffer = nullptr;
  size_t m_capacity = 0;
  size_t m_write_start = 0;
  size_t m_write_end = 0;
  size_t m_read_start = 0;
  size_t m_read_end = 0;
  size_t m_count = 0;
};

////////////////////////////////////////////////////////////////////////////////////
