#include "settings_activity.hpp"
#include "../core/addon_manager.hpp"
#include "../core/web_server.hpp"
#include "../core/playback_settings.hpp"
#include <borealis/views/dropdown.hpp>
#include <functional>

using namespace brls;

SettingsActivity::SettingsActivity() {}

SettingsActivity::~SettingsActivity() {}

brls::View* SettingsActivity::createContentView() {
    brls::TabFrame* tabs = new brls::TabFrame();
    
    tabs->addTab("Sobre", [this] { return this->createAboutTab(); });
    tabs->addTab("Addons", [this] { return this->createAddonsTab(); });
    tabs->addTab("Catálogo", [this] { return this->createCatalogManagerTab(); });
    tabs->addTab("Reproducción", [this] { return this->createPlaybackTab(); });
    tabs->addTab("UI", [] { return new brls::Box(brls::Axis::COLUMN); });
    
    brls::AppletFrame* frame = new brls::AppletFrame(tabs);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    
    // Setup back action for Activity
    frame->registerAction("Back", brls::BUTTON_B, [this](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    
    return frame;
}

static brls::Label* aboutSectionHeader(const std::string& text) {
    brls::Label* header = new brls::Label();
    header->setText("| " + text);
    header->setFontSize(24);
    header->setMarginBottom(10);
    return header;
}

static brls::Label* aboutBodyLine(const std::string& text, bool muted = false) {
    brls::Label* line = new brls::Label();
    line->setText(text);
    line->setFontSize(18);
    line->setMarginBottom(8);
    if (muted) line->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    return line;
}

brls::View* SettingsActivity::createAboutTab() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();

    brls::Box* container = new brls::Box(brls::Axis::COLUMN);
    container->setPadding(50, 50, 50, 50);

    brls::Label* title = new brls::Label();
    title->setText("StremioNX v0.1");
    title->setFontSize(40);
    title->setMarginBottom(30);
    container->addView(title);

    container->addView(aboutSectionHeader("Introducción"));
    container->addView(aboutBodyLine("Stremio para Nintendo Switch", true));
    container->addView(aboutBodyLine("Cliente de streaming para la consola, basado en el protocolo y ecosistema de Stremio (catálogos, complementos y fuentes).", true));
    container->addView(aboutBodyLine(""));

    container->addView(aboutSectionHeader("Librerías usadas (código abierto)"));
    container->addView(aboutBodyLine("- borealis (Apache-2.0): framework de interfaz de usuario", true));
    container->addView(aboutBodyLine("- nlohmann/json (MIT): parseo de JSON", true));
    container->addView(aboutBodyLine("- libcurl (curl / MIT): peticiones HTTP", true));
    container->addView(aboutBodyLine("- mpv (GPL-2.0+): motor de reproducción de video", true));
    container->addView(aboutBodyLine("- libwebp (BSD-3-Clause): decodificación de imágenes WebP", true));
    container->addView(aboutBodyLine("- libnx (devkitPro): SDK homebrew de Nintendo Switch", true));
    container->addView(aboutBodyLine("- tinyxml2 (zlib), yoga (MIT), fmt (MIT), tweeny (MIT), nanovg (zlib): librerías incluidas en borealis", true));
    container->addView(aboutBodyLine("Todas las librerías se distribuyen bajo sus respectivas licencias de código abierto. Ver README para los enlaces.", true));
    container->addView(aboutBodyLine(""));

    container->addView(aboutSectionHeader("Hecho por"));
    container->addView(aboutBodyLine("DL3G0"));
    container->addView(aboutBodyLine("Desarrollador del proyecto StremioNX.", true));

    scroll->setContentView(container);
    return scroll;
}

brls::View* SettingsActivity::createAddonsTab() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    auto alive = std::make_shared<bool>(true);
    
    auto rebuild = std::make_shared<std::function<void()>>();
    *rebuild = [this, scroll, alive, rebuild]() {
        if (!*alive) return;
        
        brls::Box* container = new brls::Box(brls::Axis::COLUMN);
        container->setPadding(50, 50, 50, 50);
        
        // Header Row with Title and Instructions
        brls::Box* headerRow = new brls::Box(brls::Axis::ROW);
        headerRow->setMarginBottom(30);
        headerRow->setAlignItems(brls::AlignItems::CENTER);
        
        brls::Label* title = new brls::Label();
        title->setText("Complementos");
        title->setFontSize(40);
        title->setGrow(1); // take up available space
        headerRow->addView(title);
        
        container->addView(headerRow);
        
        // Web Addon Hint (Prominent)
        brls::Box* hintBox = new brls::Box(brls::Axis::COLUMN);
        hintBox->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
        hintBox->setCornerRadius(8);
        hintBox->setPadding(20, 20, 20, 20);
        hintBox->setMarginBottom(30);
        
        brls::Label* webHintTitle = new brls::Label();
        webHintTitle->setText("Agregar Complementos");
        webHintTitle->setFontSize(22);
        webHintTitle->setMarginBottom(10);
        hintBox->addView(webHintTitle);
        
        brls::Label* webHint = new brls::Label();
        webHint->setText("Para agregar nuevos complementos, abre el navegador en tu celular o PC e ingresa a:\nhttp://" + WebServer::getInstance().getLocalIP() + ":8080\n(Los cambios se aplican automáticamente)");
        webHint->setFontSize(18);
        webHint->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        hintBox->addView(webHint);
        
        container->addView(hintBox);
        
        // List of Addons
        const auto manifests = AddonManager::getInstance().getInstalledManifests();
        for (const auto& manifest : manifests) {
            brls::Box* row = new brls::Box(brls::Axis::ROW);
            row->setPadding(20, 20, 20, 20);
            row->setMarginBottom(10);
            row->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
            row->setCornerRadius(8);
            row->setAlignItems(brls::AlignItems::CENTER);
            
            // Icon (Default to setting icon for now)
            brls::Image* icon = new brls::Image();
            icon->setImageFromRes("png/ico-setting.png");
            icon->setWidth(48);
            icon->setHeight(48);
            icon->setMarginRight(20);
            row->addView(icon);
            
            // Info Column
            brls::Box* infoCol = new brls::Box(brls::Axis::COLUMN);
            infoCol->setGrow(1);
            
            brls::Box* nameRow = new brls::Box(brls::Axis::ROW);
            nameRow->setAlignItems(brls::AlignItems::BASELINE);
            
            brls::Label* nameLbl = new brls::Label();
            nameLbl->setText(manifest.name.empty() ? manifest.url : manifest.name);
            nameLbl->setFontSize(24);
            nameLbl->setMarginRight(10);
            nameRow->addView(nameLbl);
            
            if (!manifest.version.empty()) {
                brls::Label* verLbl = new brls::Label();
                verLbl->setText("v" + manifest.version);
                verLbl->setFontSize(14);
                verLbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
                nameRow->addView(verLbl);
            }
            infoCol->addView(nameRow);
            
            if (!manifest.description.empty()) {
                brls::Label* descLbl = new brls::Label();
                descLbl->setText(manifest.description);
                descLbl->setFontSize(16);
                descLbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
                descLbl->setMarginTop(5);
                infoCol->addView(descLbl);
            }
            
            row->addView(infoCol);
            
            if (manifest.url.find("cinemeta") == std::string::npos) {
                // Uninstall Button
                brls::Button* btnUninstall = new brls::Button();
                btnUninstall->setText("Desinstalar");
                std::string targetUrl = manifest.url;
                btnUninstall->registerClickAction([alive, rebuild, targetUrl](brls::View* view) {
                    AddonManager::getInstance().removeAddon(targetUrl);
                    brls::delay(1, [alive, rebuild]() {
                        if (*alive) (*rebuild)();
                    });
                    return true;
                });
                
                row->addView(btnUninstall);
            } else {
                // Un-uninstallable core addon indicator
                brls::Label* coreLbl = new brls::Label();
                coreLbl->setText("App Core");
                coreLbl->setFontSize(16);
                coreLbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
                row->addView(coreLbl);
            }
            
            container->addView(row);
        }
        
        scroll->setContentView(container);
    };
    
    (*rebuild)();
    return scroll;
}

