#include "protocol/cdr_decode.h"

#if __has_include("idl/idl_parser.h")
#include "idl/idl_parser.h"
#else
class IdlRegistry {
public:
    const StructFields* get_struct_fields(const std::string&) const { return nullptr; }
    const std::string* resolve_typedef(const std::string&) const { return nullptr; }
    bool is_enum(const std::string&) const { return false; }
};
#endif

#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <memory>

namespace {

struct TCDesc;
static std::string format_f32(float v);
static std::string format_f64(double v);
static std::string format_hex_byte(uint8_t b);
static std::string format_wchar(uint16_t v);

static constexpr size_t MAX_SEQ_LEN = 65536;
static constexpr size_t MAX_DECODE_ELEMENTS = 256;
static constexpr size_t MAX_DISPLAY = 64;

class CdrCursor {
public:
    CdrCursor(const uint8_t* buf, size_t buf_len, size_t offset, bool little_endian)
        : buf_(buf), buf_len_(buf_len), base_(offset), pos_(offset), le_(little_endian) {}

    size_t remaining() const {
        return (pos_ >= buf_len_) ? 0 : buf_len_ - pos_;
    }

    size_t pos() const { return pos_; }

    void align(size_t n) {
        size_t m = (pos_ - base_) % n;
        if (m != 0) pos_ += n - m;
    }

    std::optional<uint8_t> read_u8() {
        if (remaining() < 1) return std::nullopt;
        return buf_[pos_++];
    }

    std::optional<int16_t> read_i16() {
        align(2);
        if (remaining() < 2) return std::nullopt;
        int16_t v;
        std::memcpy(&v, buf_ + pos_, 2);
        pos_ += 2;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) { uint16_t u; std::memcpy(&u, &v, 2); u = __builtin_bswap16(u); std::memcpy(&v, &u, 2); }
#else
        if (!le_) { uint16_t u; std::memcpy(&u, &v, 2); u = __builtin_bswap16(u); std::memcpy(&v, &u, 2); }
#endif
        return v;
    }

    std::optional<uint16_t> read_u16() {
        align(2);
        if (remaining() < 2) return std::nullopt;
        uint16_t v;
        std::memcpy(&v, buf_ + pos_, 2);
        pos_ += 2;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) v = __builtin_bswap16(v);
#else
        if (!le_) v = __builtin_bswap16(v);
#endif
        return v;
    }

    std::optional<int32_t> read_i32() {
        align(4);
        if (remaining() < 4) return std::nullopt;
        int32_t v;
        std::memcpy(&v, buf_ + pos_, 4);
        pos_ += 4;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) { uint32_t u; std::memcpy(&u, &v, 4); u = __builtin_bswap32(u); std::memcpy(&v, &u, 4); }
#else
        if (!le_) { uint32_t u; std::memcpy(&u, &v, 4); u = __builtin_bswap32(u); std::memcpy(&v, &u, 4); }
#endif
        return v;
    }

    std::optional<uint32_t> read_u32() {
        align(4);
        if (remaining() < 4) return std::nullopt;
        uint32_t v;
        std::memcpy(&v, buf_ + pos_, 4);
        pos_ += 4;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) v = __builtin_bswap32(v);
#else
        if (!le_) v = __builtin_bswap32(v);
#endif
        return v;
    }

    std::optional<int64_t> read_i64() {
        align(4); // TAO often uses 4-byte alignment for 8-byte types in body
        return read_i64_no_align();
    }

    std::optional<int64_t> read_i64_no_align() {
        if (remaining() < 8) return std::nullopt;
        int64_t v;
        std::memcpy(&v, buf_ + pos_, 8);
        pos_ += 8;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) { uint64_t u; std::memcpy(&u, &v, 8); u = __builtin_bswap64(u); std::memcpy(&v, &u, 8); }
#else
        if (!le_) { uint64_t u; std::memcpy(&u, &v, 8); u = __builtin_bswap64(u); std::memcpy(&v, &u, 8); }
#endif
        return v;
    }

    std::optional<uint64_t> read_u64() {
        align(4);
        return read_u64_no_align();
    }

    std::optional<uint64_t> read_u64_no_align() {
        if (remaining() < 8) return std::nullopt;
        uint64_t v;
        std::memcpy(&v, buf_ + pos_, 8);
        pos_ += 8;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) v = __builtin_bswap64(v);
#else
        if (!le_) v = __builtin_bswap64(v);
#endif
        return v;
    }

    std::optional<float> read_f32() {
        align(4);
        if (remaining() < 4) return std::nullopt;
        uint32_t bits;
        std::memcpy(&bits, buf_ + pos_, 4);
        pos_ += 4;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) bits = __builtin_bswap32(bits);
#else
        if (!le_) bits = __builtin_bswap32(bits);
