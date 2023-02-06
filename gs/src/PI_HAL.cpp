#include "PI_HAL.h"
#include "Log.h"

#define USE_SDL
//#define USE_MANGA_SCREEN2

//#define USE_BOUBLE_BUFFER

#include <fstream>
#include <future>
#include <atomic>
#include <mutex>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "Clock.h"

#ifdef USE_MANGA_SCREEN2
#include <tslib.h> //needs libts-dev 
#endif


#ifdef USE_SDL
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#include "imgui_impl_sdl.h"
#else
extern "C"
{
#include "interface/vcos/vcos.h"
#include <bcm_host.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_brcm.h>
}
#endif

#ifdef TEST_LATENCY
extern "C"
{
#include "pigpio.h"
}
#endif

extern uint8_t s_font_droid_sans[];

/* To install & compile SDL2 with DRM:

--- Install dependencies

sudo apt build-dep libsdl2
sudo apt install libdrm-dev libgbm-dev

--- Build SDL2:
git clone SDL2
cd SDL
mkdir build
cd build 
../configure --disable-video-rpi --enable-video-kmsdrm --enable-video-x11 --disable-video-opengl
make -j5
sudo make install

--- Run:
sudo -E LD_LIBRARY_PATH=/usr/local/lib DISPLAY=:0 ./gs
*/

///////////////////////////////////////////////////////////////////////////////////////////////////

struct PI_HAL::Impl
{
    uint32_t width = 1920;
    uint32_t height = 1080;

    std::mutex context_mutex;

#ifdef USE_SDL
    SDL_Window* window = nullptr;
    SDL_GLContext context;
#else
    EGLDisplay display = nullptr;
    EGLSurface surface = nullptr;
    EGLContext context = nullptr;
#endif

#ifdef USE_MANGA_SCREEN2
    tsdev* ts = nullptr;
#endif

    constexpr static size_t MAX_TOUCHES = 3;
    constexpr static size_t MAX_SAMPLES = 10;

#ifdef USE_MANGA_SCREEN2
    ts_sample_mt** ts_samples;
#endif

    struct Touch
    {
        int id = 0;
        int32_t x = 0;
        int32_t y = 0;
        bool is_pressed = false;
    };
    std::array<Touch, MAX_TOUCHES> touches;

    bool pigpio_is_isitialized = false;
    float target_backlight = 1.0f;
    float backlight = 0.0f;

#ifdef USE_MANGA_SCREEN2
    std::future<void> backlight_future;
    std::atomic_bool backlight_future_cancelled;
    Clock::time_point backlight_tp = Clock::now();
    std::shared_ptr<FILE> backlight_uart;
#endif
};

///////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef TEST_LATENCY

bool PI_HAL::init_pigpio()
{
    if (m_impl->pigpio_is_isitialized)
        return true;

    LOGI("Initializing pigpio");
    if (gpioCfgClock(2, PI_CLOCK_PCM, 0) < 0 ||
            gpioCfgPermissions(static_cast<uint64_t>(-1)))
    {
        LOGE("Cannot configure pigpio");
        return false;
    }
    if (gpioInitialise() < 0)
    {
        LOGE("Cannot init pigpio");
        return false;
    }

    m_impl->pigpio_is_isitialized = true;

    return true;
}

// ///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::shutdown_pigpio()
{
    if (m_impl->pigpio_is_isitialized)
        gpioTerminate();

    m_impl->pigpio_is_isitialized = false;
}
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////

