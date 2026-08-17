#include "main_activity.hpp"
#include "views/auto_tab_frame.hpp"
#include "views/custom_button.hpp"
#include "settings_activity.hpp"
#include "../core/logger.hpp"

MainActivity::MainActivity() {
    brls::Logger::info("MainActivity created");
}

void MainActivity::onContentAvailable() {
    settingButton->registerClickAction([this](brls::View* view) {
        brls::Application::pushActivity(new SettingsActivity(), brls::TransitionAnimation::FADE);
        return true;
    });
}

MainActivity::~MainActivity() {
    brls::Logger::info("MainActivity destroyed");
}