#endif
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }

    std::optional<double> read_f64() {
        align(8);
        return read_f64_no_align();
    }

    std::optional<double> read_f64_no_align() {
        if (remaining() < 8) return std::nullopt;
        uint64_t bits;
        std::memcpy(&bits, buf_ + pos_, 8);
        pos_ += 8;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) bits = __builtin_bswap64(bits);
#else
        if (!le_) bits = __builtin_bswap64(bits);
#endif
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    }

    std::optional<std::string> read_string() {
        auto len_opt = read_u32();
        if (!len_opt) return std::nullopt;
        size_t len = *len_opt;
        if (len == 0) return std::string{};
        if (remaining() < len) return std::nullopt;
        size_t text_len = (buf_[pos_ + len - 1] == 0) ? len - 1 : len;
        std::string s(reinterpret_cast<const char*>(buf_ + pos_), text_len);
        pos_ += len;
        return s;
    }

    // CDR wstring: 4-byte length (octets), then UTF-16 code units (2 bytes each).
    std::optional<std::string> read_wstring() {
        auto len_opt = read_u32();
        if (!len_opt) return std::nullopt;
        size_t octet_len = *len_opt;
        if (octet_len == 0) return std::string{};
        if (remaining() < octet_len) return std::nullopt;
        size_t start = pos_;
        size_t num_wchars = octet_len / 2;
        std::string result;
        result.reserve(num_wchars);
        for (size_t i = 0; i < num_wchars; ++i) {
            auto w = read_u16();
            if (!w) return std::nullopt;
            uint16_t cp = *w;
            if (cp == 0) break;
            if (cp < 0x80) {
                result += static_cast<char>(cp);
            } else if (cp < 0x800) {
                result += static_cast<char>(0xC0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                result += static_cast<char>(0xE0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
        pos_ = start + octet_len;
        return result;
    }

    std::optional<uint32_t> peek_u32() const {
        if (remaining() < 4) return std::nullopt;
        uint32_t v;
        std::memcpy(&v, buf_ + pos_, 4);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) v = __builtin_bswap32(v);
#else
        if (!le_) v = __builtin_bswap32(v);
#endif
        return v;
    }

    void set_pos(size_t p) { pos_ = p; }
    const uint8_t* get_buf() const { return buf_; }
    size_t get_pos() const { return pos_; }
    size_t get_buf_len() const { return buf_len_; }
    bool is_little_endian() const { return le_; }

    std::optional<std::string> decode_type(const std::string& type_name,
                                           const IdlRegistry* registry);
    std::optional<std::string> decode_type_impl(const std::string& type_name,
                                                const IdlRegistry* registry,
                                                unsigned int indirection_depth);

private:
    std::optional<std::string> decode_sequence(const std::string& type_name,
                                               const IdlRegistry* registry);
    std::optional<std::string> decode_array(const std::string& type_name,
                                            const IdlRegistry* registry);
    std::optional<std::string> decode_union(const UnionDef& def,
                                            const IdlRegistry* registry);
    std::optional<std::string> decode_struct(const StructFields& fields,
                                             const IdlRegistry* registry);
    std::optional<std::string> decode_struct_field(const std::string& ftype,
                                                   bool prev_was_string,
                                                   const IdlRegistry* registry);

    friend std::optional<std::string> decode_any_value_from_typecode(CdrCursor&, const TCDesc&, const IdlRegistry*);

    const uint8_t* buf_;
    size_t buf_len_;
    size_t base_;
    size_t pos_;
    bool le_;
};

// ─── TypeCode for any ─────────────────────────────────────────────────────
enum class TCKind : uint8_t {
    tk_null = 0, tk_void = 1, tk_short = 2, tk_long = 3, tk_ushort = 4, tk_ulong = 5,
    tk_float = 6, tk_double = 7, tk_boolean = 8, tk_char = 9, tk_octet = 10,
    tk_any = 11, tk_TypeCode = 12, tk_Principal = 13, tk_object = 14,
    tk_struct = 15, tk_union = 16, tk_enum = 17, tk_string = 18, tk_sequence = 19,
    tk_array = 20, tk_alias = 21, tk_except = 22, tk_longlong = 23, tk_ulonglong = 24,
    tk_longdouble = 25, tk_wchar = 26, tk_wstring = 27, tk_fixed = 28
};

struct TCDesc {
    uint8_t kind = 0;
    uint32_t bound = 0;
    uint32_t array_dim = 0;
    std::unique_ptr<TCDesc> element_type;
    std::unique_ptr<TCDesc> switch_type;
    std::vector<std::pair<std::string, std::unique_ptr<TCDesc>>> struct_members;
    std::vector<std::tuple<int32_t, std::string, std::unique_ptr<TCDesc>>> union_members;
    int default_index = -1;
    std::vector<std::string> enum_members;
};

class TypeCodeReader {
public:
    TypeCodeReader(const uint8_t* data, size_t start, size_t end, bool le)
        : data_(data), base_(start), pos_(start), end_(end), le_(le) {}

    size_t pos() const { return pos_; }
    size_t end() const { return end_; }
    const uint8_t* data() const { return data_; }
    bool little_endian() const { return le_; }
    void set_pos(size_t p) { pos_ = p; }

    void align(size_t n) {
        size_t m = (pos_ - base_) % n;
        if (m != 0) pos_ += n - m;
    }

    std::optional<uint8_t> read_u8() {
        if (pos_ >= end_) return std::nullopt;
        return data_[pos_++];
    }

    std::optional<int16_t> read_i16() {
        align(2);
        if (pos_ + 2 > end_) return std::nullopt;
        int16_t v;
        std::memcpy(&v, data_ + pos_, 2);
        pos_ += 2;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) { uint16_t u; std::memcpy(&u, &v, 2); u = __builtin_bswap16(u); std::memcpy(&v, &u, 2); }
#else
        if (!le_) { uint16_t u; std::memcpy(&u, &v, 2); u = __builtin_bswap16(u); std::memcpy(&v, &u, 2); }
#endif
        return v;
    }

    std::optional<uint16_t> read_u16() {
        align(2);
        if (pos_ + 2 > end_) return std::nullopt;
        uint16_t v;
        std::memcpy(&v, data_ + pos_, 2);
        pos_ += 2;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) v = __builtin_bswap16(v);
#else
        if (!le_) v = __builtin_bswap16(v);
#endif
        return v;
    }

    std::optional<int32_t> read_i32() {
        align(4);
        if (pos_ + 4 > end_) return std::nullopt;
        int32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) { uint32_t u; std::memcpy(&u, &v, 4); u = __builtin_bswap32(u); std::memcpy(&v, &u, 4); }
#else
        if (!le_) { uint32_t u; std::memcpy(&u, &v, 4); u = __builtin_bswap32(u); std::memcpy(&v, &u, 4); }
#endif
        return v;
    }

    std::optional<uint32_t> read_u32() {
        align(4);
        if (pos_ + 4 > end_) return std::nullopt;
        uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (le_) v = __builtin_bswap32(v);
#else
        if (!le_) v = __builtin_bswap32(v);
#endif
        return v;
    }

    std::optional<std::string> read_string() {
        auto len = read_u32();
        if (!len || pos_ + *len > end_) return std::nullopt;
        size_t l = *len;
        if (l > 0 && data_[pos_ + l - 1] == 0) l--;
        std::string s(reinterpret_cast<const char*>(data_ + pos_), l);
        pos_ += *len;
        return s;
    }

