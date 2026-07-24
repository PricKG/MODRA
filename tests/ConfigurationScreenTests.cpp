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
    CHECK(text.find("Herramientas detectadas") != std::string::npos);
}

TEST_CASE("Configuration screen saves the selected external editor command") {
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
    REQUIRE(screen->OnEvent(ftxui::Event::CtrlS));

    std::ifstream input(paths.config);
    const auto config = nlohmann::json::parse(input);
    CHECK(config.at("custom") == "preserved");
    CHECK(config.at("editor").at("command") == editor_command);
}

TEST_CASE("Configuration screen renders unavailable editor options as disabled") {
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
    auto rendered = screen->Render();
    auto output = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(50));
    Render(output, rendered);
    const std::string text = output.ToString();

    CHECK(text.find("[X]") != std::string::npos);
    CHECK(text.find("modra-editor-that-does-not-exist") != std::string::npos);
}
