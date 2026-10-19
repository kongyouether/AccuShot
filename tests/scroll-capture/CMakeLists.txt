cmake_minimum_required(VERSION 3.20)
project(accushot_scroll_capture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
get_filename_component(ACCUSHOT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(scroll_capture_test scroll_capture_test.cpp "${ACCUSHOT_ROOT}/resources/resources.qrc"
    "${ACCUSHOT_ROOT}/src/core/capture/scroll_stitcher.cpp"
    "${ACCUSHOT_ROOT}/src/ui/capture/scroll_capture_widget.cpp"
    "${ACCUSHOT_ROOT}/src/core/theme_manager.cpp"
    "${ACCUSHOT_ROOT}/src/core/export/image_exporter.cpp")
target_include_directories(scroll_capture_test PRIVATE "${ACCUSHOT_ROOT}/src")
target_link_libraries(scroll_capture_test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
if(WIN32)
    target_link_libraries(scroll_capture_test PRIVATE user32 advapi32 dwmapi)
endif()
if(MSVC)
    target_compile_options(scroll_capture_test PRIVATE /utf-8 /permissive- /W4)
endif()
enable_testing()
add_test(NAME scroll_capture_test COMMAND scroll_capture_test)
set_tests_properties(scroll_capture_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
