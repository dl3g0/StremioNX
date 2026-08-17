#include <borealis.hpp>
#include <iostream>
#include "core/addon_manager.hpp"
#include "core/http_client.hpp"
#include "core/web_server.hpp"
#include "core/logger.hpp"
#include "core/file_paths.hpp"
#include "ui/application.hpp"
#include "ui/main_activity.hpp"
#include "ui/views/auto_tab_frame.hpp"
#include "ui/views/recycling_grid.hpp"
#include "ui/views/custom_button.hpp"
#include "ui/views/hint_label.hpp"
#include "ui/views/catalog_menu_cell.hpp"
#include "ui/views/catalog_tab.hpp"
#include "ui/theme.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

int main(int argc, char* argv[]) {
#ifdef __SWITCH__
    FilePaths::ensureDataDir();
    freopen(FilePaths::kLogFile, "w", stdout);
    freopen(FilePaths::kLogFile, "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif

    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    brls::Logger::info("StremioNX Starting...");
    
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("StremioNX");
    brls::Logger::info("Borealis Window created");

    brls::Logger::info("Initializing HttpClient...");
    HttpClient::getInstance().init();
    brls::Logger::info("HttpClient initialized.");
    
    brls::Logger::info("Initializing AddonManager...");
    AddonManager::getInstance();
    brls::Logger::info("AddonManager initialized.");

    brls::Logger::info("Initializing WebServer...");
    WebServer::getInstance().start();
    brls::Logger::info("WebServer initialized.");

    // Initialize custom themes and styles
    Theme::initCustomTheme();
    Theme::initCustomStyle();
    
    brls::Logger::info("Registering XML views...");
    brls::Application::registerXMLView("AutoTabFrame", AutoTabFrame::create);
    brls::Application::registerXMLView("RecyclingGrid", RecyclingGrid::create);
    brls::Application::registerXMLView("CustomButton", CustomButton::create);
    brls::Application::registerXMLView("HintLabel", HintLabel::create);
    brls::Application::registerXMLView("CatalogTab", CatalogTab::create);
    brls::Application::registerXMLView("CatalogMenuCell", CatalogMenuCell::create);

    brls::Logger::info("Pushing MainActivity...");
    brls::Application::pushActivity(new MainActivity());
    brls::Logger::info("MainActivity pushed. Starting mainLoop...");

    while (brls::Application::mainLoop()) {
        // Borealis main loop
    }

    HttpClient::getInstance().cleanup();

    return EXIT_SUCCESS;
}