private:
    const uint8_t* data_;
    size_t base_;
    size_t pos_;
    size_t end_;
    bool le_;
};

static constexpr unsigned int MAX_TC_DEPTH = 32;
static constexpr uint32_t TC_INDIRECTION = 0xFFFFFFFFu;

static std::optional<TypeCodeReader> read_typecode_encapsulation(TypeCodeReader& r) {
    auto len = r.read_u32();
    if (!len) return std::nullopt;
    size_t start = r.pos();
    size_t end = start + *len;
    if (end > r.end() || *len < 1) return std::nullopt;
    auto byte_order = r.read_u8();
    if (!byte_order) return std::nullopt;
    r.set_pos(end);
    return TypeCodeReader(r.data(), start + 1, end, *byte_order != 0);
}

static std::optional<int32_t> read_typecode_label(TypeCodeReader& r, const TCDesc& switch_desc) {
    switch (static_cast<TCKind>(switch_desc.kind)) {
        case TCKind::tk_boolean: {
            auto v = r.read_u8();
            return v ? std::optional<int32_t>(*v ? 1 : 0) : std::nullopt;
        }
        case TCKind::tk_short: {
            auto v = r.read_i16();
            return v ? std::optional<int32_t>(*v) : std::nullopt;
        }
        case TCKind::tk_long:
        case TCKind::tk_enum: {
            return r.read_i32();
        }
        case TCKind::tk_ushort: {
            auto v = r.read_u16();
            return v ? std::optional<int32_t>(static_cast<int32_t>(*v)) : std::nullopt;
        }
        case TCKind::tk_ulong: {
            auto v = r.read_u32();
            return v ? std::optional<int32_t>(static_cast<int32_t>(*v)) : std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

static std::optional<TCDesc> parse_typecode(TypeCodeReader& r, unsigned int depth = 0) {
    if (depth > MAX_TC_DEPTH) return std::nullopt;

    size_t kind_pos = r.pos();
    auto k = r.read_u32();
    if (!k) return std::nullopt;
    if (*k == TC_INDIRECTION) {
        auto off = r.read_i32();
        if (!off) return std::nullopt;
        int64_t target64 = static_cast<int64_t>(kind_pos) + *off;
        if (target64 < 0 || static_cast<size_t>(target64) >= r.end()) return std::nullopt;
        size_t saved = r.pos();
        r.set_pos(static_cast<size_t>(target64));
        auto indirected = parse_typecode(r, depth + 1);
        r.set_pos(saved);
        return indirected;
    }

    TCDesc desc;
    desc.kind = static_cast<uint8_t>(*k);
    switch (static_cast<TCKind>(*k)) {
        case TCKind::tk_string:
        case TCKind::tk_wstring: {
            auto enc = read_typecode_encapsulation(r);
            if (!enc) return std::nullopt;
            auto b = enc->read_u32();
            if (!b) return std::nullopt;
            desc.bound = *b;
            break;
        }
        case TCKind::tk_sequence: {
            auto enc = read_typecode_encapsulation(r);
            if (!enc) return std::nullopt;
            auto elem = parse_typecode(*enc, depth + 1);
            if (!elem) return std::nullopt;
            desc.element_type = std::make_unique<TCDesc>(std::move(*elem));
            auto max_len = enc->read_u32();
            if (!max_len) return std::nullopt;
            desc.bound = *max_len;
            (void)max_len;
            break;
        }
        case TCKind::tk_array: {
            auto enc = read_typecode_encapsulation(r);
            if (!enc) return std::nullopt;
            auto elem = parse_typecode(*enc, depth + 1);
            if (!elem) return std::nullopt;
            desc.element_type = std::make_unique<TCDesc>(std::move(*elem));
            auto dim = enc->read_u32();
            if (!dim) return std::nullopt;
            desc.array_dim = *dim;
            break;
        }
        case TCKind::tk_object: {
            auto enc = read_typecode_encapsulation(r);
            if (!enc) return std::nullopt;
            (void)enc->read_string(); // repository ID
            (void)enc->read_string(); // type name
            break;
        }
        case TCKind::tk_struct:
        case TCKind::tk_except: {
            auto enc = read_typecode_encapsulation(r);
            if (!enc) return std::nullopt;
            (void)enc->read_string(); // repository ID
            (void)enc->read_string(); // type name
            auto count = enc->read_u32();
            if (!count) return std::nullopt;
            for (uint32_t i = 0; i < *count; ++i) {
                auto name = enc->read_string();
                if (!name) return std::nullopt;
                auto mem = parse_typecode(*enc, depth + 1);
                if (!mem) return std::nullopt;
                desc.struct_members.push_back({*name, std::make_unique<TCDesc>(std::move(*mem))});
            }
            break;
        }
        case TCKind::tk_union: {
            auto enc = read_typecode_encapsulation(r);
            if (!enc) return std::nullopt;
            (void)enc->read_string(); // repository ID
            (void)enc->read_string(); // type name
            auto sw = parse_typecode(*enc, depth + 1);
            if (!sw) return std::nullopt;
            desc.switch_type = std::make_unique<TCDesc>(std::move(*sw));
            auto def_idx = enc->read_i32();
            if (!def_idx) return std::nullopt;
            desc.default_index = static_cast<int>(*def_idx);
            auto count = enc->read_u32();
            if (!count) return std::nullopt;
            for (uint32_t i = 0; i < *count; ++i) {
                auto label = read_typecode_label(*enc, *desc.switch_type);
                if (!label) return std::nullopt;
                auto name = enc->read_string();
                if (!name) return std::nullopt;
                auto mem = parse_typecode(*enc, depth + 1);
                if (!mem) return std::nullopt;
                desc.union_members.push_back({*label, *name, std::make_unique<TCDesc>(std::move(*mem))});
            }
            break;
        }
        case TCKind::tk_enum: {
            auto enc = read_typecode_encapsulation(r);
            if (!enc) return std::nullopt;
            (void)enc->read_string(); // repository ID
            (void)enc->read_string(); // type name
            auto count = enc->read_u32();
            if (!count) return std::nullopt;
            for (uint32_t i = 0; i < *count; ++i) {
                auto name = enc->read_string();
                if (!name) return std::nullopt;
                desc.enum_members.push_back(*name);
            }
            break;
        }
        case TCKind::tk_alias: {
            auto enc = read_typecode_encapsulation(r);
            if (!enc) return std::nullopt;
            (void)enc->read_string(); // repository ID
            (void)enc->read_string(); // alias name
            auto orig = parse_typecode(*enc, depth + 1);
            if (!orig) return std::nullopt;
            desc = std::move(*orig);
            break;
        }
        default:
            break;
    }
    return desc;
}

static std::optional<TCDesc> parse_typecode_from_cursor(CdrCursor& c) {
    c.align(4);
    TypeCodeReader r(c.get_buf(), c.get_pos(), c.get_buf_len(), c.is_little_endian());
    auto desc = parse_typecode(r);
    if (!desc) return std::nullopt;
    c.set_pos(r.pos());
    return desc;
}

static std::optional<int32_t> read_discriminator_value(CdrCursor& c, const TCDesc& switch_desc) {
    switch (static_cast<TCKind>(switch_desc.kind)) {
        case TCKind::tk_boolean: {
            auto v = c.read_u8();
            return v ? std::optional<int32_t>(*v ? 1 : 0) : std::nullopt;
        }
        case TCKind::tk_short: {
            auto v = c.read_i16();
            return v ? std::optional<int32_t>(*v) : std::nullopt;
        }
        case TCKind::tk_long:
        case TCKind::tk_enum: {
            auto v = c.read_i32();
            return v;
        }
        case TCKind::tk_ushort: {
            auto v = c.read_u16();
            return v ? std::optional<int32_t>(static_cast<int32_t>(*v)) : std::nullopt;
        }
        case TCKind::tk_ulong: {
            auto v = c.read_u32();
            return v ? std::optional<int32_t>(static_cast<int32_t>(*v)) : std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

std::optional<std::string> decode_any_value_from_typecode(CdrCursor& c, const TCDesc& desc, const IdlRegistry* registry) {
    switch (static_cast<TCKind>(desc.kind)) {
        case TCKind::tk_null: return std::string("null");
        case TCKind::tk_void: return std::string("void");
        case TCKind::tk_short: {
            auto v = c.read_i16();
            return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
        }
        case TCKind::tk_long: {
            auto v = c.read_i32();
            return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
        }
        case TCKind::tk_ushort: {
            auto v = c.read_u16();
            return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
        }
        case TCKind::tk_ulong: {
            auto v = c.read_u32();
            return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
        }
        case TCKind::tk_float: {
            auto v = c.read_f32();
            return v ? std::optional<std::string>(format_f32(*v)) : std::nullopt;
        }
        case TCKind::tk_double: {
            auto v = c.read_f64();
            return v ? std::optional<std::string>(format_f64(*v)) : std::nullopt;
        }
        case TCKind::tk_boolean: {
            auto v = c.read_u8();
            return v ? std::optional<std::string>(*v ? "true" : "false") : std::nullopt;
        }
        case TCKind::tk_char:
        case TCKind::tk_octet: {
            auto v = c.read_u8();
            return v ? std::optional<std::string>(format_hex_byte(*v)) : std::nullopt;
        }
        case TCKind::tk_wchar: {
            auto v = c.read_u16();
            return v ? std::optional<std::string>(format_wchar(*v)) : std::nullopt;
        }
        case TCKind::tk_longlong: {
            auto v = c.read_i64();
            return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
        }
        case TCKind::tk_ulonglong: {
            auto v = c.read_u64();
            return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
        }
        case TCKind::tk_string: {
            auto s = c.read_string();
            return s ? std::optional<std::string>("\"" + *s + "\"") : std::nullopt;
        }
        case TCKind::tk_wstring: {
            auto s = c.read_wstring();
            return s ? std::optional<std::string>("\"" + *s + "\"") : std::nullopt;
        }
        case TCKind::tk_sequence: {
            if (!desc.element_type) return std::nullopt;
            auto len_opt = c.read_u32();
            if (!len_opt) return std::nullopt;
            size_t len = std::min(size_t(*len_opt), MAX_DECODE_ELEMENTS);
            std::vector<std::string> parts;
            for (size_t i = 0; i < len; ++i) {
                auto v = decode_any_value_from_typecode(c, *desc.element_type, registry);
                if (!v) break;
                parts.push_back(*v);
            }
            if (len_opt && *len_opt > len) {
                size_t skip = *len_opt - len;
                for (size_t i = 0; i < skip && desc.element_type; ++i) {
                    (void)decode_any_value_from_typecode(c, *desc.element_type, registry);
                }
            }
            std::string r = "[";
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i) r += ", ";
                r += parts[i];
            }
            if (len_opt && *len_opt > parts.size()) r += " ...";
            r += "]";
            return r;
        }
        case TCKind::tk_array: {
            if (!desc.element_type || desc.array_dim == 0) return std::nullopt;
            size_t n = desc.array_dim;
            if (n > MAX_SEQ_LEN) return std::nullopt;
            std::vector<std::string> parts;
            for (size_t i = 0; i < n; ++i) {
                auto v = decode_any_value_from_typecode(c, *desc.element_type, registry);
                if (!v) return std::nullopt;
                parts.push_back(*v);
            }
            std::string r = "[";
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i) r += ", ";
                r += parts[i];
            }
            r += "]";
            return r;
        }
        case TCKind::tk_struct:
        case TCKind::tk_except: {
            std::vector<std::string> parts;
            for (const auto& [name, mem] : desc.struct_members) {
                if (!mem) return std::nullopt;
                auto v = decode_any_value_from_typecode(c, *mem, registry);
                if (!v) return std::nullopt;
                parts.push_back(name + ": " + *v);
            }
            std::string r = "{ ";
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i) r += ", ";
                r += parts[i];
            }
            r += " }";
            return r;
        }
        case TCKind::tk_union: {
            if (!desc.switch_type) return std::nullopt;
            auto label_opt = read_discriminator_value(c, *desc.switch_type);
            if (!label_opt) return std::nullopt;
            int32_t label_val = *label_opt;
            const std::string* branch_name = nullptr;
            const TCDesc* branch_type = nullptr;
            for (size_t i = 0; i < desc.union_members.size(); ++i) {
                if (std::get<0>(desc.union_members[i]) == label_val) {
                    branch_name = &std::get<1>(desc.union_members[i]);
                    branch_type = std::get<2>(desc.union_members[i]).get();
                    break;
                }
            }
            if (!branch_type && desc.default_index >= 0 && static_cast<size_t>(desc.default_index) < desc.union_members.size()) {
                branch_name = &std::get<1>(desc.union_members[static_cast<size_t>(desc.default_index)]);
                branch_type = std::get<2>(desc.union_members[static_cast<size_t>(desc.default_index)]).get();
            }
            if (!branch_type) return std::optional<std::string>("union(disc=" + std::to_string(label_val) + ", <no match>)");
            auto val = decode_any_value_from_typecode(c, *branch_type, registry);
            if (!val) return std::nullopt;
            return "union(disc=" + std::to_string(label_val) + ", " + *branch_name + ": " + *val + ")";
        }
        case TCKind::tk_enum: {
            auto v = c.read_i32();
            if (!v) return std::nullopt;
            size_t idx = static_cast<size_t>(*v);
            if (idx < desc.enum_members.size())
                return desc.enum_members[idx];
            return std::to_string(*v);
        }
        case TCKind::tk_object: {
            auto s = c.read_string();
            return s ? std::optional<std::string>("Object(\"" + *s + "\")") : std::nullopt;
        }
        case TCKind::tk_any: {
            auto inner = parse_typecode_from_cursor(c);
            if (!inner) return std::nullopt;
            auto val = decode_any_value_from_typecode(c, *inner, registry);
            return val ? std::optional<std::string>("any(" + *val + ")") : std::nullopt;
        }
        default:
            return std::optional<std::string>("any(tk=" + std::to_string(desc.kind) + ")");
    }
}