bool PI_HAL::init_display_dispmanx()
{
#ifndef USE_SDL
    static const EGLint attribute_list[] =
    {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_BUFFER_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
    };
    static const EGLint context_attributes[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    }; 

    EGLConfig config;

    // get an EGL display connection
    m_impl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(m_impl->display != EGL_NO_DISPLAY);

    // init the EGL display connection
    EGLBoolean result = eglInitialize(m_impl->display, NULL, NULL);
    assert(result != EGL_FALSE);

    // get an appropriate EGL frame buffer configuration
    EGLint num_config;
    result = eglChooseConfig(m_impl->display, attribute_list, &config, 1, &num_config);
    assert(EGL_FALSE != result);

    result = eglBindAPI(EGL_OPENGL_ES_API);
    assert(result != EGL_FALSE);

    // create an EGL rendering context
    m_impl->context = eglCreateContext(m_impl->display, config, EGL_NO_CONTEXT, context_attributes);
    assert(m_impl->context!=EGL_NO_CONTEXT);

    // create an EGL window surface
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t success = graphics_get_display_size(0 /* LCD */, &width, &height);
    assert(success >= 0);

    VC_RECT_T dst_rect;
    vc_dispmanx_rect_set(&dst_rect, 0, 0, width, height);

    VC_RECT_T src_rect;
    vc_dispmanx_rect_set(&src_rect, 0, 0, (width<<16), (height<<16));

    DISPMANX_DISPLAY_HANDLE_T dispman_display = vc_dispmanx_display_open(0 /* LCD */);
    DISPMANX_UPDATE_HANDLE_T dispman_update = vc_dispmanx_update_start(0);

    VC_DISPMANX_ALPHA_T alpha;
    memset(&alpha, 0, sizeof(alpha));
    alpha.flags = (DISPMANX_FLAGS_ALPHA_T)(DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS);
    alpha.opacity = 255;
    alpha.mask = 0;

    DISPMANX_ELEMENT_HANDLE_T dispman_element = vc_dispmanx_element_add(dispman_update, 
                                                dispman_display,
                                                0/*layer*/, 
                                                &dst_rect, 
                                                0/*src*/,
                                                &src_rect, 
                                                DISPMANX_PROTECTION_NONE, 
                                                &alpha /*alpha*/, 
                                                0/*clamp*/, 
                                                DISPMANX_NO_ROTATE/*transform*/);

    static EGL_DISPMANX_WINDOW_T nativewindow;
    nativewindow.element = dispman_element;
    nativewindow.width = width;
    nativewindow.height = height;
    vc_dispmanx_update_submit_sync(dispman_update);

    m_impl->surface = eglCreateWindowSurface(m_impl->display, config, &nativewindow, NULL);
    assert(m_impl->surface != EGL_NO_SURFACE);

    // connect the context to the surface
    result = eglMakeCurrent(m_impl->display, m_impl->surface, m_impl->surface, m_impl->context);
    assert(EGL_FALSE != result);

    eglSwapInterval(m_impl->display, 0);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = m_impl->width;
    io.DisplaySize.y = m_impl->height;


#endif
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool PI_HAL::init_display_sdl()
{
    //SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

#ifdef USE_SDL
    SDL_Init(SDL_INIT_VIDEO);
#ifdef USE_BOUBLE_BUFFER
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#else
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
#endif

    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0); 
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0); 

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1); 

    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1", SDL_HINT_OVERRIDE);
    //SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, "0", SDL_HINT_OVERRIDE);
    

    int driver_count = SDL_GetNumVideoDrivers();
    LOGI("Drivers: {}", driver_count);
    for (int i = 0; i < driver_count; i++)
    {
        LOGI("Driver {}: {}", i, SDL_GetVideoDriver(i));
    }

    SDL_DisplayMode mode;
    int res = SDL_GetCurrentDisplayMode(0, &mode);
    assert(res == 0);

	m_impl->width = mode.w;
	m_impl->height = mode.h;
    LOGI("Mode {}: {}x{}", res, mode.w, mode.h);

    // Create an application window with the following settings:
    m_impl->window = SDL_CreateWindow(
        "ESP32 FPV",                  // window title
        0,           // initial x position
        0,           // initial y position
        m_impl->width,                               // width, in pixels
        m_impl->height,                               // height, in pixels
        SDL_WINDOW_FULLSCREEN | 
        SDL_WINDOW_OPENGL | 
        SDL_WINDOW_SHOWN | 
        SDL_WINDOW_BORDERLESS
    );

    // Check that the window was successfully created
    if (m_impl->window == nullptr) 
    {
        // In the case that the window could not be made...
        LOGE("Cannot create window: {}", SDL_GetError());
        return false;
    }

    m_impl->context = SDL_GL_CreateContext(m_impl->window);
    int err = SDL_GL_MakeCurrent(m_impl->window, m_impl->context);
    if (err != 0) 
        LOGE("Failed to create context: {}", err);

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(m_impl->window, m_impl->context);

    SDL_GL_SetSwapInterval(0);

    ImGui_ImplSDL2_SetMouseEnabled(true);

