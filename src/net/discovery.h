// Naming Service discovery helpers for ORBM.
//
// This module shells out to TAO tools (`tao_nslist`, `tao_catior`) to
// discover objects registered in the Naming Service and to resolve IORs
// into host/port/object-key information. Results are normalised into
// `CorbaNamingEntry` and auxiliary lookup maps.
//
#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <tuple>

// Runs a single discovery cycle against the configured Naming Service and
// returns a flat list of discovered CORBA objects.
std::vector<CorbaNamingEntry> run_discovery(const DiscoveryConfig& config);

// Parses textual `tao_nslist` output into `CorbaNamingEntry` records.
std::vector<CorbaNamingEntry> parse_nslist_output(const std::string& output);

// Builds a fast lookup table from object key hex → naming entry.
std::unordered_map<std::string, CorbaNamingEntry> build_lookup_map(
    const std::vector<CorbaNamingEntry>& entries);

// Builds a reverse map from server port → entries reachable on that port.
std::unordered_map<uint16_t, std::vector<CorbaNamingEntry>> build_port_map(
    const std::vector<CorbaNamingEntry>& entries);

// Parses `tao_catior` output into (host, port, type_id, object_key_hex, giop_version).
std::optional<std::tuple<std::string, uint16_t, std::string, std::string, std::string>>
parse_catior_output(const std::string& output);
