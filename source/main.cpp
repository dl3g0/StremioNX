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
#include "ui/views/search_tab.hpp"
#include "ui/views/settings_tab.hpp"
#include "ui/theme.hpp"

#ifdef __SWITCH__
#include <switch.h>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
#endif

#ifdef __SWITCH__
extern "C" {
    // Run as a regular application so libnx initializes the applet service,
    // which is required for appletSetAutoSleepDisabled() to take effect.
    u32 __nx_applet_type = AppletType_Application;
    // Raise the heap to 256MB. Multiple 2MB worker threads plus concurrent
    // poster/catalog curls were exhausting the default heap.
    size_t __nx_heap_size = 0x10000000;
}

static void autoSleepHook(AppletHookType hook, void* param) {
    (void)param;
    if (hook == AppletHookType_OnResume)
        appletSetAutoSleepDisabled(true);
}

static AppletHookCookie autoSleepHookCookie;

static void crashHandler(int sig) {
    char buf[40];
    char* p = buf;
    *p++ = '\n';
    *p++ = '=';
    *p++ = '=';
    *p++ = '=';
    *p++ = ' ';
    *p++ = 'C';
    *p++ = 'R';
    *p++ = 'A';
    *p++ = 'S';
    *p++ = 'H';
    *p++ = ':';
    *p++ = ' ';
    if (sig >= 10) {
        *p++ = (char)('0' + sig / 10);
    }
    *p++ = (char)('0' + sig % 10);
    *p++ = '\n';
    write(2, buf, (size_t)(p - buf));
    fsync(2);
    _exit(1);
}

static void installCrashHandler() {
    signal(SIGSEGV, crashHandler);
    signal(SIGBUS, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGILL, crashHandler);
}
#endif

int main(int argc, char* argv[]) {
#ifdef __SWITCH__
    FilePaths::ensureDataDir();
    Result appletRc = appletInitialize();
    if (R_SUCCEEDED(appletRc))
        appletRc = appletSetAutoSleepDisabled(true);
    if (R_FAILED(appletRc))
        brls::Logger::error("Failed to disable auto-sleep: 0x%x", appletRc);
    else
        appletHook(&autoSleepHookCookie, autoSleepHook, nullptr);
    freopen(FilePaths::kLogFile, "w", stdout);
    freopen(FilePaths::kLogFile, "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    installCrashHandler();
#endif

    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    brls::Logger::info("StremioNX Starting...");

#ifdef __SWITCH__
    brls::FontLoader::USER_EMOJI_PATH = "romfs:/fonts/OpenMoji.ttf";
#endif
    
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
    brls::Application::registerXMLView("SearchTab", SearchTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);
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
