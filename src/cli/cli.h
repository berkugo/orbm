// CLI (terminal) view for ORBM.
//
// `run_cli` consumes a stream of `GiopMessage` instances from the capture
// layer and renders a colorised, tail‑like view of live CORBA traffic.
// It is intentionally read‑only: configuration continues to flow through
// the same discovery / capture pipeline as the web UI.
//
#pragma once

#include "core/types.h"
#include <memory>
#include <atomic>
#include <string>
#include <vector>

class IdlRegistry;
class Tracker;

// Simple configuration for CLI mode; parsed from command-line flags.
struct CliConfig {
    std::string interface = "any";
    std::string ns_ref;
    size_t buffer = 100;
    bool show_hex = false;
    bool show_raw = false;
    bool compact = false;
};

// Starts the interactive CLI loop and prints messages until `stop_flag`
// becomes true or the underlying channel closes.
void run_cli(
    CliConfig cli_cfg,
    SharedObjects objects,
    SharedLookup lookup,
    SharedPortMap port_map,
    std::shared_ptr<Tracker> tracker,
    std::shared_ptr<Channel<GiopMessage>> msg_channel,
    std::shared_ptr<std::atomic<bool>> stop_flag);
