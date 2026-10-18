cmake_minimum_required(VERSION 3.20)
project(refinement_replay LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
get_filename_component(ROOT "../../.." ABSOLUTE)
set(SNAP_CPP_PATH "${ROOT}/src/core/smart_snapper.cpp" CACHE FILEPATH "Production core implementation; frozen v2 permitted for paired holdout")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SNAP_CPP_PATH}" "${ROOT}/src/core/smart_snapper.h")
file(SHA256 "${SNAP_CPP_PATH}" SNAP_CPP_SHA)
file(SHA256 "${ROOT}/src/core/smart_snapper.h" SNAP_H_SHA)
add_executable(refinement_replay ../refinement_replay.cpp "${SNAP_CPP_PATH}"
    "${ROOT}/src/core/native_snapper.cpp")
target_include_directories(refinement_replay PRIVATE "${ROOT}/src/core")
target_compile_definitions(refinement_replay PRIVATE REPLAY_ROOT="${ROOT}"
    SNAP_CPP_SHA="${SNAP_CPP_SHA}" SNAP_H_SHA="${SNAP_H_SHA}" SNAP_CPP_PATH="${SNAP_CPP_PATH}")
target_link_libraries(refinement_replay PRIVATE Qt6::Core Qt6::Gui user32 dwmapi ole32 oleaut32 uiautomationcore)
target_compile_options(refinement_replay PRIVATE /utf-8 /O2)
