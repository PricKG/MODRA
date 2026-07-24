#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "application/ProjectService.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"
#include "ui/ToolsScreen.h"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() /
               ("modra-tools-screen-tests-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(++suffix))) {
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    static int suffix;
    std::filesystem::path path;
};

int TemporaryDirectory::suffix = 0;

class ToolsFixture {
public:
    ToolsFixture()
        : paths(modra::initialize_data_directory(temporary.path / "data")),
          database(paths.database),
          projects(database) {
        database.apply_migrations();
    }

    modra::Project create_project(const std::string& name,
                                  const std::string& alias,
                                  const std::filesystem::path& local_path) {
        modra::ProjectInput input;
        input.name = name;
        input.alias = alias;
        input.local_path = local_path.string();
        return projects.create(std::move(input));
    }

    TemporaryDirectory temporary;
    modra::DataPaths paths;
    modra::Database database;
    modra::ProjectService projects;
};

std::string render(const ftxui::Component& component) {
    auto output = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
    Render(output, component->Render());
    return output.ToString();
}

}  // namespace

TEST_CASE("Tools screen shows local projects and detected repositories") {
    ToolsFixture fixture;
    const auto git_path = fixture.temporary.path / "git-project";
    std::filesystem::create_directories(git_path / ".git");
    fixture.create_project("Proyecto Git", "proyecto-git", git_path);
    fixture.create_project("Proyecto sin ruta válida", "proyecto-sin-ruta", fixture.temporary.path / "missing");

    auto screen = modra::create_tools_screen(fixture.projects);
    const std::string text = render(screen);

    CHECK(text.find("MODRA · Herramientas") != std::string::npos);
    CHECK(text.find("Proyecto Git") != std::string::npos);
    CHECK(text.find("Git") != std::string::npos);
    CHECK(text.find("Ruta no encontrada") != std::string::npos);
}

TEST_CASE("Tools screen refreshes repository information without restarting") {
    ToolsFixture fixture;
    const auto repository_path = fixture.temporary.path / "repository";
    fixture.create_project("Repositorio", "repositorio", repository_path);

    auto screen = modra::create_tools_screen(fixture.projects);
    CHECK(render(screen).find("Ruta no encontrada") != std::string::npos);

    std::filesystem::create_directories(repository_path / ".git");
    REQUIRE(screen->OnEvent(ftxui::Event::Character('r')));

    const std::string refreshed = render(screen);
    CHECK(refreshed.find("Ruta no encontrada") == std::string::npos);
    CHECK(refreshed.find("La carpeta no contiene metadatos") == std::string::npos);
}

TEST_CASE("Tools screen filters projects that require attention") {
    ToolsFixture fixture;
    const auto healthy_path = fixture.temporary.path / "healthy";
    std::filesystem::create_directories(healthy_path);
    fixture.create_project("Carpeta saludable", "saludable", healthy_path);
    fixture.create_project("Ruta pendiente", "pendiente", fixture.temporary.path / "missing");

    auto screen = modra::create_tools_screen(fixture.projects);
    CHECK(render(screen).find("Carpeta saludable") != std::string::npos);
    CHECK(render(screen).find("Ruta pendiente") != std::string::npos);

    REQUIRE(screen->OnEvent(ftxui::Event::Character('v')));
    const std::string filtered = render(screen);
    CHECK(filtered.find("Vista: requieren atención") != std::string::npos);
    CHECK(filtered.find("Carpeta saludable") == std::string::npos);
    CHECK(filtered.find("Ruta pendiente") != std::string::npos);
}
