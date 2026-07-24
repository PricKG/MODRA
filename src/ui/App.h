#pragma once

#include <string>

#include "infrastructure/config/DataDirectory.h"

namespace modra {

class ProjectService;
class TaskService;
class DashboardService;
class NoteService;

enum class MainFocus { menu, content };

struct MainNavigationState {
    static constexpr int section_count = 6;

    int selected = 0;
    int active = 0;
    MainFocus focus = MainFocus::menu;

    bool activate_selected(int previous_selection) {
        active = selected;
        return selected != previous_selection;
    }

    void enter_content() {
        focus = MainFocus::content;
    }

    void return_to_menu() {
        focus = MainFocus::menu;
    }
};

void run_ui(ProjectService& projects,
            TaskService& tasks,
            DashboardService& dashboard,
            NoteService& notes,
            DataPaths paths,
            std::string sqlite_version);

}  // namespace modra