static std::string format_f32(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << v;
    return oss.str();
}

static std::string format_f64(double v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << v;
    return oss.str();
}

static std::string format_hex_byte(uint8_t b) {
    char buf[5];
    std::snprintf(buf, sizeof(buf), "0x%02x", b);
    return buf;
}

static std::string format_wchar(uint16_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "U+%04X", v);
    return buf;
}

// IdlRegistry method stubs — these are resolved at link time against the real class.
// We access them through the pointer only when non-null.

static constexpr unsigned int MAX_INDIRECTION_DEPTH = 32;

std::optional<std::string> CdrCursor::decode_type(const std::string& type_name,
                                                   const IdlRegistry* registry) {
    return decode_type_impl(type_name, registry, 0);
}

std::optional<std::string> CdrCursor::decode_type_impl(const std::string& type_name,
                                                       const IdlRegistry* registry,
                                                       unsigned int indirection_depth) {
    if (indirection_depth > MAX_INDIRECTION_DEPTH)
        return std::nullopt;

    // sequence<T>
    if (type_name.size() > 9 &&
        type_name.substr(0, 9) == "sequence<" &&
        type_name.back() == '>') {
        return decode_sequence(type_name, registry);
    }

    // array<T,N>
    if (type_name.size() > 6 &&
        type_name.substr(0, 6) == "array<" &&
        type_name.back() == '>') {
        return decode_array(type_name, registry);
    }

    if (type_name == "string") {
        auto s = read_string();
        if (!s) return std::nullopt;
        return "\"" + *s + "\"";
    }
    if (type_name == "wstring") {
        auto s = read_wstring();
        if (!s) return std::nullopt;
        return "\"" + *s + "\"";
    }
    if (type_name == "long") {
        auto v = read_i32();
        return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
    }
    if (type_name == "unsigned long") {
        auto v = read_u32();
        return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
    }
    if (type_name == "short") {
        auto v = read_i16();
        return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
    }
    if (type_name == "unsigned short") {
        auto v = read_u16();
        return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
    }
    if (type_name == "long long") {
        auto v = read_i64();
        return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
    }
    if (type_name == "unsigned long long") {
        auto v = read_u64();
        return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
    }
    if (type_name == "boolean") {
        auto v = read_u8();
        if (!v) return std::nullopt;
        return std::string(*v != 0 ? "true" : "false");
    }
    if (type_name == "octet" || type_name == "char") {
        auto v = read_u8();
        if (!v) return std::nullopt;
        return format_hex_byte(*v);
    }
    if (type_name == "wchar") {
        auto v = read_u16();
        if (!v) return std::nullopt;
        return format_wchar(*v);
    }
    if (type_name == "float") {
        auto v = read_f32();
        if (!v) return std::nullopt;
        return format_f32(*v);
    }
    if (type_name == "double") {
        auto v = read_f64();
        if (!v) return std::nullopt;
        return format_f64(*v);
    }
    if (type_name == "fixed") {
        auto digits = read_u16();
        auto scale = read_i16();
        if (!digits || !scale) return std::nullopt;
        return "fixed(" + std::to_string(*digits) + "," + std::to_string(*scale) + ")";
    }
    if (type_name == "void") {
        return std::string("void");
    }
    if (type_name == "any") {
        auto desc = parse_typecode_from_cursor(*this);
        if (!desc) return std::optional<std::string>("any(<tc parse error>)");
        auto val = decode_any_value_from_typecode(*this, *desc, registry);
        if (!val) return std::optional<std::string>("any(<decode error>)");
        return "any(" + *val + ")";
    }
    if (type_name == "Object") {
        auto s = read_string();
        if (!s) return std::nullopt;
        return "Object(\"" + *s + "\")";
    }

    if (registry) {
        auto* resolved = registry->resolve_typedef(type_name);
        if (resolved) {
            return decode_type(*resolved, registry);
        }
        auto* fields = registry->get_struct_fields(type_name);
        if (fields) {
            return decode_struct(*fields, registry);
        }
        auto* union_def = registry->get_union_def(type_name);
        if (union_def) {
            return decode_union(*union_def, registry);
        }
        if (registry->is_enum(type_name)) {
            auto v = read_i32();
            if (!v) return std::nullopt;
            return registry->get_enum_name(type_name, *v);
        }
    }

    return std::nullopt;
}

