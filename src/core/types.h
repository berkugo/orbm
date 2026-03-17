// ORBM core shared types and threading primitives.
//
// This header contains:
// - wire-level CORBA / GIOP metadata (`GiopMessage`, `CorbaNamingEntry`)
// - IDL-derived operation signatures (`OpSignature`, `IdlParam`)
// - small synchronization helpers (`SharedData<T>`, `Channel<T>`)
// - JSON serialization glue for the HTTP / WebSocket API.
//
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>

class IdlRegistry; // forward decl — defined in idl/idl_parser.h

// ─── Enums ────────────────────────────────────────────────────────────────

enum class GiopMessageType : uint8_t {
    Request = 0, Reply = 1, CancelRequest = 2, LocateRequest = 3,
    LocateReply = 4, CloseConnection = 5, MessageError = 6, Fragment = 7,
    Unknown = 255
};

inline GiopMessageType giop_type_from_byte(uint8_t b) {
    return (b <= 7) ? static_cast<GiopMessageType>(b) : GiopMessageType::Unknown;
}

inline std::string to_string(GiopMessageType t) {
    switch (t) {
        case GiopMessageType::Request:        return "REQUEST";
        case GiopMessageType::Reply:          return "REPLY";
        case GiopMessageType::CancelRequest:  return "CANCEL_REQUEST";
        case GiopMessageType::LocateRequest:  return "LOCATE_REQUEST";
        case GiopMessageType::LocateReply:    return "LOCATE_REPLY";
        case GiopMessageType::CloseConnection:return "CLOSE_CONNECTION";
        case GiopMessageType::MessageError:   return "MESSAGE_ERROR";
        case GiopMessageType::Fragment:       return "FRAGMENT";
        default:                              return "UNKNOWN";
    }
}

enum class ReplyStatus : uint32_t {
    NoException = 0, UserException = 1, SystemException = 2,
    LocationForward = 3, Unknown = 0xFFFFFFFF
};

inline ReplyStatus reply_status_from_u32(uint32_t v) {
    return (v <= 3) ? static_cast<ReplyStatus>(v) : ReplyStatus::Unknown;
}

inline std::string to_string(ReplyStatus s) {
    switch (s) {
        case ReplyStatus::NoException:    return "NO_EXCEPTION";
        case ReplyStatus::UserException:  return "USER_EXCEPTION";
        case ReplyStatus::SystemException:return "SYSTEM_EXCEPTION";
        case ReplyStatus::LocationForward:return "LOCATION_FORWARD";
        default:                          return "UNKNOWN";
    }
}

enum class MessageDirection { ClientToServer, ServerToClient, Unknown };

inline std::string to_string(MessageDirection d) {
    switch (d) {
        case MessageDirection::ClientToServer: return "CLIENT_TO_SERVER";
        case MessageDirection::ServerToClient: return "SERVER_TO_CLIENT";
        default:                               return "UNKNOWN";
    }
}

enum class ParamDir { In, Out, InOut };

// ─── Data structs ─────────────────────────────────────────────────────────

struct DecodedParam {
    std::string name;
    std::string type_name;
    std::string value;
};

struct CorbaNamingEntry {
    std::string path;
    std::string type_id;
    std::string ior;
    std::string host;
    uint16_t port = 0;
    std::string object_key_hex;
    std::string giop_version = "1.2";
};

struct GiopMessage {
    uint64_t id = 0;
    uint64_t timestamp_ms = 0;
    std::string src_ip;
    uint16_t src_port = 0;
    std::string dst_ip;
    uint16_t dst_port = 0;
    std::string giop_version;
    GiopMessageType msg_type = GiopMessageType::Unknown;
    uint32_t request_id = 0;
    uint32_t size_bytes = 0;
    std::optional<std::string> operation;
    std::optional<std::string> object_key_hex;
    std::optional<ReplyStatus> reply_status;
    bool is_oneway = false;
    std::optional<std::string> object_path;
    std::optional<std::string> object_type_id;
    MessageDirection direction = MessageDirection::Unknown;
    std::optional<uint64_t> matched_id;
    std::optional<double> latency_ms;
    std::string raw_hex;
    bool raw_hex_truncated = false;
    std::optional<uint32_t> params_offset;
    std::optional<std::string> params_hex;
    bool params_hex_truncated = false;
    std::optional<std::vector<DecodedParam>> params;
    std::optional<DecodedParam> return_value;
    std::optional<std::vector<DecodedParam>> out_params;
};

struct IdlParam {
    std::string name;
    std::string type_name;
    ParamDir direction = ParamDir::In;
};

struct OpSignature {
    std::string name;
    std::string return_type;
    std::vector<IdlParam> params;
    bool oneway = false;
    std::string interface_name;
    std::string module_name;
};

using StructFields = std::vector<std::pair<std::string, std::string>>;

// ─── JSON serialization ───────────────────────────────────────────────────

inline void to_json(nlohmann::json& j, const DecodedParam& p) {
    j = {{"name", p.name}, {"type_name", p.type_name}, {"value", p.value}};
}

