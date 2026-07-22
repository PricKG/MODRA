function(modra_generate_embedded_migrations migration_directory generated_header generated_source)
    file(GLOB migration_files CONFIGURE_DEPENDS "${migration_directory}/*.sql")
    if(NOT migration_files)
        message(FATAL_ERROR "MODRA requires at least one migration in ${migration_directory}")
    endif()

    set(versioned_migrations)
    set(seen_versions)
    set(seen_names)
    foreach(migration_file IN LISTS migration_files)
        get_filename_component(migration_filename "${migration_file}" NAME)
        if(NOT migration_filename MATCHES "^([0-9][0-9][0-9])_([A-Za-z0-9][A-Za-z0-9_-]*)\\.sql$")
            message(FATAL_ERROR
                "Invalid migration filename '${migration_filename}'. Expected NNN_name.sql")
        endif()

        set(version_text "${CMAKE_MATCH_1}")
        set(name "${CMAKE_MATCH_2}")
        string(REGEX REPLACE "^0+" "" version "${version_text}")
        if(version STREQUAL "")
            set(version 0)
        endif()
        if(version EQUAL 0)
            message(FATAL_ERROR "Migration version must be greater than zero: ${migration_filename}")
        endif()
        if(version IN_LIST seen_versions)
            message(FATAL_ERROR "Duplicate migration version ${version}: ${migration_filename}")
        endif()
        if(name IN_LIST seen_names)
            message(FATAL_ERROR "Duplicate migration name '${name}': ${migration_filename}")
        endif()

        list(APPEND seen_versions "${version}")
        list(APPEND seen_names "${name}")
        list(APPEND versioned_migrations "${version_text}|${version}|${name}|${migration_file}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${migration_file}")
    endforeach()
    list(SORT versioned_migrations)

    get_filename_component(generated_directory "${generated_header}" DIRECTORY)
    file(MAKE_DIRECTORY "${generated_directory}")
    file(WRITE "${generated_header}" [=[#pragma once

#include <span>
#include <string_view>

namespace modra {

struct EmbeddedMigration {
    int version;
    std::string_view name;
    std::string_view sql;
};

std::span<const EmbeddedMigration> embedded_migrations();

}  // namespace modra
]=])

    file(WRITE "${generated_source}" [=[#include "modra/EmbeddedMigrations.h"

#include <array>

namespace modra {
namespace {

constexpr std::array migrations{
]=])

    foreach(migration_entry IN LISTS versioned_migrations)
        string(REPLACE "|" ";" migration_parts "${migration_entry}")
        list(GET migration_parts 0 version_text)
        list(GET migration_parts 1 version)
        list(GET migration_parts 2 name)
        list(GET migration_parts 3 migration_file)

        file(READ "${migration_file}" sql)
        set(delimiter "M${version_text}")
        string(FIND "${sql}" ")${delimiter}\"" delimiter_collision)
        while(NOT delimiter_collision EQUAL -1 AND NOT delimiter STREQUAL "M${version_text}XXXXXXXXXXXX")
            string(APPEND delimiter "X")
            string(FIND "${sql}" ")${delimiter}\"" delimiter_collision)
        endwhile()
        if(NOT delimiter_collision EQUAL -1)
            message(FATAL_ERROR "Could not choose a safe raw-string delimiter for ${migration_file}")
        endif()

        file(APPEND "${generated_source}"
            "    EmbeddedMigration{${version}, \"${name}\", R\"${delimiter}(${sql})${delimiter}\"},\n")
    endforeach()

    file(APPEND "${generated_source}" [=[};

}  // namespace

std::span<const EmbeddedMigration> embedded_migrations() {
    return migrations;
}

}  // namespace modra
]=])
endfunction()
