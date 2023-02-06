#pragma once


//When enabled, it will output a 4Hz pulse (50ms ON, 200ms OFF) on GPIO 17. This can be used to blink a LED pointing inside the camera.
//This is used with a photodiode on the screen to measure with an oscilloscope the delay between the GPIO 17 pulse and the pixels on screen
#if defined(RASPBERRY_PI)
	//#define TEST_LATENCY
#else
	//#define TEST_LATENCY
#endif

//When enabled (together with TEST_LATENCY), it will output a 4Hz pulse (50ms ON, 200ms OFF) on GPIO 17. 
//On top of this, together with the on/off pulse it will send a white/black frame to the decoding thread.
//This is used with a photodiode on the screen to measure with an oscilloscope the delay between the GPIO 17 pulse and the pixels on screen
//This measurement excludes the air unit and is used to measure the latency of the ground station alone.
//#define TEST_DISPLAY_LATENCY

#define CHECK_GL_ERRORS

#if defined(CHECK_GL_ERRORS)
#define GLCHK(X) \
do { \
    GLenum err = GL_NO_ERROR; \
    X; \
   while ((err = glGetError())) \
   { \
      LOGE("GL error {} in " #X " file {} line {}", err, __FILE__,__LINE__); \
   } \
} while(0)
#define SDLCHK(X) \
do { \
    int err = X; \
    if (err != 0) LOGE("SDL error {} in " #X " file {} line {}", err, __FILE__,__LINE__); \
} while (0)
#else
#define GLCHK(X) X
#define SDLCHK(X) X
#endif