#endif
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool PI_HAL::init_display()
{
#ifdef USE_SDL
    if (!init_display_sdl())
        return false;
#else
    if (!init_display_dispmanx())
        return false;
#endif

#ifdef USE_MANGA_SCREEN2
    {
        m_impl->backlight_uart.reset(fopen("/dev/ttyACM0", "wb"), fclose);
        if (!m_impl->backlight_uart)
        {
            LOGW("Failed to initialize backlight uart");
            return false;
        }
    }
#endif

return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::shutdown_display_dispmanx()
{
#ifndef USE_SDL
    // clear screen
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(m_impl->display, m_impl->surface);

    // Release OpenGL resources
    eglMakeCurrent(m_impl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(m_impl->display, m_impl->surface);
    eglDestroyContext(m_impl->display, m_impl->context);
    eglTerminate(m_impl->display);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::shutdown_display_sdl()
{
#ifdef USE_SDL
    SDL_GL_DeleteContext(m_impl->context);
    SDL_DestroyWindow(m_impl->window);
    SDL_Quit();
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::shutdown_display()
{
#ifdef USE_SDL
    shutdown_display_sdl();
#else
    shutdown_display_dispmanx();
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool PI_HAL::update_display()
{
    //lock_main_context();

    glScissor(0, 0, m_impl->width, m_impl->height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

#ifdef USE_MANGA_SCREEN2
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData(), true);
#else
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData(), false);
#endif
    
#ifdef USE_SDL
    ImGuiIO& io = ImGui::GetIO();
    SDL_Event event;
    while (SDL_PollEvent(&event)) 
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
        switch (event.type) 
        {
            case SDL_FINGERMOTION:
            case SDL_FINGERDOWN:
            case SDL_FINGERUP:
            {
                SDL_TouchFingerEvent& ev = *(SDL_TouchFingerEvent*)&event;
                io.MousePos = ImVec2(ev.x * m_impl->width, ev.y * m_impl->height);
                io.MouseDown[0] = event.type == SDL_FINGERUP ? false : true;
            }
            break;
            case SDL_QUIT:
                break;
            case SDL_KEYUP:
                break;
        }
    }

    glFlush();
    SDL_GL_SwapWindow(m_impl->window);
    //SDL_GL_SwapWindow(m_impl->window);
    //SDL_GL_SwapWindow(m_impl->window);

    ImGui_ImplSDL2_NewFrame(m_impl->window);
#else
    eglSwapBuffers(m_impl->display, m_impl->surface);
#endif

    Clock::time_point now = Clock::now();
#ifdef USE_MANGA_SCREEN2
    if (m_impl->target_backlight != m_impl->backlight && now - m_impl->backlight_tp >= std::chrono::milliseconds(50))
    {
        m_impl->backlight = m_impl->target_backlight;
        m_impl->backlight_tp = now;

        float b = m_impl->target_backlight;
        std::atomic_bool& cancelled = m_impl->backlight_future_cancelled;

        cancelled = true;
        if (m_impl->backlight_future.valid())
            m_impl->backlight_future.wait();

        cancelled = false;

        m_impl->backlight_future = std::async(std::launch::async, [this, b, &cancelled]()
        {
            if (!cancelled)
            {
                std::string command = "backlight " + std::to_string((int)(b * 100.f + 0.5f)) + "\r";
                fwrite((const uint8_t*)command.data(), command.size(), 1, m_impl->backlight_uart.get());
            }
        });
    }
#endif

    //unlock_main_context();

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool PI_HAL::init_ts()
{
#ifdef USE_MANGA_SCREEN2
    m_impl->ts = ts_open("/dev/input/event0", 1);
    if (!m_impl->ts)
    {
        LOGE("ts_open failes");
        return false;
    }
    int res = ts_config(m_impl->ts);
    if (res < 0)
    {
        LOGE("ts_config failed");
        return false;
    }

    m_impl->ts_samples = new ts_sample_mt*[Impl::MAX_SAMPLES];
    for (size_t i = 0; i < Impl::MAX_SAMPLES; i++)
    {
        m_impl->ts_samples[i] = new ts_sample_mt[Impl::MAX_TOUCHES];
        memset(m_impl->ts_samples[i], 0, sizeof(ts_sample_mt));
    }
#endif

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::shutdown_ts()
{
#ifdef USE_MANGA_SCREEN2
    ts_close(m_impl->ts);
    m_impl->ts = nullptr;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::update_ts()
{
//    for (Impl::Touch& touch: m_impl->touches)
//    {
//        touch.is_pressed = false;
//    }

#ifdef USE_MANGA_SCREEN2
    int ret = ts_read_mt(m_impl->ts, (ts_sample_mt**)m_impl->ts_samples, Impl::MAX_TOUCHES, Impl::MAX_SAMPLES);
    for (int sampleIndex = 0; sampleIndex < ret; sampleIndex++)
    {
        for (size_t slotIndex = 0; slotIndex < Impl::MAX_TOUCHES; slotIndex++)
        {
            Impl::Touch& touch = m_impl->touches[slotIndex];

            ts_sample_mt& sample = m_impl->ts_samples[sampleIndex][slotIndex];
            if (sample.valid < 1)
                continue;

//            printf("%ld.%06ld: %d %6d %6d %6d\n",
//                   sample.tv.tv_sec,
//                   sample.tv.tv_usec,
//                   sample.slot,
//                   sample.x,
//                   sample.y,
//                   sample.pressure);

            touch.is_pressed = sample.pressure > 0;
            touch.x = sample.x;
            touch.y = sample.y;
            touch.id = sample.slot;
        }
    }

    Impl::Touch& touch = m_impl->touches[0];

    // Update buttons
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDown[0] = touch.is_pressed;

    // Update mouse position
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    float mouse_x = touch.y;
    float mouse_y = s_height - touch.x;
    io.MousePos = ImVec2(mouse_x, mouse_y);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////

PI_HAL::PI_HAL()
{
    m_impl.reset(new Impl());
}

///////////////////////////////////////////////////////////////////////////////////////////////////

PI_HAL::~PI_HAL()
{

}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool PI_HAL::init()
{
#ifndef USE_SDL
    bcm_host_init();
#endif

#ifdef TEST_LATENCY
    if (!init_pigpio())
    {
        LOGE("Cannot initialize pigpio");
        return false;
    }
#endif

    if (!init_display())
    {
        LOGE("Cannot initialize display");
        return false;
    }
    if (!init_ts())
    {
        LOGE("Cannot initialize touch screen");
        return false;
    }

    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.Fonts->AddFontDefault();
    io.Fonts->AddFontFromMemoryTTF(s_font_droid_sans, 16, 16.f);
    io.Fonts->Build();
    ImGui_ImplOpenGL3_Init();
    ImGui_ImplOpenGL3_NewFrame();

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();

#ifdef TEST_LATENCY
    shutdown_pigpio();
#endif

    shutdown_ts();
    shutdown_display();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

ImVec2 PI_HAL::get_display_size() const
{
    return ImVec2(m_impl->width, m_impl->height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::set_backlight(float brightness)
{
    m_impl->target_backlight = std::min(std::max(brightness, 0.f), 1.f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void* PI_HAL::get_window()
{
    return m_impl->window;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void* PI_HAL::get_main_context()
{
    return m_impl->context;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void* PI_HAL::lock_main_context()
{
    m_impl->context_mutex.lock();
    SDL_GL_MakeCurrent(m_impl->window, m_impl->context);
    return m_impl->context;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void PI_HAL::unlock_main_context()
{
    SDL_GL_MakeCurrent(m_impl->window, nullptr);
    m_impl->context_mutex.unlock();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool PI_HAL::process()
{
    update_ts();
    return update_display();
}