std::optional<std::string> CdrCursor::decode_struct(const StructFields& fields,
                                                     const IdlRegistry* registry) {
    std::vector<std::string> parts;
    parts.reserve(fields.size());
    bool prev_was_string = false;
    for (auto& [fname, ftype] : fields) {
        auto v = decode_struct_field(ftype, prev_was_string, registry);
        if (!v) return std::nullopt;
        prev_was_string = (ftype == "string" || ftype == "wstring");
        parts.push_back(fname + ": " + *v);
    }
    std::string result = "{ ";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += ", ";
        result += parts[i];
    }
    result += " }";
    return result;
}

std::optional<std::string> CdrCursor::decode_struct_field(const std::string& ftype,
                                                           bool prev_was_string,
                                                           const IdlRegistry* registry) {
    if (ftype == "double" && prev_was_string) {
        align(8);
        auto v = read_f64_no_align();
        if (!v) return std::nullopt;
        return format_f64(*v);
    }
    if ((ftype == "long long" || ftype == "unsigned long long") && prev_was_string) {
        align(4);
        if (ftype == "long long") {
            auto v = read_i64_no_align();
            return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
        } else {
            auto v = read_u64_no_align();
            return v ? std::optional<std::string>(std::to_string(*v)) : std::nullopt;
        }
    }
    return decode_type(ftype, registry);
}

