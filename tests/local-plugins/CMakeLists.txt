cmake_minimum_required(VERSION 3.20)
project(local_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(local_probe main.cpp
    ${ROOT}/src/core/ocr/rapid_ocr_engine.cpp ${ROOT}/src/core/ocr/windows_media_ocr.cpp
    ${ROOT}/src/core/translation/offline_plugin_translator.cpp
    ${ROOT}/src/core/formula/formula_ocr_manager.cpp ${ROOT}/src/core/ai/ai_service.cpp)
target_include_directories(local_probe PRIVATE ${ROOT}/src)
target_compile_options(local_probe PRIVATE /utf-8)
target_link_libraries(local_probe PRIVATE Qt6::Core Qt6::Gui Qt6::Network windowsapp runtimeobject advapi32)
