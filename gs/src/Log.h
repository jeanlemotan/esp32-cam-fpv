#pragma once

#include <string>
#include "fmt/format.h"

enum class LogLevel : uint8_t
{
    DBG,		//used to debug info. Disabled in Release
    INFO,		//used to print usefull info both in Debug and Release
    WARNING,	//used to print warnings that will not crash, both Debug and Release
    ERR			//used for messages that will probably crash or seriously affect the game. Both Debug and Release
};

template<class Fmt, typename... Params>
void logf(LogLevel level, char const* file, int line, Fmt const& fmt, Params&&... params)
{
    const char* levelStr = "";
    switch (level)
    {
        case LogLevel::DBG: levelStr = "D"; break;
        case LogLevel::INFO: levelStr = "I"; break;
        case LogLevel::WARNING: levelStr = "W"; break;
        case LogLevel::ERR: levelStr = "E"; break;
    }
    printf("(%s) %s: %d: %s\n", levelStr, file, line, fmt::format(fmt, std::forward<Params>(params)...).c_str());
}

#define LOGD(fmt, ...) logf(LogLevel::DBG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) logf(LogLevel::INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) logf(LogLevel::WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) logf(LogLevel::ERR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)