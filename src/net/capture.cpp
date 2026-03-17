#include "net/capture.h"
#include "protocol/giop.h"
#include "protocol/cdr_decode.h"
#include "core/tracker.h"
#include "idl/idl_parser.h"

#include <pcap/pcap.h>
#include <cstring>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace {

constexpr size_t GIOP_HEADER_LEN = 12;
constexpr size_t MAX_RAW_BYTES = 4096;
constexpr size_t MAX_PARAMS_BYTES = 4096;

// DLT_* constants come from <pcap/pcap.h>
#ifndef DLT_LINUX_SLL2
#define DLT_LINUX_SLL2 276
#endif

uint64_t ts_ms() {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

struct StreamKey {
    std::string src_ip;
    uint16_t src_port;
    std::string dst_ip;
    uint16_t dst_port;

    bool operator==(const StreamKey& o) const {
        return src_ip == o.src_ip && src_port == o.src_port &&
               dst_ip == o.dst_ip && dst_port == o.dst_port;
    }
};

struct StreamKeyHash {
    size_t operator()(const StreamKey& k) const {
        size_t h = std::hash<std::string>{}(k.src_ip);
        h ^= std::hash<uint16_t>{}(k.src_port) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.dst_ip) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint16_t>{}(k.dst_port) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct ParsedPacket {
    std::string src_ip;
    std::string dst_ip;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
};

std::string ipv4_to_string(uint32_t addr) {
    char buf[INET_ADDRSTRLEN];
    struct in_addr a;
    a.s_addr = addr;
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

std::string ipv6_to_string(const uint8_t* addr) {
    char buf[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, addr, buf, sizeof(buf));
    return buf;
}

bool parse_ip_tcp(const uint8_t* data, size_t len, ParsedPacket& out) {
    if (len < 20) return false;

    uint8_t version = (data[0] >> 4) & 0x0F;
    if (version == 4) {
        if (len < 20) return false;
        size_t ihl = (data[0] & 0x0F) * 4;
        if (ihl < 20 || len < ihl) return false;
        uint8_t proto = data[9];
        if (proto != 6) return false; // TCP
        uint16_t total_len = ntohs(*reinterpret_cast<const uint16_t*>(data + 2));
        if (total_len > len) total_len = static_cast<uint16_t>(len);

        uint32_t src_addr, dst_addr;
        std::memcpy(&src_addr, data + 12, 4);
        std::memcpy(&dst_addr, data + 16, 4);
        out.src_ip = ipv4_to_string(src_addr);
        out.dst_ip = ipv4_to_string(dst_addr);

        const uint8_t* tcp = data + ihl;
        size_t tcp_len = total_len - ihl;
        if (tcp_len < 20) return false;
        out.src_port = ntohs(*reinterpret_cast<const uint16_t*>(tcp));
        out.dst_port = ntohs(*reinterpret_cast<const uint16_t*>(tcp + 2));
        size_t tcp_hdr_len = ((tcp[12] >> 4) & 0x0F) * 4;
        if (tcp_hdr_len < 20 || tcp_hdr_len > tcp_len) return false;
        out.payload = tcp + tcp_hdr_len;
        out.payload_len = tcp_len - tcp_hdr_len;
        return true;
    } else if (version == 6) {
        if (len < 40) return false;
        uint16_t payload_length = ntohs(*reinterpret_cast<const uint16_t*>(data + 4));
        uint8_t next_header = data[6];

        out.src_ip = ipv6_to_string(data + 8);
        out.dst_ip = ipv6_to_string(data + 24);

        const uint8_t* next = data + 40;
        size_t remaining = len - 40;
        if (payload_length < remaining)
            remaining = payload_length;

        while (next_header != 6 && remaining > 2) {
            if (next_header == 0 || next_header == 43 || next_header == 44 ||
                next_header == 60 || next_header == 51 || next_header == 50) {
                size_t ext_len = 8 + next[1] * 8;
                if (ext_len > remaining) return false;
                next_header = next[0];
                next += ext_len;
                remaining -= ext_len;
            } else {
                return false;
            }
        }
        if (next_header != 6 || remaining < 20) return false;

        out.src_port = ntohs(*reinterpret_cast<const uint16_t*>(next));
        out.dst_port = ntohs(*reinterpret_cast<const uint16_t*>(next + 2));
        size_t tcp_hdr_len = ((next[12] >> 4) & 0x0F) * 4;
        if (tcp_hdr_len < 20 || tcp_hdr_len > remaining) return false;
        out.payload = next + tcp_hdr_len;
        out.payload_len = remaining - tcp_hdr_len;
        return true;
    }
    return false;
}

bool parse_packet_by_linktype(int linktype, const uint8_t* data, size_t len, ParsedPacket& out) {
    switch (linktype) {
    case DLT_EN10MB: {
        if (len < 14) return false;
        uint16_t ethertype = ntohs(*reinterpret_cast<const uint16_t*>(data + 12));
        size_t offset = 14;
        if (ethertype == 0x8100 && len >= 18) {
            ethertype = ntohs(*reinterpret_cast<const uint16_t*>(data + 16));
            offset = 18;
        }
        if (ethertype != 0x0800 && ethertype != 0x86DD) return false;
        return parse_ip_tcp(data + offset, len - offset, out);
    }
    case DLT_RAW:
    case DLT_IPV4:
    case DLT_IPV6:
        return parse_ip_tcp(data, len, out);
    case DLT_LINUX_SLL:
        if (len < 16) return false;
        return parse_ip_tcp(data + 16, len - 16, out);
    case DLT_LINUX_SLL2:
        if (len < 20) return false;
        return parse_ip_tcp(data + 20, len - 20, out);
    default:
        if (len >= 14) {
            if (parse_ip_tcp(data + 14, len - 14, out)) return true;
        }
        return parse_ip_tcp(data, len, out);
    }
}

struct ExtractResult {
    GiopMessage msg;
    std::optional<uint16_t> learned_client_port;
};

std::optional<ExtractResult> extract_giop_message(
    std::vector<uint8_t>& buf,
    const StreamKey& key,
    uint64_t timestamp_ms,
    const SharedLookup& lookup,
    const SharedPortMap& port_map,
    const std::unordered_set<uint16_t>& client_ports,
    Tracker& tracker,
    uint64_t msg_id,
    const IdlRegistry* idl_registry)
{
    if (buf.size() < GIOP_HEADER_LEN) return std::nullopt;

    auto hdr_opt = parse_giop_header(buf.data(), buf.size());
    if (!hdr_opt) return std::nullopt;
    auto& hdr = *hdr_opt;

    size_t total_len = GIOP_HEADER_LEN + hdr.message_size;
    if (buf.size() < total_len) return std::nullopt;

    const uint8_t* body = buf.data() + GIOP_HEADER_LEN;
    size_t body_len = hdr.message_size;

    auto msg_type = giop_type_from_byte(hdr.msg_type_byte);

    std::optional<std::string> operation;
    std::optional<std::string> object_key_hex;
    std::optional<ReplyStatus> reply_status;
    uint32_t request_id = 0;
    bool is_oneway = false;
    std::optional<std::vector<DecodedParam>> params;
    std::optional<DecodedParam> return_value;
    std::optional<std::vector<DecodedParam>> out_params;
    std::optional<uint32_t> params_offset_opt;
    std::optional<std::string> params_hex;
    bool params_hex_truncated = false;

    std::unordered_set<uint16_t> server_ports;
    port_map->read([&](const auto& m) {
        for (const auto& [p, _] : m) server_ports.insert(p);
    });

    MessageDirection direction = MessageDirection::Unknown;
    std::optional<uint16_t> learned_client_port;

    switch (msg_type) {
    case GiopMessageType::Request:
        if (server_ports.count(key.dst_port)) {
            direction = MessageDirection::ClientToServer;
            learned_client_port = key.src_port;
        } else if (client_ports.count(key.dst_port)) {
            direction = MessageDirection::ServerToClient;
        } else if (!server_ports.count(key.src_port)) {
            direction = MessageDirection::ServerToClient;
            learned_client_port = key.dst_port;
        } else {
            direction = MessageDirection::ClientToServer;
        }
        break;
    case GiopMessageType::Reply:
        direction = MessageDirection::ServerToClient;
        break;
    default:
        direction = MessageDirection::Unknown;
        break;
    }

    switch (msg_type) {
    case GiopMessageType::Request: {
        auto rb = parse_request_body(body, body_len, hdr.major, hdr.minor, hdr.little_endian);
        operation = rb.operation;
        object_key_hex = rb.object_key_hex;
        is_oneway = rb.is_oneway;
        request_id = rb.request_id;
        params_offset_opt = static_cast<uint32_t>(rb.params_offset);

        if (rb.params_offset < body_len) {
            size_t end = std::min(rb.params_offset + MAX_PARAMS_BYTES, body_len);
            params_hex_truncated = end < body_len;
            params_hex = first_n_hex(body + rb.params_offset, end - rb.params_offset, MAX_PARAMS_BYTES);
        }

        if (rb.operation && idl_registry) {
            auto sig = idl_registry->lookup_operation(*rb.operation);
            if (sig && !sig->params.empty() && rb.params_offset < body_len) {
                auto decoded = decode_request_params(
                    body, body_len, rb.params_offset, *sig, hdr.little_endian, idl_registry);
                if (!decoded.empty())
                    params = std::move(decoded);
            }
        }
        break;
    }
    case GiopMessageType::Reply: {
        auto rb = parse_reply_body(body, body_len, hdr.major, hdr.minor, hdr.little_endian);
        request_id = rb.request_id;
        reply_status = rb.reply_status;

        auto op_name = tracker.get_operation_for_request_by_reply(
            key.src_ip, key.src_port, key.dst_ip, key.dst_port, rb.request_id);

        if (op_name && idl_registry) {
            auto sig = idl_registry->lookup_operation(*op_name);
            if (sig && rb.reply_body_offset < body_len) {
                auto reply_result = decode_reply_params(
                    body, body_len, rb.reply_body_offset, *sig, hdr.little_endian, idl_registry);
                return_value = reply_result.return_value;
                if (!reply_result.out_params.empty())
                    out_params = std::move(reply_result.out_params);
            }
        }
        break;
    }
    default:
        break;
    }

    std::optional<std::string> object_path;
    std::optional<std::string> object_type_id;
    if (object_key_hex) {
        lookup->read([&](const auto& m) {
            auto it = m.find(*object_key_hex);
            if (it != m.end()) {
                object_path = it->second.path;
                object_type_id = it->second.type_id;
            }
        });
    }

    if (!object_path && operation && idl_registry) {
        auto sig = idl_registry->lookup_operation(*operation);
        if (sig) object_path = sig->interface_name;
    }

    if (object_path) {
        port_map->read([&](const auto& m) {
            uint16_t server_port;
            switch (direction) {
            case MessageDirection::ClientToServer: server_port = key.dst_port; break;
            case MessageDirection::ServerToClient: server_port = key.src_port; break;
            default: server_port = key.dst_port; break;
            }
            auto it = m.find(server_port);
            if (it == m.end()) return;
            const auto& entries = it->second;
            for (const auto& e : entries) {
                if (e.path == *object_path) return;
            }
            std::string candidate = *object_path + "Service";
            for (const auto& e : entries) {
                if (e.path == candidate) { object_path = candidate; return; }
            }
        });
    }

    std::string giop_version = std::to_string(hdr.major) + "." + std::to_string(hdr.minor);

    size_t raw_end = std::min(total_len, MAX_RAW_BYTES);
    bool raw_hex_truncated = raw_end < total_len;
    std::string raw_hex = first_n_hex(buf.data(), raw_end, MAX_RAW_BYTES);

    GiopMessage msg;
    msg.id = msg_id;
    msg.timestamp_ms = timestamp_ms;
    msg.src_ip = key.src_ip;
    msg.src_port = key.src_port;
    msg.dst_ip = key.dst_ip;
    msg.dst_port = key.dst_port;
    msg.giop_version = std::move(giop_version);
    msg.msg_type = msg_type;
    msg.request_id = request_id;
    msg.size_bytes = hdr.message_size;
    msg.operation = std::move(operation);
    msg.object_key_hex = std::move(object_key_hex);
    msg.reply_status = std::move(reply_status);
    msg.is_oneway = is_oneway;
    msg.object_path = std::move(object_path);
    msg.object_type_id = std::move(object_type_id);
    msg.direction = direction;
    msg.raw_hex = std::move(raw_hex);
    msg.raw_hex_truncated = raw_hex_truncated;
    msg.params_offset = params_offset_opt;
    msg.params_hex = std::move(params_hex);
    msg.params_hex_truncated = params_hex_truncated;
    msg.params = std::move(params);
    msg.return_value = std::move(return_value);
    msg.out_params = std::move(out_params);

    switch (msg.msg_type) {
    case GiopMessageType::Request:
        tracker.track_request(msg);
        break;
    case GiopMessageType::Reply:
        if (auto match = tracker.match_reply(msg)) {
            msg.matched_id = match->request_id;
            msg.latency_ms = match->latency_ms;
        }
        break;
    default:
        break;
    }

    buf.erase(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(total_len));
    return ExtractResult{std::move(msg), learned_client_port};
}

} // anonymous namespace

std::string build_bpf_filter(const std::vector<uint16_t>& /*ports*/) {
    return "tcp";
}

void run_capture_blocking(
    const std::string& interface,
    const std::string& filter,
    SharedLookup lookup,
    SharedPortMap port_map,
    std::shared_ptr<Tracker> tracker,
    std::shared_ptr<Channel<GiopMessage>> msg_channel,
    std::shared_ptr<std::atomic<uint64_t>> message_id,
    std::shared_ptr<std::atomic<bool>> stop_flag,
    std::shared_ptr<const IdlRegistry> idl_registry)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t* handle = pcap_open_live(interface.c_str(), 65535, 0, 500, errbuf);
    if (!handle) {
        std::cerr << "pcap_open_live failed: " << errbuf << std::endl;
        return;
    }

    struct bpf_program fp;
    if (pcap_compile(handle, &fp, filter.c_str(), 1, PCAP_NETMASK_UNKNOWN) == 0) {
        pcap_setfilter(handle, &fp);
        pcap_freecode(&fp);
    }

    int linktype = pcap_datalink(handle);

    std::unordered_map<StreamKey, std::vector<uint8_t>, StreamKeyHash> buffers;
    std::unordered_set<uint16_t> client_ports;

    while (!stop_flag->load(std::memory_order_relaxed)) {
        struct pcap_pkthdr* pkt_header = nullptr;
        const uint8_t* pkt_data = nullptr;
        int res = pcap_next_ex(handle, &pkt_header, &pkt_data);
        if (res <= 0) continue;

        uint64_t ts = ts_ms();

        ParsedPacket parsed;
        if (!parse_packet_by_linktype(linktype, pkt_data, pkt_header->caplen, parsed))
            continue;
        if (parsed.payload_len == 0) continue;

        StreamKey key{parsed.src_ip, parsed.src_port, parsed.dst_ip, parsed.dst_port};
        auto& buf = buffers[key];
        buf.insert(buf.end(), parsed.payload, parsed.payload + parsed.payload_len);

        while (auto result = extract_giop_message(
            buf, key, ts, lookup, port_map, client_ports,
            *tracker, message_id->fetch_add(1, std::memory_order_relaxed), idl_registry.get()))
        {
            msg_channel->send(std::move(result->msg));
            if (result->learned_client_port)
                client_ports.insert(*result->learned_client_port);
        }
    }

    pcap_close(handle);
}
