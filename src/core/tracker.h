// Request/Reply correlation and timeout tracking for ORBM.
//
// `Tracker` keeps a small in‑memory index of in‑flight GIOP requests so
// that replies can be matched later and per‑call latency can be computed.
// It also exposes a lightweight timeout mechanism used to emit synthetic
// \"request_timeout\" events to the UI.
//
#pragma once

#include "core/types.h"
#include <string>
#include <tuple>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <vector>
#include <optional>

class Tracker {
public:
    void track_request(const GiopMessage& msg) {
        if (msg.msg_type != GiopMessageType::Request) return;
        auto key = make_key(msg.src_ip, msg.src_port, msg.dst_ip, msg.dst_port, msg.request_id);
        {
            std::lock_guard lk(mu_);
            pending_[key] = {msg.id, clock::now(), msg.operation};
        }
        if (msg.operation) {
            std::lock_guard lk(op_mu_);
            op_map_[key] = *msg.operation;
        }
    }

    struct MatchResult { uint64_t request_id; double latency_ms; };

    std::optional<MatchResult> match_reply(const GiopMessage& msg) {
        if (msg.msg_type != GiopMessageType::Reply) return std::nullopt;
        auto key = make_key(msg.dst_ip, msg.dst_port, msg.src_ip, msg.src_port, msg.request_id);
        std::lock_guard lk(mu_);
        auto it = pending_.find(key);
        if (it == pending_.end()) return std::nullopt;
        auto& [id, ts, op] = it->second;
        double lat = std::chrono::duration<double, std::milli>(clock::now() - ts).count();
        uint64_t matched_id = id;
        pending_.erase(it);
        return MatchResult{matched_id, lat};
    }

    std::optional<std::string> get_operation_for_request_by_reply(
            const std::string& reply_src_ip, uint16_t reply_src_port,
            const std::string& reply_dst_ip, uint16_t reply_dst_port,
            uint32_t request_id) const {
        auto key = make_key(reply_dst_ip, reply_dst_port, reply_src_ip, reply_src_port, request_id);
        std::lock_guard lk(op_mu_);
        auto it = op_map_.find(key);
        if (it == op_map_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<uint64_t> check_timeouts() {
        auto now = clock::now();
        std::vector<uint64_t> timed_out;
        std::lock_guard lk(mu_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - std::get<1>(it->second));
            if (elapsed.count() >= TIMEOUT_SECS) {
                timed_out.push_back(std::get<0>(it->second));
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
        return timed_out;
    }

private:
    using clock = std::chrono::steady_clock;
    static constexpr int64_t TIMEOUT_SECS = 30;

    struct PendingKey {
        std::string src_ip;
        uint16_t src_port;
        std::string dst_ip;
        uint16_t dst_port;
        uint32_t request_id;

        bool operator==(const PendingKey& o) const {
            return src_ip == o.src_ip && src_port == o.src_port &&
                   dst_ip == o.dst_ip && dst_port == o.dst_port &&
                   request_id == o.request_id;
        }
    };

    struct PendingKeyHash {
        size_t operator()(const PendingKey& k) const {
            size_t h = std::hash<std::string>{}(k.src_ip);
            h ^= std::hash<uint16_t>{}(k.src_port) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::string>{}(k.dst_ip) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint16_t>{}(k.dst_port) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(k.request_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    using PendingEntry = std::tuple<uint64_t, clock::time_point, std::optional<std::string>>;

    static PendingKey make_key(const std::string& src, uint16_t sp,
                               const std::string& dst, uint16_t dp, uint32_t rid) {
        return {src, sp, dst, dp, rid};
    }

    std::unordered_map<PendingKey, PendingEntry, PendingKeyHash> pending_;
    mutable std::mutex mu_;

    std::unordered_map<PendingKey, std::string, PendingKeyHash> op_map_;
    mutable std::mutex op_mu_;
};
