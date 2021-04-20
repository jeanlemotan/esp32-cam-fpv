#pragma once

#include "IHAL.h"
#include <memory>

class PI_HAL : virtual public IHAL
{
public:
    PI_HAL();
    ~PI_HAL();

    bool init() override;
    void shutdown() override;
    
    void* get_window() override;
    void* get_main_context() override;
    void* lock_main_context() override;
    void unlock_main_context() override;

    ImVec2 get_display_size() const override;
    void set_backlight(float brightness) override; //0..1

    bool process() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool init_pigpio();
    void shutdown_pigpio();

    bool init_display_dispmanx();
    bool init_display_sdl();
    bool init_display();

    void shutdown_display_dispmanx();
    void shutdown_display_sdl();
    void shutdown_display();
    
    bool update_display(); 

    bool init_ts();
    void shutdown_ts();
    void update_ts();
};
