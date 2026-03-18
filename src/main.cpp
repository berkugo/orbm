#include "core/types.h"
#include "core/tracker.h"
#include "net/capture.h"
#include "net/discovery.h"
#include "idl/idl_parser.h"
#include "web/server.h"
#include "cli/cli.h"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <cstdlib>
#include <filesystem>
#include <csignal>

namespace fs = std::filesystem;

static std::shared_ptr<std::atomic<bool>> g_stop;

static void signal_handler(int) {
    if (g_stop) g_stop->store(true, std::memory_order_relaxed);
}

struct Args {
    std::string ns_ref = "corbaloc:iiop:localhost:2809/NameService";
    std::string interface = "any";
    std::string host = "0.0.0.0";
    uint16_t ws_port = 3000;
    uint64_t refresh_interval = 30;
    size_t buffer = 100;
    std::vector<std::string> idl_paths;
    std::vector<std::string> orb_args;
    bool cli_mode = false;
    bool show_hex = false;
};

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\n"
              << "Modes:\n"
              << "  (default)                   Web UI mode (HTTP + WebSocket)\n"
              << "  --cli                       CLI mode (terminal output)\n"
              << "\n"
              << "Options:\n"
              << "  --ns-ref <ref>              Naming Service reference\n"
              << "  --interface, -i <iface>     Capture interface (default: any)\n"
              << "  --host <addr>               Listen address (default: 0.0.0.0)\n"
              << "  --ws-port, -p <port>        HTTP/WebSocket port (default: 3000)\n"
              << "  --refresh-interval <secs>   NS refresh interval (default: 30)\n"
              << "  --buffer <count>            Max stored messages (default: 100)\n"
              << "  --idl <path>                IDL file or directory (repeatable)\n"
              << "  --hex                       Show params hex in CLI mode\n"
              << "  -- <orb_args...>            Extra ORB arguments\n";
}

static Args parse_args(int argc, char* argv[]) {
    Args args;
    bool past_separator = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (past_separator) {
            args.orb_args.push_back(arg);
            continue;
        }

        if (arg == "--") {
            past_separator = true;
            continue;
        }

        auto next_val = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Missing value for " << arg << "\n";
            std::exit(1);
        };

        if (arg == "--ns-ref") { args.ns_ref = next_val(); }
        else if (arg == "--interface" || arg == "-i") { args.interface = next_val(); }
        else if (arg == "--host") { args.host = next_val(); }
        else if (arg == "--ws-port" || arg == "-p") { args.ws_port = static_cast<uint16_t>(std::stoi(next_val())); }
        else if (arg == "--refresh-interval") { args.refresh_interval = std::stoull(next_val()); }
        else if (arg == "--buffer") { args.buffer = std::stoull(next_val()); }
        else if (arg == "--idl") { args.idl_paths.push_back(next_val()); }
        else if (arg == "--cli") { args.cli_mode = true; }
        else if (arg == "--hex") { args.show_hex = true; }
        else if (arg == "--help" || arg == "-h") { print_usage(argv[0]); std::exit(0); }
        else { std::cerr << "Unknown option: " << arg << "\n"; print_usage(argv[0]); std::exit(1); }
    }
    return args;
}