std::optional<std::string> CdrCursor::decode_sequence(const std::string& type_name,
                                                       const IdlRegistry* registry) {
    std::string element_type = type_name.substr(9, type_name.size() - 10);
    while (!element_type.empty() && element_type.front() == ' ') element_type.erase(element_type.begin());
    while (!element_type.empty() && element_type.back() == ' ') element_type.pop_back();

    auto len_opt = read_u32();
    if (!len_opt) return std::nullopt;
    size_t len = *len_opt;

    if (len > MAX_SEQ_LEN) return std::nullopt;

    if (element_type == "octet") {
        if (remaining() < len) return std::nullopt;
        size_t show = std::min(len, MAX_DISPLAY);
        std::string result = "[";
        for (size_t i = 0; i < show; ++i) {
            if (i > 0) result += ", ";
            result += format_hex_byte(buf_[pos_ + i]);
        }
        if (len > MAX_DISPLAY)
            result += " ... (+" + std::to_string(len - MAX_DISPLAY) + " more)";
        result += "]";
        pos_ += len;
        return result;
    }

    size_t to_decode = std::min(len, MAX_DECODE_ELEMENTS);
    std::vector<std::string> elements;
    elements.reserve(to_decode);
    for (size_t i = 0; i < to_decode; ++i) {
        auto v = decode_type(element_type, registry);
        if (!v) return std::nullopt;
        elements.push_back(std::move(*v));
    }

    size_t show = std::min(elements.size(), MAX_DISPLAY);
    std::string result = "[";
    for (size_t i = 0; i < show; ++i) {
        if (i > 0) result += ", ";
        result += elements[i];
    }
    if (len > show)
        result += " ... (+" + std::to_string(len - show) + " more)";
    result += "]";
    return result;
}

