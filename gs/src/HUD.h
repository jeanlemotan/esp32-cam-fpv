#pragma once

class IHAL;

class HUD
{
public:
    HUD(IHAL& hal);

    void draw();

private:

    IHAL& m_hal;
};