static std::shared_ptr<IdlRegistry> load_idl(const std::vector<std::string>& user_paths) {
    auto registry = std::make_shared<IdlRegistry>();
    size_t files_loaded = 0;

    std::vector<std::string> paths = user_paths;
    if (paths.empty()) {
        for (const auto& candidate : {"cpp_test/idl", "idl"}) {
            if (fs::is_directory(candidate)) { paths.push_back(candidate); break; }
        }
        if (paths.empty()) {
            try {
                auto exe = fs::read_symlink("/proc/self/exe");
                auto dir = exe.parent_path();
                for (const auto& rel : {"../../cpp_test/idl", "../../idl", "../cpp_test/idl"}) {
                    auto p = dir / rel;
                    if (fs::is_directory(p)) { paths.push_back(p.string()); break; }
                }
            } catch (...) {}
        }
    }

    for (const auto& idl_path : paths) {
        fs::path p(idl_path);
        if (fs::is_directory(p))
            files_loaded += registry->parse_dir_recursive(p.string());
        else if (fs::is_regular_file(p)) {
            if (registry->parse_file(p.string())) files_loaded++;
        } else {
            std::cerr << "[warn] IDL path not found: " << idl_path << "\n";
        }
    }

    size_t ops = registry->ops.size();
    size_t structs = registry->structs.size();
    if (!paths.empty()) {
        std::cout << "[info] IDL: " << files_loaded << " file(s) loaded, "
                  << ops << " operations, " << structs << " structs\n";
        if (files_loaded > 0 && ops == 0)
            std::cerr << "[warn] No operations parsed from IDL files\n";
    } else {
        std::cerr << "[warn] No IDL path given; request params will not be decoded\n";
    }
    return registry;
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);

    std::string nslist_bin, catior_bin, ld_library_path;
    if (const char* ace = std::getenv("ACE_ROOT")) {
        std::cout << "[info] ACE_ROOT=" << ace << std::endl;
        nslist_bin = std::string(ace) + "/bin/tao_nslist";
        catior_bin = std::string(ace) + "/bin/tao_catior";
        ld_library_path = std::string(ace) + "/lib";
    } else {
        std::cerr << "[warn] ACE_ROOT not set, falling back to PATH lookup\n";
        nslist_bin = "tao_nslist";
        catior_bin = "tao_catior";
    }
    std::cout << "[info] nslist=" << nslist_bin << ", catior=" << catior_bin << std::endl;

    auto idl_registry = load_idl(args.idl_paths);

    DiscoveryConfig disc_config;
    disc_config.ns_ref = args.ns_ref;
    disc_config.nslist_bin = nslist_bin;
    disc_config.catior_bin = catior_bin;
    disc_config.ld_library_path = ld_library_path;
    disc_config.orb_args = args.orb_args;

    auto objects = std::make_shared<SharedData<std::vector<CorbaNamingEntry>>>();
    auto lookup = std::make_shared<SharedData<std::unordered_map<std::string, CorbaNamingEntry>>>();
    auto port_map = std::make_shared<SharedData<std::unordered_map<uint16_t, std::vector<CorbaNamingEntry>>>>();

    auto entries = run_discovery(disc_config);
    if (!entries.empty()) {
        std::cout << "[info] Discovered " << entries.size() << " objects from Naming Service\n";
        objects->set(entries);
        lookup->set(build_lookup_map(entries));
        port_map->set(build_port_map(entries));
    } else {
        std::cerr << "[warn] Initial NS discovery returned no entries\n";
    }

    auto stop_flag = std::make_shared<std::atomic<bool>>(false);
    g_stop = stop_flag;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string filter = build_bpf_filter({});
    auto msg_channel = std::make_shared<Channel<GiopMessage>>();
    auto message_id = std::make_shared<std::atomic<uint64_t>>(0);
    auto tracker = std::make_shared<Tracker>();

    std::thread capture_thread([iface = args.interface, filter, lookup, port_map,
                                tracker, msg_channel, message_id, stop_flag, idl_registry]() {
        run_capture_blocking(iface, filter, lookup, port_map, tracker,
                             msg_channel, message_id, stop_flag, idl_registry);
    });

    if (args.cli_mode
#ifdef ORBM_NO_UI
        || true  // CLI-only build: always use CLI path
#endif
    ) {
        CliConfig cli_cfg;
        cli_cfg.interface = args.interface;
        cli_cfg.ns_ref = args.ns_ref;
        cli_cfg.buffer = args.buffer;
        cli_cfg.show_hex = args.show_hex;

        run_cli(cli_cfg, objects, lookup, port_map, tracker, msg_channel, stop_flag);

        stop_flag->store(true, std::memory_order_relaxed);
        msg_channel->close();
        if (capture_thread.joinable()) capture_thread.join();
        return 0;
    }

