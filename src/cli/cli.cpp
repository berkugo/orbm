#include "cli/cli.h"
#include "core/tracker.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <csignal>
#include <ctime>

namespace {

const char* const RESET  = "\033[0m";
const char* const BOLD   = "\033[1m";
const char* const DIM    = "\033[2m";
const char* const RED    = "\033[31m";
const char* const GREEN  = "\033[32m";
const char* const YELLOW = "\033[33m";
const char* const BLUE   = "\033[34m";
const char* const CYAN   = "\033[36m";
const char* const WHITE  = "\033[37m";
const char* const MAGENTA = "\033[35m";
const char* const BG_RED = "\033[41m";

std::string format_time(uint64_t ts_ms) {
    auto secs = static_cast<time_t>(ts_ms / 1000);
    auto ms = static_cast<int>(ts_ms % 1000);
    struct tm tm_buf{};
    localtime_r(&secs, &tm_buf);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms);
    return buf;
}

std::string truncate(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len - 3) + "...";
}

void print_separator() {
    std::cout << DIM;
    for (int i = 0; i < 100; ++i) std::cout << "\xe2\x94\x80";
    std::cout << RESET << "\n";
}

void print_objects(const std::vector<CorbaNamingEntry>& entries) {
    std::cout << "\n" << BOLD << CYAN << " DISCOVERED OBJECTS" << RESET << "\n";
    print_separator();

    if (entries.empty()) {
        std::cout << DIM << "  (none)" << RESET << "\n";
    } else {
        for (size_t i = 0; i < entries.size(); ++i) {
            auto& e = entries[i];
            std::cout << "  " << BOLD << WHITE << std::setw(2) << (i + 1) << RESET
                      << "  " << GREEN << std::left << std::setw(24) << e.path << RESET
                      << DIM << " " << e.host << ":" << e.port
                      << " GIOP/" << e.giop_version << RESET << "\n";
        }
    }
    print_separator();
    std::cout << "\n";
}

void print_message(const GiopMessage& m, bool show_hex) {
    bool is_request = (m.msg_type == GiopMessageType::Request);
    bool is_reply   = (m.msg_type == GiopMessageType::Reply);

    const char* arrow = is_request ? "\xe2\x86\x92" : "\xe2\x86\x90";  // → ←
    const char* type_color = is_request ? YELLOW : GREEN;
    if (is_reply && m.reply_status && *m.reply_status != ReplyStatus::NoException)
        type_color = RED;

    std::string time_str = format_time(m.timestamp_ms);
    std::string type_str = to_string(m.msg_type);
    std::string op = m.operation.value_or("");
    std::string obj = m.object_path.value_or("");

    std::cout << DIM << time_str << RESET
              << " " << type_color << BOLD << arrow << " " << std::left << std::setw(8) << type_str << RESET;

    if (!op.empty()) {
        std::cout << "  " << BOLD << WHITE << std::left << std::setw(22) << op << RESET;
    } else {
        std::cout << "  " << DIM << std::left << std::setw(22) << "-" << RESET;
    }

    if (!obj.empty()) {
        std::cout << CYAN << std::left << std::setw(18) << obj << RESET;
    }

    if (m.direction == MessageDirection::ClientToServer) {
        std::cout << DIM << " " << m.src_ip << ":" << m.src_port
                  << " > " << m.dst_ip << ":" << m.dst_port << RESET;
    } else if (m.direction == MessageDirection::ServerToClient) {
        std::cout << DIM << " " << m.src_ip << ":" << m.src_port
                  << " > " << m.dst_ip << ":" << m.dst_port << RESET;
    }

    if (m.is_oneway) std::cout << MAGENTA << " [oneway]" << RESET;

    if (is_reply && m.latency_ms) {
        double lat = *m.latency_ms;
        const char* lat_color = (lat < 10) ? GREEN : (lat < 100) ? YELLOW : RED;
        std::cout << " " << lat_color << std::fixed << std::setprecision(1) << lat << "ms" << RESET;
    }

    if (is_reply && m.reply_status) {
        auto s = *m.reply_status;
        if (s == ReplyStatus::NoException)
            std::cout << " " << GREEN << "OK" << RESET;
        else
            std::cout << " " << BG_RED << WHITE << BOLD << " " << to_string(s) << " " << RESET;
    }

    std::cout << DIM << " (" << m.size_bytes << "B)" << RESET;
    std::cout << "\n";

    if (m.params && !m.params->empty()) {
        for (auto& p : *m.params) {
            std::cout << "         " << BLUE << p.name << RESET
                      << DIM << " (" << p.type_name << ")" << RESET
                      << " = " << truncate(p.value, 120) << "\n";
        }
    }

    if (m.return_value) {
        auto& rv = *m.return_value;
        std::cout << "         " << GREEN << "return" << RESET
                  << DIM << " (" << rv.type_name << ")" << RESET
                  << " = " << truncate(rv.value, 120) << "\n";
    }

    if (m.out_params && !m.out_params->empty()) {
        for (auto& p : *m.out_params) {
            std::cout << "         " << MAGENTA << "out " << p.name << RESET
                      << DIM << " (" << p.type_name << ")" << RESET
                      << " = " << truncate(p.value, 120) << "\n";
        }
    }

    if (show_hex && m.params_hex) {
        std::cout << "         " << DIM << "hex: " << truncate(*m.params_hex, 80) << RESET << "\n";
    }
}

} // anonymous namespace

void run_cli(
    CliConfig cli_cfg,
    SharedObjects objects,
    SharedLookup /*lookup*/,
    SharedPortMap /*port_map*/,
    std::shared_ptr<Tracker> /*tracker*/,
    std::shared_ptr<Channel<GiopMessage>> msg_channel,
    std::shared_ptr<std::atomic<bool>> stop_flag)
{
    std::cout << BOLD << CYAN
              << "\n  ORBM — CLI Mode\n" << RESET;
    std::cout << DIM << "  Interface: " << cli_cfg.interface
              << "  |  NS: " << cli_cfg.ns_ref
              << "  |  Buffer: " << cli_cfg.buffer
              << "\n  Press Ctrl+C to exit\n" << RESET;

    std::vector<CorbaNamingEntry> obj_list;
    objects->read([&](const auto& v) { obj_list = v; });
    print_objects(obj_list);

    std::cout << BOLD << CYAN << " LIVE CAPTURE" << RESET << "\n";
    print_separator();

    uint64_t count = 0;
    while (!stop_flag->load(std::memory_order_relaxed)) {
        auto opt = msg_channel->recv();
        if (!opt) break;
        print_message(*opt, cli_cfg.show_hex);
        ++count;
    }

    std::cout << "\n" << DIM << "Captured " << count << " messages." << RESET << "\n";
}
