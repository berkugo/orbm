#include "web/server.h"
#include "net/discovery.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

std::string find_frontend_dir(const std::string& hint) {
    if (!hint.empty() && fs::is_directory(hint))
        return hint;

    // Typical dev layouts (run from cpp/ or cpp/build/)
    for (const auto& cand : {
            "src/web/frontend",
            "../src/web/frontend",
            "frontend"}) {
        if (fs::is_directory(cand)) return cand;
    }

    // Installed layout: bin/orbm next to share/orbm/frontend
    try {
        auto exe = fs::read_symlink("/proc/self/exe");
        auto dir = exe.parent_path();
        for (const auto& rel : {
                 "../share/orbm/frontend",
                 "../../src/web/frontend",
                 "../src/web/frontend"}) {
            auto p = dir / rel;
            if (fs::is_directory(p)) return p.string();
        }
    } catch (...) {}

    return "frontend";
}

std::string read_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::string content_type_for(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html")
        return "text/html; charset=utf-8";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js")
        return "application/javascript; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css")
        return "text/css; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg")
        return "image/svg+xml";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json")
        return "application/json";
    return "application/octet-stream";
}

} // anonymous namespace

crow::SimpleApp create_app(std::shared_ptr<AppState> state, const std::string& frontend_hint) {
    std::string frontend_dir = find_frontend_dir(frontend_hint);

    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([frontend_dir]() {
        std::string path = frontend_dir + "/index.html";
        std::string body = read_file(path);
        if (body.empty()) {
            crow::response res(404);
            res.body = "index.html not found";
            return res;
        }
        crow::response res(200);
        res.set_header("Content-Type", "text/html; charset=utf-8");
        res.body = std::move(body);
        return res;
    });

    CROW_ROUTE(app, "/vendor/<path>")
    ([frontend_dir](const std::string& path) {
        std::string file_path = frontend_dir + "/vendor/" + path;
        std::string body = read_file(file_path);
        if (body.empty()) {
            crow::response res(404);
            res.body = "Not found";
            return res;
        }
        crow::response res(200);
        res.set_header("Content-Type", content_type_for(file_path));
        res.body = std::move(body);
        return res;
    });

    CROW_ROUTE(app, "/api/objects")
    ([state]() {
        nlohmann::json j;
        state->objects->read([&](const auto& objs) { j = objs; });
        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.body = j.dump();
        return res;
    });

    CROW_ROUTE(app, "/api/messages")
    ([state](const crow::request& req) {
        size_t limit = 100;
        size_t offset = 0;
        if (auto v = req.url_params.get("limit"))
            limit = std::min(size_t(100), size_t(std::max(0, std::atoi(v))));
        if (auto v = req.url_params.get("offset"))
            offset = static_cast<size_t>(std::max(0, std::atoi(v)));
        (void)offset;

        nlohmann::json j = nlohmann::json::array();
        state->messages->read([&](const auto& msgs) {
            size_t start = msgs.size() > limit ? msgs.size() - limit : 0;
            for (size_t i = msgs.size(); i > start; --i)
                j.push_back(msgs[i - 1]);
        });

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.body = j.dump();
        return res;
    });

    CROW_ROUTE(app, "/api/refresh").methods(crow::HTTPMethod::POST)
    ([state](const crow::request& req) {
        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); } catch (...) {}

        DiscoveryConfig cfg;
        state->config->read([&](const auto& c) { cfg = c; });

        if (body.contains("ns_ref") && body["ns_ref"].is_string())
            cfg.ns_ref = body["ns_ref"].get<std::string>();

        auto entries = run_discovery(cfg);
        if (entries.empty()) {
            crow::response res(200);
            res.set_header("Content-Type", "application/json");
            res.body = R"({"ok":true,"count":0,"objects":[]})";
            return res;
        }

        size_t count = entries.size();
        auto lookup_map = build_lookup_map(entries);
        auto pmap = build_port_map(entries);

        state->objects->set(entries);
        state->lookup->set(std::move(lookup_map));
        state->port_map->set(std::move(pmap));

        WsEvent ev;
        ev.type = WsEvent::ObjectsUpdated;
        ev.objects = entries;
        state->ws_channel->send(std::move(ev));

        nlohmann::json j;
        j["ok"] = true;
        j["count"] = count;
        j["objects"] = entries;

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.body = j.dump();
        return res;
    });

    CROW_ROUTE(app, "/api/clear").methods(crow::HTTPMethod::POST)
    ([state]() {
        state->messages->write([](auto& msgs) { msgs.clear(); });
        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.body = R"({"ok":true})";
        return res;
    });

    CROW_ROUTE(app, "/api/config").methods(crow::HTTPMethod::GET)
    ([state]() {
        nlohmann::json j;
        state->config->read([&](const auto& c) { j["ns_ref"] = c.ns_ref; });
        state->interface_name->read([&](const auto& iface) { j["interface"] = iface; });

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.body = j.dump();
        return res;
    });

    CROW_ROUTE(app, "/api/config").methods(crow::HTTPMethod::POST)
    ([state](const crow::request& req) {
        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); } catch (...) {}

        if (body.contains("ns_ref") && body["ns_ref"].is_string()) {
            std::string ns_ref = body["ns_ref"].get<std::string>();
            state->config->write([&](auto& c) { c.ns_ref = ns_ref; });
        }
        if (body.contains("interface") && body["interface"].is_string()) {
            std::string iface = body["interface"].get<std::string>();
            state->interface_name->set(iface);
        }

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.body = R"({"ok":true})";
        return res;
    });

    CROW_WEBSOCKET_ROUTE(app, "/ws")
    .onopen([state](crow::websocket::connection& conn) {
        std::lock_guard<std::mutex> lk(state->ws_mutex);
        state->ws_connections.insert(&conn);
    })
    .onclose([state](crow::websocket::connection& conn, const std::string& /*reason*/) {
        std::lock_guard<std::mutex> lk(state->ws_mutex);
        state->ws_connections.erase(&conn);
    })
    .onmessage([](crow::websocket::connection& /*conn*/, const std::string& /*data*/, bool /*is_binary*/) {
    });

    return app;
}
