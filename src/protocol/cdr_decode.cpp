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

namespace {

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

    std::optional<std::string> decode_type(const std::string& type_name,
                                           const IdlRegistry* registry);

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

    const uint8_t* buf_;
    size_t buf_len_;
    size_t base_;
    size_t pos_;
    bool le_;
};


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

std::optional<std::string> CdrCursor::decode_type(const std::string& type_name,
                                                   const IdlRegistry* registry) {
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

    if (type_name == "string" || type_name == "wstring") {
        auto s = read_string();
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
        auto tc_kind = read_u32();
        if (!tc_kind) return std::nullopt;
        return "any(tk=" + std::to_string(*tc_kind) + ")";
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
