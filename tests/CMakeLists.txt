cmake_minimum_required(VERSION 3.20)
project(accushot_toolbar_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Svg Widgets Network Test)

add_executable(toolbar_smoke
    toolbar_smoke.cpp
    ../resources/resources.qrc
    ../src/core/toolbar_config.cpp
    ../src/core/hotkey_config.cpp
    ../src/core/theme_manager.cpp
    ../src/ui/toolbar/floating_toolbar.cpp
    ../src/ui/toolbar/toolbar_icons.cpp
    ../src/ui/settings/toolbar_settings_dialog.cpp
)
target_include_directories(toolbar_smoke PRIVATE ../src)
target_link_libraries(toolbar_smoke PRIVATE Qt6::Core Qt6::Gui Qt6::Svg Qt6::Widgets Qt6::Test)
if(WIN32)
    target_link_libraries(toolbar_smoke PRIVATE advapi32 dwmapi)
endif()

# Compile the changed integration sources without linking the WinRT OCR backend.
add_library(toolbar_integration_compile OBJECT
    ../src/ui/overlay/snipping_overlay.cpp
    ../src/ui/ocr/ocr_result_dialog.cpp
    ../src/ui/settings/hotkey_settings_dialog.cpp
)
set_target_properties(toolbar_integration_compile PROPERTIES AUTOMOC OFF)
target_include_directories(toolbar_integration_compile PRIVATE ../src)
target_link_libraries(toolbar_integration_compile PRIVATE Qt6::Core Qt6::Gui Qt6::Svg Qt6::Widgets Qt6::Network)
if(MSVC)
    target_compile_options(toolbar_integration_compile PRIVATE /utf-8)
endif()
if(MSVC)
    target_compile_options(toolbar_smoke PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME toolbar_smoke COMMAND toolbar_smoke --output ${CMAKE_BINARY_DIR}/screenshots)
set_tests_properties(toolbar_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
