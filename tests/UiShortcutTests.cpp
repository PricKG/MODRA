#include <cctype>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "ui/App.h"
#include "ui/KeyEvent.h"

TEST_CASE("Alphabetic shortcuts accept lowercase and uppercase") {
    const std::string shortcuts = "abcdefghilmnopqrstuvwxyz";
    for (const char lowercase : shortcuts) {
        const char uppercase = static_cast<char>(std::toupper(static_cast<unsigned char>(lowercase)));
        CHECK(modra::shortcut(ftxui::Event::Character(lowercase), lowercase));
        CHECK(modra::shortcut(ftxui::Event::Character(uppercase), lowercase));
    }
}

TEST_CASE("Main navigation starts on Dashboard and changes content with arrows") {
    modra::MainNavigationState navigation;
    std::vector<std::string> sections{"Dashboard", "Proyectos", "Mi trabajo", "Conocimiento", "Herramientas",
                                      "Configuración"};
    auto menu = ftxui::Menu(&sections, &navigation.selected);
    CHECK(navigation.selected == 0);
    CHECK(navigation.active == 0);
    CHECK(navigation.focus == modra::MainFocus::menu);

    int previous = navigation.selected;
    CHECK(menu->OnEvent(ftxui::Event::ArrowDown));
    CHECK(navigation.activate_selected(previous));
    CHECK(navigation.selected == 1);
    CHECK(navigation.active == 1);
    previous = navigation.selected;
    CHECK(menu->OnEvent(ftxui::Event::ArrowUp));
    CHECK(navigation.activate_selected(previous));
    CHECK(navigation.selected == 0);
    CHECK(navigation.active == 0);
}

TEST_CASE("Main navigation respects boundaries and ignores j and k") {
    modra::MainNavigationState navigation;
    std::vector<std::string> sections{"Dashboard", "Proyectos", "Mi trabajo", "Conocimiento", "Herramientas",
                                      "Configuración"};
    auto menu = ftxui::Menu(&sections, &navigation.selected);
    int previous = navigation.selected;
    menu->OnEvent(ftxui::Event::ArrowUp);
    CHECK_FALSE(navigation.activate_selected(previous));
    CHECK(navigation.selected == 0);
    menu->OnEvent(ftxui::Event::Character('j'));
    menu->OnEvent(ftxui::Event::Character('J'));
    menu->OnEvent(ftxui::Event::Character('k'));
    menu->OnEvent(ftxui::Event::Character('K'));
    CHECK(navigation.selected == 0);

    for (int index = 1; index < modra::MainNavigationState::section_count; ++index) {
        previous = navigation.selected;
        REQUIRE(menu->OnEvent(ftxui::Event::ArrowDown));
        REQUIRE(navigation.activate_selected(previous));
    }
    CHECK(navigation.selected == modra::MainNavigationState::section_count - 1);
    previous = navigation.selected;
    menu->OnEvent(ftxui::Event::ArrowDown);
    CHECK_FALSE(navigation.activate_selected(previous));
    CHECK(navigation.selected == modra::MainNavigationState::section_count - 1);
}

TEST_CASE("An active content panel blocks main menu navigation") {
    modra::MainNavigationState navigation;
    navigation.focus = modra::MainFocus::content;
    CHECK(navigation.selected == 0);
    CHECK(navigation.active == 0);
}

TEST_CASE("A shortcut does not match a different letter") {
    CHECK_FALSE(modra::shortcut(ftxui::Event::Character('x'), 'n'));
    CHECK_FALSE(modra::shortcut(ftxui::Event::Character('X'), 'n'));
}
