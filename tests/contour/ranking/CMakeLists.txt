cmake_minimum_required(VERSION 3.20)
project(ranking_replay LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
get_filename_component(ROOT "../../.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ROOT}/src/core/smart_snapper.cpp" "${ROOT}/src/core/smart_snapper.h")
file(SHA256 "${ROOT}/src/core/smart_snapper.cpp" RANK_CPP_SHA)
file(SHA256 "${ROOT}/src/core/smart_snapper.h" RANK_H_SHA)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${ROOT}"
    OUTPUT_VARIABLE SOURCE_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE)
add_executable(ranking_replay ../ranking_replay.cpp "${ROOT}/src/core/smart_snapper.cpp"
    "${ROOT}/src/core/native_snapper.cpp")
target_include_directories(ranking_replay PRIVATE "${ROOT}/src/core")
target_compile_definitions(ranking_replay PRIVATE REPLAY_ROOT="${ROOT}"
    RANK_CPP_SHA="${RANK_CPP_SHA}" RANK_H_SHA="${RANK_H_SHA}" SOURCE_HEAD="${SOURCE_HEAD}")
target_link_libraries(ranking_replay PRIVATE Qt6::Core Qt6::Gui user32 dwmapi ole32 oleaut32 uiautomationcore)
target_compile_options(ranking_replay PRIVATE /utf-8 /O2)
