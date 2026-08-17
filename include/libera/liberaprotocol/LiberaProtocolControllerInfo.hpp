#pragma once

#include "libera/System.hpp"
#include "libera/protocol/Protocol.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace libera::liberaprotocol {

class LiberaProtocolControllerInfo : public core::ControllerInfo {
public:
    static constexpr std::string_view controllerType() {
        return "LiberaProtocol";
    }

    LiberaProtocolControllerInfo(protocol::DiscoveryAdvertisement advertisementValue,
                                 std::string sourceAddressValue)
        : core::ControllerInfo(controllerType(),
                               advertisementValue.endpointId,
                               advertisementValue.displayName,
                               advertisementValue.maxPointRate,
                               core::ControllerInfo::NetworkInfo{
                                   advertisementValue.address.empty()
                                       ? sourceAddressValue
                                       : advertisementValue.address,
                                   advertisementValue.tcpPort})
        , advertisement_(std::move(advertisementValue))
        , sourceAddress_(std::move(sourceAddressValue)) {
        if (advertisement_.address.empty()) {
            advertisement_.address = sourceAddress_;
        }
    }

    const protocol::DiscoveryAdvertisement& advertisement() const {
        return advertisement_;
    }

    const std::string& address() const {
        return advertisement_.address;
    }

    std::uint16_t port() const {
        return advertisement_.tcpPort;
    }

    bool supportsStreamMode(protocol::StreamMode mode) const {
        return (advertisement_.supportedStreamModes & protocol::streamModeMask(mode)) != 0;
    }

    std::uint8_t maxUserChannelCount() const {
        return advertisement_.maxUserChannelCount;
    }

    std::uint32_t maxFramePointCount() const {
        return advertisement_.maxFramePointCount;
    }

    std::uint32_t featureFlags() const {
        return advertisement_.featureFlags;
    }

private:
    protocol::DiscoveryAdvertisement advertisement_;
    std::string sourceAddress_;
};

} // namespace libera::liberaprotocol
