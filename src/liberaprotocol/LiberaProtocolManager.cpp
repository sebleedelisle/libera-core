#include "libera/liberaprotocol/LiberaProtocolManager.hpp"

#include "libera/log/Log.hpp"
#include "libera/protocol/Codec.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace libera::liberaprotocol {
namespace {

using namespace std::chrono_literals;

std::uint16_t discoveryPortFromEnvironment() {
    const char* raw = std::getenv("LIBERA_PROTOCOL_DISCOVERY_PORT");
    if (!raw || raw[0] == '\0') {
        return protocol::DEFAULT_DISCOVERY_PORT;
    }
    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (end == raw || parsed <= 0 || parsed > 65535) {
        return protocol::DEFAULT_DISCOVERY_PORT;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::chrono::milliseconds discoveryWindowFromEnvironment() {
    const char* raw = std::getenv("LIBERA_PROTOCOL_DISCOVERY_MS");
    if (!raw || raw[0] == '\0') {
        return 750ms;
    }
    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (end == raw || parsed <= 0) {
        return 750ms;
    }
    return std::chrono::milliseconds(std::min<long>(parsed, 5000));
}

std::string discoveryKey(const protocol::DiscoveryAdvertisement& advertisement) {
    return advertisement.endpointId + "@" + advertisement.address + ":" +
           std::to_string(advertisement.tcpPort);
}

} // namespace

LiberaProtocolManager::LiberaProtocolManager() = default;

LiberaProtocolManager::~LiberaProtocolManager() {
    closeAll();
}

std::vector<std::unique_ptr<core::ControllerInfo>> LiberaProtocolManager::discover() {
    std::vector<std::unique_ptr<core::ControllerInfo>> out;

    auto io = net::shared_io_context();
    net::UdpSocket socket(*io);
    if (auto ec = socket.open_v4(false)) {
        logError("[LiberaProtocolManager] discovery socket open failed", ec.message());
        return out;
    }
    const auto discoveryPort = discoveryPortFromEnvironment();
    if (auto ec = socket.bind_any(discoveryPort, false)) {
        logError("[LiberaProtocolManager] discovery bind failed",
                 discoveryPort,
                 ec.message());
        return out;
    }

    std::unordered_map<std::string, LiberaProtocolControllerInfo> discovered;
    std::array<std::uint8_t, 4096> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + discoveryWindowFromEnvironment();

    while (std::chrono::steady_clock::now() < deadline) {
        net::udp::endpoint senderEndpoint;
        std::size_t received = 0;
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now);
        const auto timeout = std::min(100ms, std::max(1ms, remaining));
        auto ec = socket.recv_from(buffer.data(),
                                   buffer.size(),
                                   senderEndpoint,
                                   received,
                                   timeout,
                                   false);
        if (ec) {
            if (ec == net::asio::error::timed_out ||
                ec == net::asio::error::operation_aborted) {
                continue;
            }
            break;
        }

        protocol::DiscoveryAdvertisement advertisement;
        std::string error;
        if (!protocol::decodeDiscoveryAdvertisement(buffer.data(),
                                                    received,
                                                    advertisement,
                                                    error)) {
            continue;
        }
        const auto sourceAddress = senderEndpoint.address().to_string();
        if (advertisement.address.empty()) {
            advertisement.address = sourceAddress;
        }
        if (advertisement.endpointId.empty()) {
            advertisement.endpointId = advertisement.address + ":" +
                                       std::to_string(advertisement.tcpPort);
        }
        if (advertisement.displayName.empty()) {
            advertisement.displayName = "Libera Protocol " + advertisement.endpointId;
        }
        const auto key = discoveryKey(advertisement);
        discovered.emplace(key,
                           LiberaProtocolControllerInfo(advertisement, sourceAddress));
    }

    out.reserve(discovered.size());
    for (auto& [key, info] : discovered) {
        (void)key;
        out.emplace_back(std::make_unique<LiberaProtocolControllerInfo>(std::move(info)));
    }
    return out;
}

std::shared_ptr<LiberaProtocolController>
LiberaProtocolManager::createController(const LiberaProtocolControllerInfo& info) {
    return std::make_shared<LiberaProtocolController>(info);
}

LiberaProtocolManager::NewControllerDisposition
LiberaProtocolManager::prepareNewController(LiberaProtocolController& controller,
                                            const LiberaProtocolControllerInfo& info) {
    if (auto result = controller.connect(info); !result) {
        logError("[LiberaProtocolManager] initial connect failed", result.error().message());
        return NewControllerDisposition::DropController;
    }
    controller.startThread();
    return NewControllerDisposition::KeepController;
}

void LiberaProtocolManager::prepareExistingController(LiberaProtocolController& controller,
                                                      const LiberaProtocolControllerInfo& info) {
    controller.updateDiscoveredInfo(info);
}

void LiberaProtocolManager::closeController(const std::string& key,
                                            LiberaProtocolController& controller) {
    (void)key;
    controller.close();
}

} // namespace libera::liberaprotocol
