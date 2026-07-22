cmake_minimum_required(VERSION 3.24)

project(MODRA VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/FS)
endif()

set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    ftxui
    URL https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v6.1.9.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    cli11
    URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    sqlite_amalgamation
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(ftxui cli11 spdlog nlohmann_json sqlite_amalgamation)

add_library(modra_sqlite STATIC "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c")
target_include_directories(modra_sqlite SYSTEM PUBLIC "${sqlite_amalgamation_SOURCE_DIR}")
target_compile_definitions(modra_sqlite PRIVATE SQLITE_THREADSAFE=1)

configure_file(cmake/Version.h.in generated/modra/Version.h @ONLY)

include(cmake/GenerateMigrations.cmake)
set(MODRA_GENERATED_MIGRATIONS_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/modra/EmbeddedMigrations.h")
set(MODRA_GENERATED_MIGRATIONS_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/modra/EmbeddedMigrations.cpp")
modra_generate_embedded_migrations(
    "${CMAKE_CURRENT_SOURCE_DIR}/migrations"
    "${MODRA_GENERATED_MIGRATIONS_HEADER}"
    "${MODRA_GENERATED_MIGRATIONS_SOURCE}"
)

add_library(modra_core
    "${MODRA_GENERATED_MIGRATIONS_SOURCE}"
    src/application/AppInfo.cpp
    src/application/DashboardService.cpp
    src/application/NoteService.cpp
    src/application/ProjectService.cpp
    src/application/TaskService.cpp
    src/domain/Note.cpp
    src/domain/Project.cpp
    src/domain/Task.cpp
    src/infrastructure/config/DataDirectory.cpp
    src/infrastructure/database/Database.cpp
    src/infrastructure/database/NoteRepository.cpp
    src/infrastructure/database/ProjectRepository.cpp
    src/infrastructure/database/TaskRepository.cpp
    src/infrastructure/system/Environment.cpp
    src/infrastructure/system/ExternalEditor.cpp
)
target_include_directories(modra_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(modra_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
target_link_libraries(modra_core
    PUBLIC
        modra_sqlite
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

add_executable(modra
    src/main.cpp
    src/ui/App.cpp
    src/ui/DashboardScreen.cpp
    src/ui/KnowledgeScreen.cpp
    src/ui/ProjectScreen.cpp
    src/ui/TaskScreen.cpp
    src/ui/WorkScreen.cpp
)
target_link_libraries(modra
    PRIVATE
        modra_core
        CLI11::CLI11
        ftxui::component
        ftxui::dom
        ftxui::screen
)

function(modra_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

modra_enable_warnings(modra_core)
modra_enable_warnings(modra)

if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(modra_tests
        tests/DashboardTests.cpp
        tests/InfrastructureTests.cpp
        tests/GlobalTaskTests.cpp
        tests/NoteTests.cpp
        tests/ProjectTests.cpp
        tests/TaskTests.cpp
        tests/UiShortcutTests.cpp
    )
    add_executable(modra_editor_fixture tests/EditorFixture.cpp)
    modra_enable_warnings(modra_editor_fixture)
    add_dependencies(modra_tests modra_editor_fixture)
    target_compile_definitions(modra_tests PRIVATE MODRA_EDITOR_FIXTURE="$<TARGET_FILE:modra_editor_fixture>")
    target_link_libraries(modra_tests PRIVATE modra_core Catch2::Catch2WithMain ftxui::component)
    modra_enable_warnings(modra_tests)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(modra_tests)

    set(MODRA_STANDALONE_TEST_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/standalone-runtime")
    file(MAKE_DIRECTORY "${MODRA_STANDALONE_TEST_DIRECTORY}")
    add_test(NAME "MODRA executable runs without external migrations"
             COMMAND modra doctor)
    set_tests_properties("MODRA executable runs without external migrations" PROPERTIES
        WORKING_DIRECTORY "${MODRA_STANDALONE_TEST_DIRECTORY}"
        ENVIRONMENT "LOCALAPPDATA=${MODRA_STANDALONE_TEST_DIRECTORY}/data"
    )
endif()
