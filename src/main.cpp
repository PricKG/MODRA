#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <CLI/CLI.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "application/AppInfo.h"
#include "application/DashboardService.h"
#include "application/NoteService.h"
#include "application/ProjectService.h"
#include "application/TaskService.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"
#include "infrastructure/system/Environment.h"
#include "ui/App.h"

int main(int argc, char** argv) {
    CLI::App cli{"MODRA - organizacion personal de proyectos desde la consola"};
    cli.set_version_flag("--version", std::string(modra::application_name()) + " " +
                                           std::string(modra::application_version()));
    auto* doctor = cli.add_subcommand("doctor", "Comprueba el entorno local de MODRA");

    try {
        cli.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return cli.exit(error);
    }

    bool logging_ready = false;
    try {
        const auto requested_paths = modra::current_data_paths();
        const auto paths = modra::initialize_data_directory(requested_paths.root);

        auto logger = spdlog::basic_logger_mt("modra", (paths.logs / "modra.log").string());
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::flush_on(spdlog::level::info);
        logging_ready = true;
        spdlog::info("MODRA {} starting", modra::application_version());
        spdlog::info("Data directory: {}", paths.root.string());

        modra::Database database(paths.database);
        database.apply_migrations();

        if (doctor->parsed()) {
            bool writable = false;
            const auto probe = paths.root / ".modra-write-test";
            {
                std::ofstream output(probe, std::ios::binary | std::ios::trunc);
                output << "ok";
                writable = output.good();
            }
            std::error_code remove_error;
            std::filesystem::remove(probe, remove_error);

            const bool git_available = modra::command_is_available("git");
            const bool svn_available = modra::command_is_available("svn");
            std::cout << "MODRA doctor\n"
                      << "[OK] Version: " << modra::application_version() << '\n'
                      << "[OK] Sistema operativo: " << modra::operating_system_name() << '\n'
                      << "[OK] Directorio de datos: " << paths.root.string() << '\n'
                      << (writable ? "[OK]" : "[ERROR]") << " Escritura en directorio de datos\n"
                      << "[OK] SQLite inicializado\n"
                      << "[OK] SQLite version: " << database.sqlite_version() << '\n'
                      << (git_available ? "[OK]" : "[ADVERTENCIA]") << " Git: "
                      << (git_available ? "disponible" : "no disponible") << '\n'
                      << (svn_available ? "[OK]" : "[ADVERTENCIA]") << " SVN: "
                      << (svn_available ? "disponible" : "no disponible") << '\n';

            if (!writable) {
                throw std::runtime_error("The data directory is not writable");
            }
        } else {
            modra::ProjectService projects(database);
            modra::TaskService tasks(database);
            modra::DashboardService dashboard(database);
            modra::NoteService notes(database, paths.config, paths.root);
            modra::run_ui(projects, tasks, dashboard, notes);
        }

        spdlog::info("MODRA closing");
        spdlog::shutdown();
        return 0;
    } catch (const std::exception& error) {
        if (logging_ready) {
            spdlog::critical("Unhandled error: {}", error.what());
            spdlog::shutdown();
        }
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    } catch (...) {
        if (logging_ready) {
            spdlog::critical("Unhandled non-standard error");
            spdlog::shutdown();
        }
        std::cerr << "Error no controlado\n";
        return 1;
    }
}
