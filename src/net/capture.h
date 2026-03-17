// Packet capture and GIOP message extraction for ORBM.
//
// `run_capture_blocking` owns the libpcap loop: it reads raw IP/TCP packets,
// reassembles TCP streams, identifies GIOP messages and publishes decoded
// `GiopMessage` instances into a `Channel` for higher layers (CLI / web).
// The function is intentionally blocking and designed to be run on a
// dedicated background thread.
//
#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>

class IdlRegistry;
class Tracker;

// Builds a conservative BPF filter for the capture interface.
// Currently this is a thin wrapper that can evolve to honor port hints.
std::string build_bpf_filter(const std::vector<uint16_t>& ports);

// Starts a blocking capture loop on the given `interface`, applies `filter`,
// and pushes every successfully decoded GIOP message to `msg_channel`.
// 
// Lifetime / ownership:
// - Returns only when `stop_flag` becomes true or libpcap terminates.
// - Does not own any of the shared state; callers are responsible for
//   synchronising shutdown and closing the channel.
void run_capture_blocking(
    const std::string& interface,
    const std::string& filter,
    SharedLookup lookup,
    SharedPortMap port_map,
    std::shared_ptr<Tracker> tracker,
    std::shared_ptr<Channel<GiopMessage>> msg_channel,
    std::shared_ptr<std::atomic<uint64_t>> message_id,
    std::shared_ptr<std::atomic<bool>> stop_flag,
    std::shared_ptr<const IdlRegistry> idl_registry
);
