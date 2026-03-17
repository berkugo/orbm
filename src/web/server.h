// HTTP / WebSocket server wiring for ORBM.
//
// `AppState` aggregates all shared runtime data that the Crow handlers and
// WebSocket broadcaster need in order to:
// - expose `/api/messages`, `/api/objects`, `/api/config` REST endpoints
// - serve the static React frontend from `src/web/frontend`
// - push live `GiopMessage` updates over `/ws`.
//
#pragma once

#include "core/types.h"
#include "crow.h"
#include <set>
#include <mutex>
#include <memory>
#include <string>

struct AppState {
    SharedObjects objects;
    SharedLookup lookup;
    SharedPortMap port_map;
    std::shared_ptr<SharedData<std::vector<GiopMessage>>> messages;
    std::shared_ptr<Channel<WsEvent>> ws_channel;
    std::shared_ptr<SharedData<DiscoveryConfig>> config;
    size_t max_messages = 100;
    std::shared_ptr<SharedData<std::string>> interface_name;
    std::mutex ws_mutex;
    std::set<crow::websocket::connection*> ws_connections;
};

// Creates and configures a Crow application bound to the given `AppState`.
// `frontend_dir` can be used to override the auto-detected location of the
// static web assets (primarily for tests and custom packaging).
crow::SimpleApp create_app(std::shared_ptr<AppState> state, const std::string& frontend_dir);
