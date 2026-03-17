cmake_minimum_required(VERSION 3.14)
project(orbm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# ── System deps ──
find_library(PCAP_LIB pcap REQUIRED)

add_executable(orbm
    src/main.cpp
    src/protocol/giop.cpp
    src/protocol/cdr_decode.cpp
    src/idl/idl_parser.cpp
    src/net/discovery.cpp
    src/net/capture.cpp
    src/web/server.cpp
    src/cli/cli.cpp
)

target_include_directories(orbm PRIVATE src)
target_link_libraries(orbm PRIVATE
    Crow::Crow
    nlohmann_json::nlohmann_json
    ${PCAP_LIB}
    pthread
)

install(TARGETS orbm RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/web/frontend DESTINATION share/orbm)