#ifndef ORBM_NO_UI
    // --- Web UI mode ---

    auto ws_channel = std::make_shared<Channel<WsEvent>>();
    auto config = std::make_shared<SharedData<DiscoveryConfig>>(disc_config);
    auto interface_name = std::make_shared<SharedData<std::string>>(args.interface);
    auto messages = std::make_shared<SharedData<std::vector<GiopMessage>>>();
    size_t max_messages = args.buffer;

    std::thread dispatcher_thread([msg_channel, messages, ws_channel, max_messages]() {
        while (auto opt = msg_channel->recv()) {
            auto msg = std::move(*opt);
            messages->write([&](auto& v) {
                v.push_back(msg);
                if (v.size() > max_messages)
                    v.erase(v.begin(), v.begin() + static_cast<ptrdiff_t>(v.size() - max_messages));
            });
            WsEvent ev;
            ev.type = WsEvent::GiopMsg;
            ev.giop_msg = std::move(msg);
            ws_channel->send(std::move(ev));
        }
    });

    std::thread timeout_thread([tracker, ws_channel, stop_flag]() {
        while (!stop_flag->load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            auto timed_out = tracker->check_timeouts();
            for (uint64_t id : timed_out) {
                WsEvent ev;
                ev.type = WsEvent::RequestTimeout;
                ev.timeout_id = id;
                ws_channel->send(std::move(ev));
            }
        }
    });

    std::thread refresh_thread([config, objects, lookup, port_map,
                                ws_channel, refresh_secs = args.refresh_interval, stop_flag]() {
        std::this_thread::sleep_for(std::chrono::seconds(refresh_secs));
        while (!stop_flag->load(std::memory_order_relaxed)) {
            DiscoveryConfig cfg;
            config->read([&](const auto& c) { cfg = c; });
            auto ents = run_discovery(cfg);
            if (!ents.empty()) {
                objects->set(ents);
                lookup->set(build_lookup_map(ents));
                port_map->set(build_port_map(ents));
                WsEvent ev;
                ev.type = WsEvent::ObjectsUpdated;
                ev.objects = std::move(ents);
                ws_channel->send(std::move(ev));
            }
            std::this_thread::sleep_for(std::chrono::seconds(refresh_secs));
        }
    });

    auto app_state = std::make_shared<AppState>();
    app_state->objects = objects;
    app_state->lookup = lookup;
    app_state->port_map = port_map;
    app_state->messages = messages;
    app_state->ws_channel = ws_channel;
    app_state->config = config;
    app_state->max_messages = max_messages;
    app_state->interface_name = interface_name;

    std::thread ws_broadcast_thread([app_state]() {
        while (auto opt = app_state->ws_channel->recv()) {
            auto json_str = ws_event_to_json(*opt).dump();
            std::lock_guard<std::mutex> lk(app_state->ws_mutex);
            for (auto* conn : app_state->ws_connections) {
                try { conn->send_text(json_str); } catch (...) {}
            }
        }
    });

    auto app = create_app(app_state, "");
    std::cout << "[info] ORBM listening on http://" << args.host << ":" << args.ws_port << std::endl;
    app.bindaddr(args.host).port(args.ws_port).multithreaded().run();

    stop_flag->store(true, std::memory_order_relaxed);
    msg_channel->close();
    ws_channel->close();
    if (capture_thread.joinable()) capture_thread.join();
    if (dispatcher_thread.joinable()) dispatcher_thread.join();
    if (timeout_thread.joinable()) timeout_thread.join();
    if (refresh_thread.joinable()) refresh_thread.join();
    if (ws_broadcast_thread.joinable()) ws_broadcast_thread.join();

    return 0;
#else
    // Should not be reachable because CLI path is forced above when ORBM_NO_UI is set.
    std::cerr << "[error] Web UI is disabled in this build.\n";
    return 1;
#endif
}
