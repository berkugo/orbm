cmake_minimum_required(VERSION 3.14)
project(orbm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Option to build CLI-only binary without pulling web UI deps
option(ORBM_NO_UI "Build ORBM without web UI (CLI only, no external header-only deps)" OFF)

if (NOT ORBM_NO_UI)
    include(FetchContent)

    # ── Standalone Asio (required by Crow) ──
    FetchContent_Declare(asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG        asio-1-28-0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(asio)
    set(ASIO_INCLUDE_DIR ${asio_SOURCE_DIR}/asio/include CACHE PATH "" FORCE)

    # ── Crow HTTP/WebSocket framework ──
    FetchContent_Declare(Crow
        GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
        GIT_TAG        v1.2.0
        GIT_SHALLOW    TRUE
    )
    set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Crow)

    # ── nlohmann/json ──
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(json)
endif()

# ── System deps ──
find_library(PCAP_LIB pcap REQUIRED)

add_executable(orbm
    src/main.cpp
    src/protocol/giop.cpp
    src/protocol/cdr_decode.cpp
    src/idl/idl_parser.cpp
    src/net/discovery.cpp
    src/net/capture.cpp
    src/cli/cli.cpp
)

if (ORBM_NO_UI)
    target_compile_definitions(orbm PRIVATE ORBM_NO_UI=1)
else()
    target_sources(orbm PRIVATE src/web/server.cpp)
endif()

target_include_directories(orbm PRIVATE src)

if (ORBM_NO_UI)
    target_link_libraries(orbm PRIVATE
        ${PCAP_LIB}
        pthread
    )
else()
    target_link_libraries(orbm PRIVATE
        Crow::Crow
        nlohmann_json::nlohmann_json
        ${PCAP_LIB}
        pthread
    )
endif()

install(TARGETS orbm RUNTIME DESTINATION bin)
if (NOT ORBM_NO_UI)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/web/frontend DESTINATION share/orbm)
endif()
