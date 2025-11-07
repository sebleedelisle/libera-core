#include "libera/core/Log.hpp"

#include <iostream>
#include <mutex>

namespace libera::core {

namespace {

LogSink makeDefaultInfoSink() {
    return [](std::string_view message) {
        std::cout << message;
        std::cout.flush();
    };
}

LogSink makeDefaultErrorSink() {
    return [](std::string_view message) {
        std::cerr << message;
        std::cerr.flush();
    };
}

std::mutex sinkMutex;
LogSink infoSink = makeDefaultInfoSink();
LogSink errorSink = makeDefaultErrorSink();

} // namespace

void setInfoLogSink(LogSink sink) {
    std::lock_guard lock(sinkMutex);
    infoSink = sink ? std::move(sink) : makeDefaultInfoSink();
}

void setErrorLogSink(LogSink sink) {
    std::lock_guard lock(sinkMutex);
    errorSink = sink ? std::move(sink) : makeDefaultErrorSink();
}

void setLogSinks(LogSink newInfo, LogSink newError) {
    std::lock_guard lock(sinkMutex);
    infoSink = newInfo ? std::move(newInfo) : makeDefaultInfoSink();
    errorSink = newError ? std::move(newError) : makeDefaultErrorSink();
}

void resetLogSinks() {
    std::lock_guard lock(sinkMutex);
    infoSink = makeDefaultInfoSink();
    errorSink = makeDefaultErrorSink();
}

void logInfo(std::string_view message) {
    LogSink sink;
    {
        std::lock_guard lock(sinkMutex);
        sink = infoSink;
    }
    if (sink) {
        sink(message);
    }
}

void logError(std::string_view message) {
    LogSink sink;
    {
        std::lock_guard lock(sinkMutex);
        sink = errorSink;
    }
    if (sink) {
        sink(message);
    }
}

} // namespace libera::core

