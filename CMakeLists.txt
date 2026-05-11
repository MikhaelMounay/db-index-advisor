cmake_minimum_required(VERSION 3.16)
project(IndexAdvisor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# All compiled binaries land in build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Core library
# The server and test runner link against this target.
add_library(advisor_core STATIC
    src/core/cost_model.cpp
    src/core/greedy.cpp
    src/core/dp.cpp
)
target_include_directories(advisor_core PUBLIC src/core)
target_compile_options(advisor_core PRIVATE -Wall -Wextra)

# Test runner binary
# Links only against the core library
add_executable(tests src/tests/test_cases.cpp)
target_link_libraries(tests PRIVATE advisor_core)
target_compile_options(tests PRIVATE -Wall -Wextra)

# Server binary
# Links against the core library as well as external dependencies
include(FetchContent)

FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(server
    src/server/main.cpp
    src/server/handlers.cpp)
target_link_libraries(server PRIVATE advisor_core httplib nlohmann_json::nlohmann_json)
target_compile_options(server PRIVATE -Wall -Wextra)