brls::View* SettingsActivity::createCatalogManagerTab() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    auto alive = std::make_shared<bool>(true);
    
    auto rebuild = std::make_shared<std::function<void()>>();
    *rebuild = [this, scroll, alive, rebuild]() {
        if (!*alive) return;
        
        brls::Box* container = new brls::Box(brls::Axis::COLUMN);
        container->setPadding(20, 20, 20, 20);
        
        brls::Label* title = new brls::Label();
        title->setText("Gestión de Catálogo");
        title->setFontSize(28);
        title->setMarginBottom(30);
        container->addView(title);
        
        auto catalogs = AddonManager::getInstance().getAllCatalogs();
        
        for (const auto& cat : catalogs) {
            brls::Box* row = new brls::Box(brls::Axis::ROW);
            row->setMarginBottom(15);
            row->setAlignItems(brls::AlignItems::CENTER);
            row->setFocusable(true);
            row->setCornerRadius(8);
            row->setPadding(10, 10, 10, 10);
            
            brls::Label* catName = new brls::Label();
            catName->setText(cat.name + " (" + cat.type + ")");
            catName->setFontSize(20);
            
            std::string key = cat.type + ":" + cat.id;
            bool isHidden = AddonManager::getInstance().isCatalogHidden(key);
            if (isHidden) {
                catName->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
            }
            
            row->addView(catName);
            
            // Spacer
            brls::Box* spacer = new brls::Box(brls::Axis::ROW);
            spacer->setGrow(1.0f);
            row->addView(spacer);
            
            // Up button
            brls::Button* btnUp = new brls::Button();
            btnUp->setText("Subir");
            btnUp->setMarginRight(10);
            btnUp->registerClickAction([alive, rebuild, key](brls::View* view) {
                AddonManager::getInstance().moveCatalogUp(key);
                brls::delay(1, [alive, rebuild]() {
                    if (*alive) (*rebuild)();
                });
                return true;
            });
            row->addView(btnUp);
            
            // Down button
            brls::Button* btnDown = new brls::Button();
            btnDown->setText("Bajar");
            btnDown->setMarginRight(10);
            btnDown->registerClickAction([alive, rebuild, key](brls::View* view) {
                AddonManager::getInstance().moveCatalogDown(key);
                brls::delay(1, [alive, rebuild]() {
                    if (*alive) (*rebuild)();
                });
                return true;
            });
            row->addView(btnDown);
            
            // Hide/Show button
            brls::Button* btnToggle = new brls::Button();
            btnToggle->setText(isHidden ? "Mostrar" : "Ocultar");
            btnToggle->registerClickAction([alive, rebuild, key](brls::View* view) {
                AddonManager::getInstance().toggleCatalogHidden(key);
                brls::delay(1, [alive, rebuild]() {
                    if (*alive) (*rebuild)();
                });
                return true;
            });
            row->addView(btnToggle);
            
            container->addView(row);
        }
        
        scroll->setContentView(container);
    };
    
    (*rebuild)();
    return scroll;
}

