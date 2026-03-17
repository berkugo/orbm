// CDR (Common Data Representation) decoding helpers for ORBM.
//
// These functions walk a CDR stream using IDL metadata from `IdlRegistry`
// and produce human‑readable strings for display in the CLI / web UI.
// They are intentionally defensive: whenever decoding fails, they return
// placeholder \"decode error\" markers instead of throwing.
//
#pragma once

#include "core/types.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <cstddef>

class IdlRegistry;

// Decodes all in / inout parameters for a request, starting at
// `params_offset` within the message body.
std::vector<DecodedParam> decode_request_params(
    const uint8_t* body, size_t body_len, size_t params_offset,
    const OpSignature& sig, bool little_endian,
    const IdlRegistry* registry = nullptr);

// Decodes only the return value from a reply body.
std::optional<DecodedParam> decode_reply_return(
    const uint8_t* body, size_t body_len, size_t reply_body_offset,
    const OpSignature& sig, bool little_endian,
    const IdlRegistry* registry = nullptr);

// Container for a full reply decode (return + out/inout parameters).
struct ReplyDecodeResult {
    std::optional<DecodedParam> return_value;
    std::vector<DecodedParam> out_params;
};

// Decodes the reply return value and all out / inout parameters in order.
ReplyDecodeResult decode_reply_params(
    const uint8_t* body, size_t body_len, size_t reply_body_offset,
    const OpSignature& sig, bool little_endian,
    const IdlRegistry* registry = nullptr);