std::optional<std::string> CdrCursor::decode_array(const std::string& type_name,
                                                    const IdlRegistry* registry) {
    auto inner = type_name.substr(6, type_name.size() - 7);
    auto comma_pos = inner.rfind(',');
    if (comma_pos == std::string::npos) return std::nullopt;

    std::string element_type = inner.substr(0, comma_pos);
    while (!element_type.empty() && element_type.back() == ' ') element_type.pop_back();
    size_t array_size = 0;
    try { array_size = std::stoul(inner.substr(comma_pos + 1)); }
    catch (...) { return std::nullopt; }

    if (array_size > MAX_SEQ_LEN) return std::nullopt;

    std::vector<std::string> elements;
    elements.reserve(array_size);
    for (size_t i = 0; i < array_size; ++i) {
        auto v = decode_type(element_type, registry);
        if (!v) return std::nullopt;
        elements.push_back(std::move(*v));
    }

    size_t show = std::min(elements.size(), MAX_DISPLAY);
    std::string result = "[";
    for (size_t i = 0; i < show; ++i) {
        if (i > 0) result += ", ";
        result += elements[i];
    }
    if (array_size > show)
        result += " ... (+" + std::to_string(array_size - show) + " more)";
    result += "]";
    return result;
}

std::optional<std::string> CdrCursor::decode_union(const UnionDef& def,
                                                    const IdlRegistry* registry) {
    auto disc_val = decode_type(def.discriminator_type, registry);
    if (!disc_val) return std::nullopt;

    std::string disc_str = *disc_val;
    const UnionBranch* matched = nullptr;
    const UnionBranch* default_branch = nullptr;

    for (const auto& branch : def.branches) {
        for (const auto& label : branch.case_labels) {
            if (label == "default") { default_branch = &branch; continue; }
            if (label == disc_str) { matched = &branch; break; }
        }
        if (matched) break;
    }

    if (!matched) matched = default_branch;
    if (!matched) return "union(disc=" + disc_str + ", <no match>)";

    auto val = decode_type(matched->type_name, registry);
    if (!val) return "union(disc=" + disc_str + ", <decode error>)";

    return "union(disc=" + disc_str + ", " + matched->field_name + ": " + *val + ")";
}

} // anonymous namespace

