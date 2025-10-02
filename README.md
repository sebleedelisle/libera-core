# libera-core

This repository implements reusable building blocks for streaming laser control data. The EtherDream integration highlights how runtime control, networking, and wire serialization are separated for clarity and testability.

## EtherDream Control Loop
- `EtherDreamDevice` owns the worker thread that polls device status, gathers points from user callbacks, and streams frames using the configuration in `libera/etherdream/EtherDreamConfig.hpp`.
- Every command uses `waitForResponse()` to synchronously await the EtherDream ACK and capture the decoded `dac_status` payload (buffer fullness, playback state, point rate, etc.).
- The streaming loop keeps at least `ETHERDREAM_MIN_PACKET_POINTS` points in flight and ensures latency coverage using `ETHERDREAM_MAX_LATENCY` to compute the minimum refill quantity.

## Networking Setup
- TCP connectivity is managed by `libera::net::TcpClient`; `EtherDreamDevice::connect()` resolves the configured port (`ETHERDREAM_DAC_PORT`) and enables low-latency options (TCP_NODELAY + keepalive) as soon as the socket comes up.
- A library-wide default timeout is controlled through `libera::net::set_default_timeout()` (initialised from `ETHERDREAM_DEFAULT_TIMEOUT`). Any networking call can override the timeout, but omitting the argument keeps behaviour consistent and reduces boilerplate.
- The worker loop starts with a protocol ping (`?`) to confirm the DAC is alive before entering the cadence-driven run loop, while connection attempts use `ETHERDREAM_CONNECT_TIMEOUT` for a longer grace period.
- `waitForResponse()` blocks for the matching EtherDream ACK and returns the parsed status payload, so every command path picks up fresh device telemetry or surfaces transport errors immediately.

## Serialization Contract
- `EtherDreamProtocol` encapsulates the encoding rules for converting `LaserPoint` values into EtherDream frames. `serializePoints()` returns reusable thread-local storage to avoid heap churn on the hot path.
- Coordinate and colour scaling obey the EtherDream DAC v2 specification (section 2.2 "Point Format"): coordinates map to signed 16-bit integers (`ETHERDREAM_COORD_SCALE`), while colour/intensity channels map to unsigned 16-bit (`ETHERDREAM_CHANNEL_SCALE`).
- The protocol helpers keep the worker loop focused solely on scheduling and IO; any other component that needs to inspect packet sizes can include `EtherDreamProtocol.hpp` and rely on the shared constants (`ETHERDREAM_HEADER_SIZE`, `ETHERDREAM_POINT_SIZE`, etc.).
- Time-sensitive behaviour is tuned through `ETHERDREAM_MAX_LATENCY`, `ETHERDREAM_MIN_PACKET_POINTS`, and the sleep bounds (`ETHERDREAM_MIN_SLEEP` / `ETHERDREAM_MAX_SLEEP`), with the stream loop calculating how many samples to queue based on the last ACK’s buffer fullness and point rate.

## Coding Conventions
- Compile-time constants use ALL_CAPS snake case (for example `ETHERDREAM_MIN_POINTS_PER_TICK`) to make immutability obvious in hot code paths.
- File headers provide a brief summary of responsibilities and cross-component touch points to reduce orientation time for new contributors.
