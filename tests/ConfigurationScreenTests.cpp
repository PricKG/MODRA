#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <nlohmann/json.hpp>

#include "infrastructure/config/DataDirectory.h"
#include "ui/ConfigurationScreen.h"

TEST_CASE("Configuration screen renders local paths and environment status") {
    const auto root = std::filesystem::temp_directory_path() / "modra-configuration-screen-test";
    modra::DataPaths paths{
        root,
        root / "modra.db",
        root / "config.json",
        root / "backups",
        root / "exports",
        root / "logs",
    };
    const auto screen = modra::create_configuration_screen(paths, "3.50.4");
    auto rendered = screen->Render();
    auto output = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
    Render(output, rendered);
    const std::string text = output.ToString();

    CHECK(text.find("MODRA · Configuración") != std::string::npos);
    CHECK(text.find("3.50.4") != std::string::npos);
    CHECK(text.find("modra.db") != std::string::npos);
    CHECK(text.find("Editor externo") != std::string::npos);
}

TEST_CASE("Configuration screen opens the editor picker and saves with Enter") {
    const auto root = std::filesystem::temp_directory_path() / "modra-configuration-editor-test";
    std::filesystem::create_directories(root);
    modra::DataPaths paths{
        root,
        root / "modra.db",
        root / "config.json",
        root / "backups",
        root / "exports",
        root / "logs",
    };
    const std::string editor_command = (root / "modra-test-editor").string() + " --wait";
    {
        std::ofstream executable(root / "modra-test-editor");
        executable << "test editor";
    }
    {
        std::ofstream output(paths.config);
        output << nlohmann::json{
                      {"version", 1},
                      {"custom", "preserved"},
                      {"editor", {{"command", editor_command}}},
                  }.dump(2)
               << '\n';
    }

    auto screen = modra::create_configuration_screen(paths, "3.50.4");
    screen->TakeFocus();
    REQUIRE(screen->OnEvent(ftxui::Event::Return));
    REQUIRE(screen->OnEvent(ftxui::Event::Return));

    auto rendered = screen->Render();
    auto rendered_output = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
    Render(rendered_output, rendered);
    CHECK(rendered_output.ToString().find("JSON válido") == std::string::npos);

    std::ifstream input(paths.config);
    const auto config = nlohmann::json::parse(input);
    CHECK(config.at("custom") == "preserved");
    CHECK(config.at("editor").at("command") == editor_command);
}

#ifdef _WIN32
TEST_CASE("Configuration screen selects, applies and refreshes an editor with Ctrl+S") {
    const auto root = std::filesystem::temp_directory_path() / "modra-configuration-keyboard-editor-test";
    std::filesystem::create_directories(root);
    modra::DataPaths paths{
        root,
        root / "modra.db",
        root / "config.json",
        root / "backups",
        root / "exports",
        root / "logs",
    };
    {
        std::ofstream output(paths.config);
        output << nlohmann::json{{"version", 1}, {"editor", {{"command", "notepad.exe"}}}}.dump(2) << '\n';
    }

    auto screen = modra::create_configuration_screen(paths, "3.50.4");
    screen->TakeFocus();
    REQUIRE(screen->OnEvent(ftxui::Event::Return));
    REQUIRE(screen->OnEvent(ftxui::Event::ArrowDown));
    REQUIRE(screen->OnEvent(ftxui::Event::CtrlS));

    std::ifstream input(paths.config);
    const auto config = nlohmann::json::parse(input);
    CHECK(config.at("editor").at("command") == "");

    auto rendered = screen->Render();
    auto output = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
    Render(output, rendered);
    const std::string text = output.ToString();
    CHECK(text.find("Editor automático configurado.") != std::string::npos);
    CHECK(text.find("Elegir editor") == std::string::npos);
}
#endif

TEST_CASE("Configuration screen keeps unavailable editor diagnostics out of the picker") {
    const auto root = std::filesystem::temp_directory_path() / "modra-configuration-disabled-editor-test";
    std::filesystem::create_directories(root);
    modra::DataPaths paths{
        root,
        root / "modra.db",
        root / "config.json",
        root / "backups",
        root / "exports",
        root / "logs",
    };
    {
        std::ofstream output(paths.config);
        output << nlohmann::json{{"version", 1}, {"editor", {{"command", "modra-editor-that-does-not-exist"}}}}.dump(2)
               << '\n';
    }

    auto screen = modra::create_configuration_screen(paths, "3.50.4");
    REQUIRE(screen->OnEvent(ftxui::Event::Return));
    auto rendered = screen->Render();
    auto output = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(50));
    Render(output, rendered);
    const std::string text = output.ToString();

    CHECK(text.find("modra-editor-that-does-not-exist") != std::string::npos);
    CHECK(text.find("[X]") == std::string::npos);
}
