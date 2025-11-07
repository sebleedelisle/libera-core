#pragma once

#include <functional>
#include <string_view>
#include <sstream>
#include <utility>

namespace libera::core {

using LogSink = std::function<void(std::string_view)>;

void setInfoLogSink(LogSink sink);
void setErrorLogSink(LogSink sink);
void setLogSinks(LogSink infoSink, LogSink errorSink);
void resetLogSinks();

void logInfo(std::string_view message);
void logError(std::string_view message);

template<typename... Args>
void logInfof(Args&&... args) {
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    logInfo(oss.str());
}

template<typename... Args>
void logErrorf(Args&&... args) {
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    logError(oss.str());
}

} // namespace libera::core

