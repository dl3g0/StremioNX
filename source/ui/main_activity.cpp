#include "main_activity.hpp"
#include "views/auto_tab_frame.hpp"
#include "../core/logger.hpp"

MainActivity::MainActivity() {
    brls::Logger::info("MainActivity created");
}

void MainActivity::onContentAvailable() {
}

MainActivity::~MainActivity() {
    brls::Logger::info("MainActivity destroyed");
}
