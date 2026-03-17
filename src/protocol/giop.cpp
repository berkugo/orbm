#include "protocol/giop.h"
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <vector>

namespace {

constexpr size_t GIOP_HDR_LEN = 12;

inline size_t align4(size_t pos) { return (pos + 3) & ~size_t(3); }
inline size_t align8(size_t pos) { return (pos + 7) & ~size_t(7); }

inline size_t align8_from_msg_start(size_t body_offset) {
    return align8(GIOP_HDR_LEN + body_offset) - GIOP_HDR_LEN;
}

uint32_t read_u32(const uint8_t* buf, size_t len, size_t pos, bool le) {
    if (pos + 4 > len) return 0;
    uint32_t v;
    std::memcpy(&v, buf + pos, 4);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (le) v = __builtin_bswap32(v);
#else
    if (!le) v = __builtin_bswap32(v);
#endif
    return v;
}

struct StringResult { std::string str; size_t next; };

StringResult read_cdr_string(const uint8_t* buf, size_t len, size_t pos, bool le) {
    if (pos + 4 > len) return {"", pos};
    uint32_t slen = read_u32(buf, len, pos, le);
    if (slen == 0 || pos + 4 + slen > len) return {"", pos + 4};
    size_t text_len = (slen > 0 && buf[pos + 4 + slen - 1] == 0) ? slen - 1 : slen;
    std::string s(reinterpret_cast<const char*>(buf + pos + 4), text_len);
    size_t next = align4(pos + 4 + slen);
    return {std::move(s), next};
}

struct OctetSeqResult { std::vector<uint8_t> data; size_t next; };

OctetSeqResult read_cdr_octet_seq(const uint8_t* buf, size_t len, size_t pos, bool le) {
    if (pos + 4 > len) return {{}, pos};
    uint32_t slen = read_u32(buf, len, pos, le);
    if (pos + 4 + slen > len) return {{}, pos + 4};
    std::vector<uint8_t> bytes(buf + pos + 4, buf + pos + 4 + slen);
    return {std::move(bytes), pos + 4 + slen};
}

size_t skip_service_context_list(const uint8_t* buf, size_t len, size_t pos, bool le) {
    if (pos + 4 > len) return pos;
    uint32_t count = read_u32(buf, len, pos, le);
    size_t p = pos + 4;
    for (uint32_t i = 0; i < count; ++i) {
        p = align4(p);
        if (p + 4 > len) break;
        p += 4; // context_id
        p = align4(p);
        if (p + 4 > len) break;
        auto [data, next] = read_cdr_octet_seq(buf, len, p, le);
        p = next;
    }
    return p;
}

} // anonymous namespace

std::optional<GiopHeader> parse_giop_header(const uint8_t* buf, size_t len) {
    if (len < 12) return std::nullopt;
    if (buf[0] != 'G' || buf[1] != 'I' || buf[2] != 'O' || buf[3] != 'P')
        return std::nullopt;

    GiopHeader h;
    h.major = buf[4];
    h.minor = buf[5];
    h.flags = buf[6];
    h.msg_type_byte = buf[7];
    h.little_endian = (h.flags & 1) != 0;

    uint32_t sz;
    std::memcpy(&sz, buf + 8, 4);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (h.little_endian) sz = __builtin_bswap32(sz);
#else
    if (!h.little_endian) sz = __builtin_bswap32(sz);
#endif
    h.message_size = sz;
    return h;
}

RequestBodyResult parse_request_body(const uint8_t* buf, size_t len,
                                     uint8_t major, uint8_t minor, bool le) {
    RequestBodyResult r;
    if (len < 4) {
        r.params_offset = len;
        return r;
    }

    if (major == 1 && minor >= 2) {
        size_t pos = 0;

        r.request_id = read_u32(buf, len, pos, le);
        pos += 4;

        if (pos + 1 > len) { r.params_offset = len; return r; }
        uint8_t response_flags = buf[pos];
        r.is_oneway = (response_flags & 1) == 0;
        pos += 1;

        pos += 3; // reserved[3]

        pos = align4(pos);
        if (pos + 4 > len) { r.params_offset = len; return r; }
        uint32_t disposition = read_u32(buf, len, pos, le);
        pos += 4;

        if (disposition == 0) {
            auto [key_bytes, next] = read_cdr_octet_seq(buf, len, pos, le);
            pos = next;
            r.object_key_hex = bytes_to_hex(key_bytes.data(), key_bytes.size());
        }

        pos = align4(pos);
        if (pos >= len) { r.params_offset = len; return r; }
        auto [op, next] = read_cdr_string(buf, len, pos, le);

        size_t sc_end = skip_service_context_list(buf, len, next, le);
        r.params_offset = align8_from_msg_start(sc_end);

        if (!op.empty()) r.operation = std::move(op);
    } else {
        size_t pos = skip_service_context_list(buf, len, 0, le);
        if (pos + 4 > len) { r.params_offset = len; return r; }

        r.request_id = read_u32(buf, len, pos, le);
        pos += 4;

        if (pos + 1 > len) { r.params_offset = len; return r; }
        bool response_expected = buf[pos] != 0;
        r.is_oneway = !response_expected;
        pos += 1;

        if (minor >= 1) pos += 3;

        pos = align4(pos);
        auto [key_bytes, next1] = read_cdr_octet_seq(buf, len, pos, le);
        pos = next1;
        r.object_key_hex = bytes_to_hex(key_bytes.data(), key_bytes.size());

        pos = align4(pos);
        if (pos >= len) { r.params_offset = len; return r; }
        auto [op, params_offset] = read_cdr_string(buf, len, pos, le);
        r.params_offset = params_offset;

        if (!op.empty()) r.operation = std::move(op);
    }

    return r;
}

ReplyBodyResult parse_reply_body(const uint8_t* buf, size_t len,
                                 uint8_t major, uint8_t minor, bool le) {
    ReplyBodyResult r;

    if (major == 1 && minor >= 2 && len >= 8) {
        r.request_id = read_u32(buf, len, 0, le);
        r.reply_status = reply_status_from_u32(read_u32(buf, len, 4, le));
        size_t sc_end = skip_service_context_list(buf, len, 8, le);
        r.reply_body_offset = align8_from_msg_start(sc_end);
        return r;
    }

    size_t pos = skip_service_context_list(buf, len, 0, le);
    if (pos + 8 > len) {
        r.reply_status = reply_status_from_u32(0);
        r.reply_body_offset = len;
        return r;
    }
    r.request_id = read_u32(buf, len, pos, le);
    r.reply_status = reply_status_from_u32(read_u32(buf, len, pos + 4, le));
    r.reply_body_offset = pos + 8;
    return r;
}

std::string bytes_to_hex(const uint8_t* data, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result.push_back(hex_chars[(data[i] >> 4) & 0xF]);
        result.push_back(hex_chars[data[i] & 0xF]);
    }
    return result;
}

std::string first_n_hex(const uint8_t* data, size_t len, size_t max_bytes) {
    size_t take = std::min(len, max_bytes);
    return bytes_to_hex(data, take);
}