inline void to_json(nlohmann::json& j, const CorbaNamingEntry& e) {
    j = {{"path", e.path}, {"type_id", e.type_id}, {"ior", e.ior},
         {"host", e.host}, {"port", e.port},
         {"object_key_hex", e.object_key_hex}, {"giop_version", e.giop_version}};
}

inline void to_json(nlohmann::json& j, const GiopMessage& m) {
    j = {
        {"id", m.id}, {"timestamp_ms", m.timestamp_ms},
        {"src_ip", m.src_ip}, {"src_port", m.src_port},
        {"dst_ip", m.dst_ip}, {"dst_port", m.dst_port},
        {"giop_version", m.giop_version},
        {"msg_type", to_string(m.msg_type)},
        {"request_id", m.request_id}, {"size_bytes", m.size_bytes},
        {"is_oneway", m.is_oneway},
        {"direction", to_string(m.direction)},
        {"raw_hex", m.raw_hex},
        {"raw_hex_truncated", m.raw_hex_truncated},
        {"params_hex_truncated", m.params_hex_truncated},
    };
    auto set_opt = [&](const char* key, const auto& opt) {
        if (opt.has_value()) j[key] = opt.value(); else j[key] = nullptr;
    };
    set_opt("operation", m.operation);
    set_opt("object_key_hex", m.object_key_hex);
    if (m.reply_status) j["reply_status"] = to_string(*m.reply_status);
    else j["reply_status"] = nullptr;
    set_opt("object_path", m.object_path);
    set_opt("object_type_id", m.object_type_id);
    set_opt("matched_id", m.matched_id);
    set_opt("latency_ms", m.latency_ms);
    if (m.params_offset) j["params_offset"] = *m.params_offset;
    if (m.params_hex) j["params_hex"] = *m.params_hex;
    if (m.params) j["params"] = *m.params;
    if (m.return_value) j["return_value"] = *m.return_value;
    if (m.out_params && !m.out_params->empty()) j["out_params"] = *m.out_params;
}

// ─── Thread-safe shared data wrapper ──────────────────────────────────────

template<typename T>
class SharedData {
public:
    SharedData() = default;
    explicit SharedData(T initial) : data_(std::move(initial)) {}

    T get() const { std::shared_lock lk(mu_); return data_; }
    void set(T v) { std::unique_lock lk(mu_); data_ = std::move(v); }

    template<typename F>
    auto read(F&& f) const -> decltype(f(std::declval<const T&>())) {
        std::shared_lock lk(mu_);
        return f(data_);
    }

    template<typename F>
    auto write(F&& f) -> decltype(f(std::declval<T&>())) {
        std::unique_lock lk(mu_);
        return f(data_);
    }

private:
    mutable std::shared_mutex mu_;
    T data_{};
};

using SharedObjects = std::shared_ptr<SharedData<std::vector<CorbaNamingEntry>>>;
using SharedLookup  = std::shared_ptr<SharedData<std::unordered_map<std::string, CorbaNamingEntry>>>;
using SharedPortMap = std::shared_ptr<SharedData<std::unordered_map<uint16_t, std::vector<CorbaNamingEntry>>>>;

// ─── Thread-safe message channel ──────────────────────────────────────────

template<typename T>
class Channel {
public:
    void send(T item) {
        std::lock_guard lk(mu_);
        q_.push(std::move(item));
        cv_.notify_one();
    }

    std::optional<T> recv() {
        std::unique_lock lk(mu_);
        cv_.wait(lk, [this]{ return !q_.empty() || closed_; });
        if (q_.empty()) return std::nullopt;
        T item = std::move(q_.front());
        q_.pop();
        return item;
    }

    bool try_recv(T& out) {
        std::lock_guard lk(mu_);
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    void close() {
        std::lock_guard lk(mu_);
        closed_ = true;
        cv_.notify_all();
    }

private:
    std::queue<T> q_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool closed_ = false;
};

// ─── WebSocket event ──────────────────────────────────────────────────────

struct WsEvent {
    enum Type { GiopMsg, RequestTimeout, ObjectsUpdated } type;
    std::optional<GiopMessage> giop_msg;
    std::optional<uint64_t> timeout_id;
    std::optional<std::vector<CorbaNamingEntry>> objects;
};

inline nlohmann::json ws_event_to_json(const WsEvent& ev) {
    nlohmann::json j;
    switch (ev.type) {
        case WsEvent::GiopMsg:
            j["type"] = "giop_message";
            j["data"] = *ev.giop_msg;
            break;
        case WsEvent::RequestTimeout:
            j["type"] = "request_timeout";
            j["data"] = {{"id", *ev.timeout_id}};
            break;
        case WsEvent::ObjectsUpdated:
            j["type"] = "objects_updated";
            j["data"] = *ev.objects;
            break;
    }
    return j;
}

// ─── Discovery config ─────────────────────────────────────────────────────

struct DiscoveryConfig {
    std::string ns_ref;
    std::string nslist_bin;
    std::string catior_bin;
    std::string ld_library_path;
    std::vector<std::string> orb_args;
};
