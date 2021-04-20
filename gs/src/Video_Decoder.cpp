#include "Video_Decoder.h"
#include "Log.h"

#include <vector>
#include <deque>
#include <mutex>
#include <memory>
#include <thread>
#include <condition_variable>
#include "fmt/format.h"
#include "Clock.h"
#include "Pool.h"
#include "IHAL.h"
#include <SDL2/SDL.h>


extern "C"
{
//#include <jpeglib.h>
#include <turbojpeg.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
}

#define CHECK_GL_ERRORS

#if defined(CHECK_GL_ERRORS)
#define GLCHK(X) \
do { \
    GLenum err = GL_NO_ERROR; \
    X; \
   while ((err = glGetError())) \
   { \
      LOGE("GL error {} in " #X "file {} line {}", err, __FILE__,__LINE__); \
   } \
} while(0)
#define SDLCHK(X) \
do { \
    int err = X; \
    if (err != 0) LOGE("SDL error {} in " #X "file {} line {}", err, __FILE__,__LINE__); \
} while (0)
#else
#define GLCHK(X) X
#define EGLCHK()
#endif

struct Input
{
    std::vector<uint8_t> data;
};
using Input_ptr = Pool<Input>::Ptr;

struct Output
{
    std::vector<uint32_t> pbos;
    std::vector<uint32_t> textures;
    uint32_t width = 0;
    uint32_t height = 0;
    std::array<std::vector<uint8_t>, 3> planes;
};
using Output_ptr = Pool<Output>::Ptr;

struct Video_Decoder::Impl
{
    SDL_Window* window = nullptr;
    SDL_GLContext context;
    std::vector<std::thread> threads;

    Pool<Input> input_pool;

    std::mutex input_queue_mutex;
    std::deque<Input_ptr> input_queue;
    std::condition_variable input_queue_cv;

    ///

    Pool<Output> output_pool;

    std::mutex output_queue_mutex;
    std::deque<Output_ptr> output_queue;

    std::deque<Output_ptr> locked_outputs;
};

Video_Decoder::Video_Decoder()
    : m_impl(new Impl)
{
}

Video_Decoder::~Video_Decoder()
{
    {
        std::unique_lock<std::mutex> lg(m_impl->input_queue_mutex);
        m_exit = true;
    }
    m_impl->input_queue_cv.notify_all();

    for (auto& t: m_impl->threads)
        if (t.joinable())
            t.join();

    for (auto& t: m_textures)
    {
        if (t != 0)
            glDeleteTextures(1, &t);
    }
}

bool Video_Decoder::init(IHAL& hal)
{
    m_hal = &hal;

    m_impl->window = (SDL_Window*)hal.get_window();
    assert(m_impl->window != nullptr);
    m_impl->context = SDL_GL_CreateContext(m_impl->window);
    assert(m_impl->context != nullptr);

    m_impl->output_pool.on_acquire = [this](Output& output) 
    {
        if (output.textures.empty())
        {
            //m_hal->lock_main_context();
            output.pbos.resize(3);
            GLCHK(glGenBuffers(output.pbos.size(), output.pbos.data()));

            output.textures.resize(3);
            GLCHK(glGenTextures(output.textures.size(), output.textures.data()));
            for (auto& t: output.textures)
            {
                GLCHK(glBindTexture(GL_TEXTURE_2D, t));
                GLCHK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
                GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
                GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
                GLCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
                GLCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
                LOGI("Texture: {}", t);
            }
            //m_hal->unlock_main_context();
        }
    };

    for (size_t i = 0; i < 2; i++)
        m_impl->threads.push_back(std::thread([this]() { decoder_thread_proc(); }));

    return true;
}

uint32_t Video_Decoder::get_video_texture_id(size_t channel) const
{
    return m_textures[channel];
}
ImVec2 Video_Decoder::get_video_resolution() const
{
    return m_resolution;
}

bool Video_Decoder::decode_data(void const* data, size_t size)
{
    if (!data || size == 0)
        return false;
        
    Input_ptr input = m_impl->input_pool.acquire();
    input->data.resize(size);
    memcpy(input->data.data(), data, size);

    {
        std::unique_lock<std::mutex> lg(m_impl->input_queue_mutex);
        m_impl->input_queue.push_back(input);
    }

    m_impl->input_queue_cv.notify_all();

    return true;
}

void Video_Decoder::decoder_thread_proc()
{
    LOGI("SDL window: {}", (size_t)m_impl->window);
    SDLCHK(SDL_GL_MakeCurrent(m_impl->window, m_impl->context));

    while (!m_exit)
    {
        Input_ptr input;
        {
            std::unique_lock<std::mutex> lg(m_impl->input_queue_mutex);
            if (m_impl->input_queue.empty())
                m_impl->input_queue_cv.wait(lg, [this] { return m_impl->input_queue.empty() == false || m_exit == true; });

            if (m_exit)
                break;

            if (!m_impl->input_queue.empty())
            {
                input = m_impl->input_queue.back();
                m_impl->input_queue.clear();
            }
            else
                continue;
        }

        const uint8_t* data = (const uint8_t*)input->data.data();
        size_t size = input->data.size();

        //find the end marker for JPEG. Data after that can be discarded
        const uint8_t* dptr = &data[size - 4];
        while (dptr > data)
        {
            if (dptr[0] == 0xFF && dptr[1] == 0xD9 && dptr[2] == 0x00 && dptr[3] == 0x00)
            {
                dptr += 2;
                size = dptr - data;
                if ((size & 0x1FF) == 0)
                    size += 1; 
                if ((size % 100) == 0)
                    size += 1;
                break;
            }
            dptr--;
        }

        //LOGI("In size = {}, correct size = {}", _size, size);

        auto start_tp = Clock::now();

        int width, height;
        int inSubsamp, inColorspace;

        tjhandle tjInstance = tjInitDecompress();

        if (tjDecompressHeader3(tjInstance, data, size, &width, &height, &inSubsamp, &inColorspace) < 0)
        {
            tjDestroy(tjInstance);
            LOGE("Jpeg header error: {}", tjGetErrorStr());
            continue;
        }

        std::array<uint8_t*, 3> planesPtr;

        Output_ptr output = m_impl->output_pool.acquire();

        for (size_t i = 0; i < output->planes.size(); i++)
        {
            output->planes[i].resize(tjPlaneSizeYUV(i, width, 0, height, inSubsamp));
            planesPtr[i] = output->planes[i].data();
        }
        output->width = width;
        output->height = height;

        int flags = TJ_FASTUPSAMPLE | TJFLAG_FASTDCT;
        if (tjDecompressToYUVPlanes(tjInstance, data, size, planesPtr.data(), 0, nullptr, 0, flags) < 0)
        {
            //tjDestroy(m_impl->tjInstance);
            LOGE("decompressing JPEG image: {}", tjGetErrorStr());
            //return false;
        }
        
        tjDestroy(tjInstance);


        //m_hal->lock_main_context();
/*
        for (size_t i = 0; i < output->pbos.size(); i++)
        {
            auto& b = output->pbos[i];
            GLCHK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, b));
            GLCHK(glBufferData(GL_PIXEL_UNPACK_BUFFER, output->planes[i].size(), output->planes[i].data(), GL_STREAM_DRAW));
        }
        GLCHK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0));
//*/

/*
        for (size_t i = 0; i < output->textures.size(); i++)
        {
            GLCHK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, output->pbos[i]));
            GLCHK(glBufferData(GL_PIXEL_UNPACK_BUFFER, output->planes[i].size(), output->planes[i].data(), GL_STREAM_DRAW));
            GLCHK(glBindTexture(GL_TEXTURE_2D, output->textures[i]));
            uint32_t width = i == 0 ? output->width : output->width / 2;
            uint32_t height = output->height;
            GLCHK(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, 0));
        }
        GLCHK(glFlush()); //to publish the changes
//*/
        //m_hal->unlock_main_context();

        {
            std::lock_guard<std::mutex> lg(m_impl->output_queue_mutex);
            m_impl->output_queue.push_back(std::move(output));
        }

        //LOGI("Decompressed in {}us, {}", std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_tp).count(), input->data.size() - size);
    }
}

size_t Video_Decoder::lock_output()
{
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lg(m_impl->output_queue_mutex);
        count = m_impl->output_queue.size();
        if (count == 0)
            return 0;

        m_impl->locked_outputs.push_back(m_impl->output_queue.back());
        m_impl->output_queue.clear();
    }

    Output& output = *m_impl->locked_outputs.back();
    
    //for (size_t i = 0; i < output->pbos.size(); i++)
    {
        //auto& b = output->pbos[i];
        //GLCHK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, b));
        //GLCHK(glBufferData(GL_PIXEL_UNPACK_BUFFER, output->planes[i].size(), output->planes[i].data(), GL_STREAM_DRAW));
    }
    //GLCHK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0));
    //GLCHK(glFlush()); //to publish the changes
//*
    for (size_t i = 0; i < output.textures.size(); i++)
    {
        GLCHK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, output.pbos[i]));
        GLCHK(glBufferData(GL_PIXEL_UNPACK_BUFFER, output.planes[i].size(), output.planes[i].data(), GL_STREAM_DRAW));
        GLCHK(glBindTexture(GL_TEXTURE_2D, output.textures[i]));
        uint32_t width = i == 0 ? output.width : output.width / 2;
        uint32_t height = output.height;
        GLCHK(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, 0));
    }
    GLCHK(glFlush()); //to publish the changes
    //GLCHK(glFinish()); //to publish the changes
    GLCHK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0));
//*/    
    std::copy(output.textures.begin(), output.textures.end(), m_textures.begin());
    m_resolution = ImVec2((float)output.width, (float)output.height);

    return count;
}
bool Video_Decoder::unlock_output()
{
    if (m_impl->locked_outputs.empty())
        return false;

    while (m_impl->locked_outputs.size() > 4)
        m_impl->locked_outputs.pop_front();

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

