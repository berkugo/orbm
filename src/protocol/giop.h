// GIOP header and message-body helpers for ORBM.
//
// This module understands the on-the-wire layout of GIOP 1.x messages and
// provides safe parsing helpers used by the capture layer:
// - `parse_giop_header` validates the 12‑byte header and extracts size/flags.
// - `parse_request_body` and `parse_reply_body` compute offsets into the
//   CDR payload where operation arguments live.
//
#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <cstdint>
#include <cstddef>

// Minimal in-memory representation of a 12‑byte GIOP header.
struct GiopHeader {
    uint8_t major, minor, flags, msg_type_byte;
    uint32_t message_size;
    bool little_endian;
};

// Validates and parses a GIOP header from `buf` (at least 12 bytes).
// Returns `std::nullopt` if the buffer does not start with \"GIOP\".
std::optional<GiopHeader> parse_giop_header(const uint8_t* buf, size_t len);

// Result of parsing the protocol-specific part of a GIOP Request body.
struct RequestBodyResult {
    std::optional<std::string> operation;
    std::optional<std::string> object_key_hex;
    bool is_oneway = true;
    uint32_t request_id = 0;
    size_t params_offset = 0;
};

// Parses the GIOP Request body and locates the CDR-encoded argument block.
RequestBodyResult parse_request_body(const uint8_t* buf, size_t len,
                                     uint8_t major, uint8_t minor, bool little_endian);

// Result of parsing the fixed header of a GIOP Reply body.
struct ReplyBodyResult {
    uint32_t request_id = 0;
    ReplyStatus reply_status = ReplyStatus::Unknown;
    size_t reply_body_offset = 0;
};

// Parses the GIOP Reply body and locates the CDR-encoded return/out block.
ReplyBodyResult parse_reply_body(const uint8_t* buf, size_t len,
                                 uint8_t major, uint8_t minor, bool little_endian);

// Utility helpers for hex‑encoding raw bytes for the UI and debugging.
std::string bytes_to_hex(const uint8_t* data, size_t len);
std::string first_n_hex(const uint8_t* data, size_t len, size_t max_bytes);
