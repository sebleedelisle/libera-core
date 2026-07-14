#include "libera/usb/LibusbSafe.hpp"

#include "libera/log/Log.hpp"

#include <cstdio>

#if defined(_WIN32) && defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#endif

namespace libera::usb {

namespace {

#if defined(_WIN32) && defined(_MSC_VER)
unsigned int guardedGetDeviceList(libusb_context* context,
                                  libusb_device*** list,
                                  ssize_t* count) {
    unsigned int exceptionCode = 0;
    __try {
        *count = libusb_get_device_list(context, list);
    } __except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        *list = nullptr;
    }
    return exceptionCode;
}

void logDeviceListException(unsigned int exceptionCode, const char* caller) {
    char codeText[16] = {};
    std::snprintf(codeText, sizeof(codeText), "0x%08x", exceptionCode);
    logError("[libusb] libusb_get_device_list crashed",
             "caller", caller != nullptr ? caller : "unknown",
             "exception", codeText);
}
#endif

} // namespace

ssize_t getDeviceList(libusb_context* context,
                      libusb_device*** list,
                      const char* caller) {
    if (list == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    *list = nullptr;

#if defined(_WIN32) && defined(_MSC_VER)
    ssize_t count = LIBUSB_ERROR_OTHER;
    const unsigned int exceptionCode = guardedGetDeviceList(context, list, &count);
    if (exceptionCode != 0) {
        logDeviceListException(exceptionCode, caller);
        return LIBUSB_ERROR_OTHER;
    }
    return count;
#else
    (void)caller;
    return libusb_get_device_list(context, list);
#endif
}

} // namespace libera::usb
