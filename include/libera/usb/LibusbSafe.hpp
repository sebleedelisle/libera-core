#pragma once

#include "libusb.h"

namespace libera::usb {

ssize_t getDeviceList(libusb_context* context,
                      libusb_device*** list,
                      const char* caller = nullptr);

} // namespace libera::usb
