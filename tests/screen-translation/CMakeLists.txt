cmake_minimum_required(VERSION 3.20)
project(screen_translation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Svg Widgets Network Test)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(screen_translation screen_translation.cpp ${ROOT}/resources/resources.qrc
    ${ROOT}/src/ui/translation/screen_translation_window.cpp
    ${ROOT}/src/ui/toolbar/toolbar_icons.cpp
    ${ROOT}/src/core/ocr/ocr_manager.cpp
    ${ROOT}/src/core/ocr/rapid_ocr_engine.cpp
    ${ROOT}/src/core/ocr/windows_media_ocr.cpp
    ${ROOT}/src/core/translation/translation_plugin_manager.cpp
    ${ROOT}/src/core/translation/offline_plugin_translator.cpp
    ${ROOT}/src/core/translation/online_fallback_translator.cpp
    ${ROOT}/src/core/ai/ai_service.cpp
    ${ROOT}/src/core/plugin/plugin_manager.cpp
    ${ROOT}/src/core/theme_manager.cpp)
target_include_directories(screen_translation PRIVATE ${ROOT}/src)
target_compile_options(screen_translation PRIVATE /utf-8 /permissive- /W4)
target_link_libraries(screen_translation PRIVATE Qt6::Core Qt6::Gui Qt6::Svg Qt6::Widgets Qt6::Network Qt6::Test windowsapp runtimeobject advapi32 user32 dwmapi)
enable_testing()
add_test(NAME screen_translation COMMAND screen_translation)
set_tests_properties(screen_translation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
