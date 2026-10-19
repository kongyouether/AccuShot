cmake_minimum_required(VERSION 3.20)
project(accushot_translation_timeout LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network)
get_filename_component(ACCUSHOT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(TRANSLATION_DIR "${ACCUSHOT_ROOT}/src/core/translation")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TRANSLATION_DIR}/online_fallback_translator.cpp")
file(READ "${TRANSLATION_DIR}/online_fallback_translator.cpp" TRANSLATOR_SOURCE)
# Redirect only the two endpoints; exercise the production worker and its real timers.
foreach(ENDPOINT IN ITEMS primary fallback)
    if(ENDPOINT STREQUAL "primary")
        set(URL_VARIABLE "url")
        set(ORIGINAL_URL "QUrl url(\"https://m.youdao.com/translate\");")
    else()
        set(URL_VARIABLE "gtxUrl")
        set(ORIGINAL_URL "QUrl gtxUrl(\"https://translate.googleapis.com/translate_a/single\");")
    endif()
    string(REPLACE "${ORIGINAL_URL}"
        "QUrl ${URL_VARIABLE}(QStringLiteral(\"http://127.0.0.1:%1/${ENDPOINT}\").arg(qEnvironmentVariable(\"ACCUSHOT_TEST_HTTP_PORT\")));"
        TRANSLATOR_SOURCE "${TRANSLATOR_SOURCE}")
endforeach()
option(OLD_TIMEOUT_BUG "Remove primary timer cancellation to detect the stale timeout in this test process" OFF)
if(OLD_TIMEOUT_BUG)
    string(REPLACE "timeoutTimer.stop();" "/* Original uncancelled timeout. */" TRANSLATOR_SOURCE "${TRANSLATOR_SOURCE}")
endif()
# Observe the timeout lifetime explicitly: freed storage is not guaranteed to crash.
string(REPLACE "QTimer timeoutTimer;" "bool primaryFinished = false;\n    QTimer timeoutTimer;"
    TRANSLATOR_SOURCE "${TRANSLATOR_SOURCE}")
string(REPLACE "if (reply->isRunning()) {"
    "if (primaryFinished) qFatal(\"Expired primary timeout fired during fallback\");\n        if (reply->isRunning()) {"
    TRANSLATOR_SOURCE "${TRANSLATOR_SOURCE}")
string(REPLACE "loop.exec();" "loop.exec();\n    primaryFinished = true;"
    TRANSLATOR_SOURCE "${TRANSLATOR_SOURCE}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/online_fallback_translator.cpp" "${TRANSLATOR_SOURCE}")
add_executable(translation_timeout ../translation_timeout.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/online_fallback_translator.cpp"
    "${TRANSLATION_DIR}/online_fallback_translator.h")
target_include_directories(translation_timeout PRIVATE "${TRANSLATION_DIR}")
target_link_libraries(translation_timeout PRIVATE Qt6::Core Qt6::Network)
if(MSVC)
    target_compile_options(translation_timeout PRIVATE /utf-8 /permissive- /W4)
endif()
enable_testing()
add_test(NAME translation_timeout COMMAND translation_timeout)
set_tests_properties(translation_timeout PROPERTIES TIMEOUT 20)