static std::string langLabelForCode(const std::string& code) {
    for (const auto& opt : PlaybackSettings::getInstance().getLanguageOptions()) {
        if (opt.code == code)
            return opt.label;
    }
    return code;
}

static brls::Box* makeSettingRow(const std::string& title, const std::string& subtitle, brls::Button* button) {
    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setPadding(20, 20, 20, 20);
    row->setMarginBottom(15);
    row->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
    row->setCornerRadius(8);
    row->setAlignItems(brls::AlignItems::CENTER);

    brls::Box* infoCol = new brls::Box(brls::Axis::COLUMN);
    infoCol->setGrow(1);

    brls::Label* titleLbl = new brls::Label();
    titleLbl->setText(title);
    titleLbl->setFontSize(22);
    infoCol->addView(titleLbl);

    if (!subtitle.empty()) {
        brls::Label* subLbl = new brls::Label();
        subLbl->setText(subtitle);
        subLbl->setFontSize(15);
        subLbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        subLbl->setMarginTop(4);
        infoCol->addView(subLbl);
    }

    row->addView(infoCol);
    row->addView(button);
    return row;
}

brls::View* SettingsActivity::createPlaybackTab() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();

    brls::Box* container = new brls::Box(brls::Axis::COLUMN);
    container->setPadding(20, 20, 20, 20);

    brls::Label* title = new brls::Label();
    title->setText("Reproducción");
    title->setFontSize(28);
    title->setMarginBottom(30);
    container->addView(title);

    // Subtítulos por defecto (Sí / No)
    brls::Button* subsToggle = new brls::Button();
    auto updateSubsToggle = [subsToggle]() {
        subsToggle->setText(PlaybackSettings::getInstance().subsEnabled() ? "Sí" : "No");
    };
    updateSubsToggle();
    subsToggle->registerClickAction([updateSubsToggle](brls::View* view) {
        PlaybackSettings& s = PlaybackSettings::getInstance();
        s.setSubsEnabled(!s.subsEnabled());
        updateSubsToggle();
        return true;
    });
    container->addView(makeSettingRow("Subtítulos por defecto", "Mostrar subtítulos automáticamente al reproducir", subsToggle));

    // Idioma de subtítulos
    brls::Button* subsLangBtn = new brls::Button();
    auto updateSubsLang = [subsLangBtn]() {
        subsLangBtn->setText(langLabelForCode(PlaybackSettings::getInstance().subsLang()));
    };
    updateSubsLang();
    subsLangBtn->registerClickAction([updateSubsLang](brls::View* view) {
        auto& settings = PlaybackSettings::getInstance();
        const auto& opts = settings.getLanguageOptions();

        std::vector<std::string> values;
        std::vector<std::string> codes;
        int selected = 0;
        for (size_t i = 0; i < opts.size(); i++) {
            values.push_back(opts[i].label);
            codes.push_back(opts[i].code);
            if (opts[i].code == settings.subsLang())
                selected = (int)i;
        }

        auto* dropdown = new brls::Dropdown("Idioma de subtítulos", values, [codes, updateSubsLang](int index) {
            if (index >= 0 && index < (int)codes.size()) {
                PlaybackSettings::getInstance().setSubsLang(codes[index]);
                updateSubsLang();
            }
        }, selected);

        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });
    container->addView(makeSettingRow("Idioma de subtítulos", "Idioma que se seleccionará por defecto", subsLangBtn));

    // Pista de audio por defecto
    brls::Button* audioLangBtn = new brls::Button();
    auto updateAudioLang = [audioLangBtn]() {
        audioLangBtn->setText(langLabelForCode(PlaybackSettings::getInstance().audioLang()));
    };
    updateAudioLang();
    audioLangBtn->registerClickAction([updateAudioLang](brls::View* view) {
        auto& settings = PlaybackSettings::getInstance();
        const auto& opts = settings.getLanguageOptions();

        std::vector<std::string> values;
        std::vector<std::string> codes;
        int selected = 0;
        for (size_t i = 0; i < opts.size(); i++) {
            values.push_back(opts[i].label);
            codes.push_back(opts[i].code);
            if (opts[i].code == settings.audioLang())
                selected = (int)i;
        }

        auto* dropdown = new brls::Dropdown("Pista de audio por defecto", values, [codes, updateAudioLang](int index) {
            if (index >= 0 && index < (int)codes.size()) {
                PlaybackSettings::getInstance().setAudioLang(codes[index]);
                updateAudioLang();
            }
        }, selected);

        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });
    container->addView(makeSettingRow("Pista de audio por defecto", "Idioma de audio preferido (ej: Español Latino)", audioLangBtn));

    scroll->setContentView(container);
    return scroll;
}
