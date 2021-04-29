#pragma once

#include <memory>
#include "imgui.h"

class IHAL;

class Video_Decoder
{
public:
    Video_Decoder();
    ~Video_Decoder();

    bool decode_data(void const* data, size_t size);
    void inject_test_data(uint32_t value);

    bool init(IHAL& hal);

    size_t lock_output();
    uint32_t get_video_texture_id(size_t component) const;;
    ImVec2 get_video_resolution() const;
    bool unlock_output();


    struct Impl;

private:
    void decoder_thread_proc(size_t thread_index);

    IHAL* m_hal = nullptr;
    bool m_exit = false;
    ImVec2 m_resolution;
    std::array<uint32_t, 3> m_textures;
    std::unique_ptr<Impl> m_impl;
};
