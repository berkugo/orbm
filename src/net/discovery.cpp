#include "net/discovery.h"
#include <cstdio>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>

namespace {

inline bool str_ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool str_starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline std::string str_trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string shell_escape(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    result += "'";
    return result;
}

std::string run_command(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[4096];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

std::string build_path(const std::vector<std::string>& context_stack, const std::string& name) {
    if (context_stack.empty()) return name;
    std::string path;
    for (const auto& ctx : context_stack) {
        if (!path.empty()) path += "/";
        path += ctx;
    }
    path += "/" + name;
    return path;
}

std::string run_catior_cmd(const std::string& bin, const std::string& ior,
                           const std::string& ld_library_path) {
    std::string cmd;
    if (!ld_library_path.empty()) {
        cmd = "LD_LIBRARY_PATH=" + shell_escape(ld_library_path) + " ";
    }
    cmd += shell_escape(bin) + " -i " + shell_escape(ior) + " 2>&1";
    auto output = run_command(cmd);
    if (!output.empty()) return output;

    cmd.clear();
    if (!ld_library_path.empty()) {
        cmd = "LD_LIBRARY_PATH=" + shell_escape(ld_library_path) + " ";
    }
    cmd += shell_escape(bin) + " " + shell_escape(ior) + " 2>&1";
    return run_command(cmd);
}

} // anonymous namespace

std::vector<CorbaNamingEntry> run_discovery(const DiscoveryConfig& config) {
    std::string cmd;
    if (!config.ld_library_path.empty()) {
        cmd = "LD_LIBRARY_PATH=" + shell_escape(config.ld_library_path) + " ";
    }
    cmd += shell_escape(config.nslist_bin);
    cmd += " -ORBInitRef " + shell_escape("NameService=" + config.ns_ref);
    for (const auto& arg : config.orb_args) {
        cmd += " " + shell_escape(arg);
    }
    cmd += " 2>&1";

    auto output = run_command(cmd);
    return parse_nslist_output(output);
}

std::vector<CorbaNamingEntry> parse_nslist_output(const std::string& output) {
    std::vector<CorbaNamingEntry> result;
    std::vector<std::string> context_stack;

    std::string current_name;
    std::string current_host;
    uint16_t current_port = 0;
    bool has_name = false;
    bool has_endpoint = false;

    auto flush = [&]() {
        if (has_name && has_endpoint) {
            CorbaNamingEntry entry;
            entry.path = build_path(context_stack, current_name);
            entry.host = std::move(current_host);
            entry.port = current_port;
            entry.giop_version = "1.2";
            result.push_back(std::move(entry));
        }
        has_name = false;
        has_endpoint = false;
        current_name.clear();
        current_host.clear();
        current_port = 0;
    };

    const std::string nc_suffix_new = ": Naming context";
    const std::string nc_suffix_old = "(NamingContext)";

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        size_t pipe_count = 0;
        size_t pos = 0;
        while (pos < line.size()) {
            if (line[pos] == '|') { pipe_count++; pos++; }
            else if (line[pos] == ' ' || line[pos] == '\t') { pos++; }
            else break;
        }

        std::string content = str_trim(line.substr(pos));
        if (content.empty()) continue;

        if (content.size() > 2 && content[0] == '+' && content[1] == ' ') {
            std::string rest = content.substr(2);

            bool is_context = false;
            std::string entry_name;

            if (str_ends_with(rest, nc_suffix_new)) {
                is_context = true;
                entry_name = str_trim(rest.substr(0, rest.size() - nc_suffix_new.size()));
            } else if (str_ends_with(rest, nc_suffix_old)) {
                is_context = true;
                auto name_part = rest.substr(0, rest.size() - nc_suffix_old.size());
                auto ce = name_part.find_last_not_of(" \t/");
                entry_name = (ce != std::string::npos) ? name_part.substr(0, ce + 1) : "";
            }

            if (is_context) {
                flush();
                while (context_stack.size() > pipe_count) context_stack.pop_back();
                if (!entry_name.empty()) context_stack.push_back(entry_name);
                continue;
            }

            flush();
            while (context_stack.size() > pipe_count) context_stack.pop_back();

            auto colon = rest.find(':');
            std::string obj_name = (colon != std::string::npos)
                ? rest.substr(0, colon) : rest;
            obj_name = str_trim(obj_name);
            if (!obj_name.empty()) {
                current_name = std::move(obj_name);
                has_name = true;
            }
            continue;
        }

        if (str_starts_with(content, "Endpoint:")) {
            auto ep = str_trim(content.substr(9));
            auto colon_pos = ep.rfind(':');
            if (colon_pos != std::string::npos) {
                auto host = str_trim(ep.substr(0, colon_pos));
                auto port_str = str_trim(ep.substr(colon_pos + 1));
                try {
                    unsigned long pval = std::stoul(port_str);
                    if (pval <= 65535) {
                        current_host = std::move(host);
                        current_port = static_cast<uint16_t>(pval);
                        has_endpoint = true;
                    }
                } catch (...) {}
            }
            continue;
        }
    }

    flush();
    return result;
}

std::unordered_map<std::string, CorbaNamingEntry> build_lookup_map(
    const std::vector<CorbaNamingEntry>& entries)
{
    std::unordered_map<std::string, CorbaNamingEntry> map;
    for (const auto& e : entries) {
        if (!e.object_key_hex.empty()) {
            map[e.object_key_hex] = e;
        }
    }
    return map;
}

std::unordered_map<uint16_t, std::vector<CorbaNamingEntry>> build_port_map(
    const std::vector<CorbaNamingEntry>& entries)
{
    std::unordered_map<uint16_t, std::vector<CorbaNamingEntry>> map;
    for (const auto& e : entries) {
        map[e.port].push_back(e);
    }
    return map;
}

std::optional<std::tuple<std::string, uint16_t, std::string, std::string, std::string>>
parse_catior_output(const std::string& output) {
    static const std::regex ip_re(R"(The IP address is:\s*(.+))");
    static const std::regex port_re(R"(The port is:\s*(\d+))");
    static const std::regex key_re(R"(The object key \(as hex\):\s*([0-9a-fA-F ]+))");
    static const std::regex type_re(R"(The Type ID is:\s*(.+))");
    static const std::regex giop_re(R"(IIOP Version is:\s*(\d+\.\d+))");

    std::smatch m;

    std::string host = "0.0.0.0";
    if (std::regex_search(output, m, ip_re) && m.size() > 1)
        host = str_trim(m[1].str());

    uint16_t port = 0;
    if (std::regex_search(output, m, port_re) && m.size() > 1) {
        try { port = static_cast<uint16_t>(std::stoul(str_trim(m[1].str()))); }
        catch (...) {}
    }

    std::string key_hex;
    if (std::regex_search(output, m, key_re) && m.size() > 1) {
        key_hex = m[1].str();
        key_hex.erase(std::remove(key_hex.begin(), key_hex.end(), ' '), key_hex.end());
        std::transform(key_hex.begin(), key_hex.end(), key_hex.begin(),
                       [](unsigned char c){ return std::tolower(c); });
    }

    if (key_hex.empty()) return std::nullopt;

    std::string type_id;
    if (std::regex_search(output, m, type_re) && m.size() > 1)
        type_id = str_trim(m[1].str());

    std::string giop_version = "1.2";
    if (std::regex_search(output, m, giop_re) && m.size() > 1)
        giop_version = str_trim(m[1].str());

    return std::make_tuple(std::move(host), port, std::move(key_hex),
                           std::move(type_id), std::move(giop_version));
}