std::vector<DecodedParam> decode_request_params(
        const uint8_t* body, size_t body_len, size_t params_offset,
        const OpSignature& sig, bool little_endian,
        const IdlRegistry* registry) {
    CdrCursor cursor(body, body_len, params_offset, little_endian);
    std::vector<DecodedParam> result;

    for (auto& param : sig.params) {
        if (param.direction == ParamDir::Out) continue;
        auto value = cursor.decode_type(param.type_name, registry);
        if (value) {
            result.push_back({param.name, param.type_name, std::move(*value)});
        } else {
            result.push_back({param.name, param.type_name,
                              "<decode error: " + param.type_name + ">"});
            break;
        }
    }
    return result;
}

std::optional<DecodedParam> decode_reply_return(
        const uint8_t* body, size_t body_len, size_t reply_body_offset,
        const OpSignature& sig, bool little_endian,
        const IdlRegistry* registry) {
    if (sig.return_type == "void") return std::nullopt;
    CdrCursor cursor(body, body_len, reply_body_offset, little_endian);
    auto value = cursor.decode_type(sig.return_type, registry);
    if (!value) return std::nullopt;
    return DecodedParam{"return", sig.return_type, std::move(*value)};
}

ReplyDecodeResult decode_reply_params(
        const uint8_t* body, size_t body_len, size_t reply_body_offset,
        const OpSignature& sig, bool little_endian,
        const IdlRegistry* registry) {
    ReplyDecodeResult result;
    CdrCursor cursor(body, body_len, reply_body_offset, little_endian);

    if (sig.return_type != "void") {
        auto value = cursor.decode_type(sig.return_type, registry);
        if (value) {
            result.return_value = DecodedParam{"return", sig.return_type, std::move(*value)};
        }
    }

    for (auto& param : sig.params) {
        if (param.direction == ParamDir::Out || param.direction == ParamDir::InOut) {
            auto value = cursor.decode_type(param.type_name, registry);
            if (!value) break;
            result.out_params.push_back({param.name, param.type_name, std::move(*value)});
        }
    }

    return result;
}
