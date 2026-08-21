#include "settings_tab.hpp"
#include "../../core/addon_manager.hpp"
#include "../../core/web_server.hpp"
#include "../../core/playback_settings.hpp"
#include "../../core/stremio_auth.hpp"
#include <borealis.hpp>
#include <borealis/views/dropdown.hpp>
#include <borealis/views/dialog.hpp>
#include <functional>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <algorithm>
#include <pthread.h>

using namespace brls;

static void spawnBgThread(std::function<void()> task) {
    struct ThreadData {
        std::function<void()> task;
    };
    ThreadData* data = new ThreadData{std::move(task)};
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    auto threadFunc = [](void* arg) -> void* {
        ThreadData* d = static_cast<ThreadData*>(arg);
        if (d->task) d->task();
        delete d;
        return nullptr;
    };
    pthread_create(&thread, &attr, threadFunc, data);
    pthread_detach(thread);
    pthread_attr_destroy(&attr);
}

static std::string langCodeToLabel(const std::string& code) {
    for (const auto& opt : PlaybackSettings::getInstance().getLanguageOptions()) {
        if (opt.code == code)
            return opt.label;
    }
    return code;
}

static brls::Box* createSettingCard(const std::string& title, const std::string& subtitle, brls::View* control) {
    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setPadding(18, 20, 18, 20);
    row->setMarginBottom(12);
    row->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
    row->setCornerRadius(8);
    row->setAlignItems(brls::AlignItems::CENTER);

    brls::Box* infoCol = new brls::Box(brls::Axis::COLUMN);
    infoCol->setGrow(1);

    brls::Label* titleLbl = new brls::Label();
    titleLbl->setText(title);
    titleLbl->setFontSize(20);
    infoCol->addView(titleLbl);

    if (!subtitle.empty()) {
        brls::Label* subLbl = new brls::Label();
        subLbl->setText(subtitle);
        subLbl->setFontSize(14);
        subLbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        subLbl->setMarginTop(4);
        infoCol->addView(subLbl);
    }

    row->addView(infoCol);
    if (control) row->addView(control);
    return row;
}

SettingsTab::SettingsTab() : Box(Axis::COLUMN) {
    alive = std::make_shared<bool>(true);
    this->setGrow(1.0f);
    this->setPadding(20, 30, 20, 30);

    // Top Category Navigation Bar
    topBar = new brls::Box(Axis::ROW);
    topBar->setMarginBottom(20);
    topBar->setAlignItems(AlignItems::CENTER);

    std::vector<std::string> categories = {
        "Sobre",
        "Complementos",
        "Catalogo",
        "Reproduccion",
        "Cuenta"
    };

    for (size_t i = 0; i < categories.size(); i++) {
        brls::Button* btn = new brls::Button();
        btn->setText(categories[i]);
        btn->setFontSize(18.0f);
        btn->setMarginRight(12.0f);
        btn->setCornerRadius(6.0f);
        btn->setPadding(10, 18, 10, 18);

        int idx = (int)i;
        btn->registerClickAction([this, idx](brls::View* v) {
            this->selectSection(idx);
            return true;
        });

        navButtons.push_back(btn);
        topBar->addView(btn);
    }

    this->addView(topBar);

    // Content Area
    contentContainer = new brls::Box(Axis::COLUMN);
    contentContainer->setGrow(1.0f);
    this->addView(contentContainer);

    selectSection(0);
}

SettingsTab::~SettingsTab() {
    if (alive) *alive = false;
}

brls::View* SettingsTab::create() {
    return new SettingsTab();
}

void SettingsTab::updateNavButtonStyles() {
    Theme theme = Application::getTheme();
    for (int i = 0; i < (int)navButtons.size(); i++) {
        if (i == currentSection) {
            navButtons[i]->setBackgroundColor(theme["color/grey_1"]);
            navButtons[i]->setTextColor(theme["color/tsvitch"]);
        } else {
            navButtons[i]->setBackgroundColor(theme["brls/background"]);
            navButtons[i]->setTextColor(theme["brls/text"]);
        }
    }
}

void SettingsTab::selectSection(int index) {
    currentSection = index;
    updateNavButtonStyles();

    contentContainer->clearViews();

    brls::View* view = nullptr;
    switch (index) {
        case 0: view = createAboutSection(); break;
        case 1: view = createAddonsSection(); break;
        case 2: view = createCatalogSection(); break;
        case 3: view = createPlaybackSection(); break;
        case 4: view = createAccountSection(); break;
        default: view = createAboutSection(); break;
    }

    if (view) {
        view->setGrow(1.0f);
        contentContainer->addView(view);
    }
}

// -----------------------------------------------------------------------------
// [0] SOBRE
// -----------------------------------------------------------------------------
brls::View* SettingsTab::createAboutSection() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    brls::Box* container = new brls::Box(brls::Axis::COLUMN);
    container->setPadding(10, 10, 30, 10);

    brls::Label* title = new brls::Label();
    title->setText("StremioNX v0.3");
    title->setFontSize(22);
    title->setMarginBottom(15);
    container->addView(title);

    brls::Label* intro = new brls::Label();
    intro->setText("Cliente de streaming para Nintendo Switch basado en el protocolo Stremio.");
    intro->setFontSize(18);
    intro->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    intro->setMarginBottom(25);
    container->addView(intro);

    auto addSectionHeader = [container](const std::string& h) {
        brls::Label* lbl = new brls::Label();
        lbl->setText("| " + h);
        lbl->setFontSize(22);
        lbl->setMarginBottom(8);
        container->addView(lbl);
    };

    auto addSectionLine = [container](const std::string& l) {
        brls::Label* lbl = new brls::Label();
        lbl->setText(l);
        lbl->setFontSize(16);
        lbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        lbl->setMarginBottom(6);
        container->addView(lbl);
    };

    addSectionHeader("Librerias de codigo abierto");
    addSectionLine("- borealis (Apache-2.0): UI framework para Nintendo Switch");
    addSectionLine("- nlohmann/json (MIT): procesado de JSON");
    addSectionLine("- libcurl (MIT): peticiones HTTP/HTTPS");
    addSectionLine("- mpv (GPL-2.0+): motor de reproduccion de video");
    addSectionLine("- libwebp (BSD-3-Clause): decodificacion de imagenes WebP");
    addSectionLine("- libnx (devkitPro): SDK homebrew de Nintendo Switch");
    addSectionLine("- tinyxml2, yoga, fmt, tweeny, nanovg (incluidos en borealis)");

    brls::Box* sp = new brls::Box(brls::Axis::COLUMN);
    sp->setHeight(15);
    container->addView(sp);

    addSectionHeader("Creditos");
    addSectionLine("Desarrollado por DL3G0");

    scroll->setContentView(container);
    return scroll;
}

// -----------------------------------------------------------------------------
// [1] COMPLEMENTOS (ADDONS)
// -----------------------------------------------------------------------------
brls::View* SettingsTab::createAddonsSection() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    brls::Box* container = new brls::Box(brls::Axis::COLUMN);
    container->setPadding(10, 10, 30, 10);

    // Web Addon Hint
    brls::Box* hintBox = new brls::Box(brls::Axis::COLUMN);
    hintBox->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
    hintBox->setCornerRadius(8);
    hintBox->setPadding(16, 20, 16, 20);
    hintBox->setMarginBottom(20);

    brls::Label* hintTitle = new brls::Label();
    hintTitle->setText("Agregar Complementos");
    hintTitle->setFontSize(20);
    hintTitle->setMarginBottom(6);
    hintBox->addView(hintTitle);

    std::string ip = WebServer::getInstance().getLocalIP();
    std::string msg = "Abre un navegador en tu PC o movil conectado a la misma red e ingresa a:\nhttp://" + ip + ":8080\nTambien puedes sincronizar los complementos de tu cuenta en la pestana \"Cuenta\".";

    brls::Label* hintMsg = new brls::Label();
    hintMsg->setText(msg);
    hintMsg->setFontSize(16);
    hintMsg->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    hintBox->addView(hintMsg);

    container->addView(hintBox);

    // List of Addons
    auto manifests = AddonManager::getInstance().getInstalledManifests();
    for (const auto& m : manifests) {
        brls::Box* row = new brls::Box(brls::Axis::ROW);
        row->setPadding(16, 20, 16, 20);
        row->setMarginBottom(10);
        row->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
        row->setCornerRadius(8);
        row->setAlignItems(brls::AlignItems::CENTER);

        brls::Label* badge = new brls::Label();
        badge->setText("[+]");
        badge->setFontSize(20);
        badge->setTextColor(brls::Application::getTheme()["color/tsvitch"]);
        badge->setMarginRight(15);
        row->addView(badge);

        brls::Box* info = new brls::Box(brls::Axis::COLUMN);
        info->setGrow(1);

        brls::Box* titleRow = new brls::Box(brls::Axis::ROW);
        titleRow->setAlignItems(brls::AlignItems::BASELINE);

        brls::Label* nameLbl = new brls::Label();
        nameLbl->setText(m.name.empty() ? m.url : m.name);
        nameLbl->setFontSize(22);
        nameLbl->setMarginRight(10);
        titleRow->addView(nameLbl);

        if (!m.version.empty()) {
            brls::Label* vLbl = new brls::Label();
            vLbl->setText("v" + m.version);
            vLbl->setFontSize(14);
            vLbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
            titleRow->addView(vLbl);
        }
        info->addView(titleRow);

        if (!m.description.empty()) {
            brls::Label* desc = new brls::Label();
            desc->setText(m.description);
            desc->setFontSize(14);
            desc->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
            desc->setMarginTop(4);
            info->addView(desc);
        }
        row->addView(info);

        if (m.url.find("cinemeta") == std::string::npos) {
            brls::Button* btnDel = new brls::Button();
            btnDel->setText("Desinstalar");
            auto aliveRef = alive;
            std::string url = m.url;
            btnDel->registerClickAction([this, aliveRef, url](brls::View* v) {
                spawnBgThread([this, aliveRef, url]() {
                    AddonManager::getInstance().removeAddon(url);
                    brls::sync([this, aliveRef]() {
                        if (*aliveRef) this->selectSection(1);
                    });
                });
                return true;
            });
            row->addView(btnDel);
        } else {
            brls::Label* core = new brls::Label();
            core->setText("App Core");
            core->setFontSize(16);
            core->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
            row->addView(core);
        }

        container->addView(row);
    }

    scroll->setContentView(container);
    return scroll;
}

// -----------------------------------------------------------------------------
// [2] GESTION DE CATALOGOS
// -----------------------------------------------------------------------------
brls::View* SettingsTab::createCatalogSection() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    brls::Box* container = new brls::Box(brls::Axis::COLUMN);
    container->setPadding(10, 10, 30, 10);

    auto catalogs = AddonManager::getInstance().getAllCatalogs();
    for (const auto& cat : catalogs) {
        brls::Box* row = new brls::Box(brls::Axis::ROW);
        row->setPadding(14, 18, 14, 18);
        row->setMarginBottom(10);
        row->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
        row->setCornerRadius(8);
        row->setAlignItems(brls::AlignItems::CENTER);

        std::string key = cat.type + ":" + cat.id;
        bool isHidden = AddonManager::getInstance().isCatalogHidden(key);

        brls::Label* catName = new brls::Label();
        catName->setText(cat.name + " (" + cat.type + ")");
        catName->setFontSize(20);
        if (isHidden) {
            catName->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        }
        row->addView(catName);

        brls::Box* spacer = new brls::Box(brls::Axis::ROW);
        spacer->setGrow(1.0f);
        row->addView(spacer);

        // Up
        brls::Button* btnUp = new brls::Button();
        btnUp->setText("Subir");
        btnUp->setMarginRight(8);
        btnUp->registerClickAction([this, key](brls::View* v) {
            AddonManager::getInstance().moveCatalogUp(key);
            this->selectSection(2);
            return true;
        });
        row->addView(btnUp);

        // Down
        brls::Button* btnDown = new brls::Button();
        btnDown->setText("Bajar");
        btnDown->setMarginRight(8);
        btnDown->registerClickAction([this, key](brls::View* v) {
            AddonManager::getInstance().moveCatalogDown(key);
            this->selectSection(2);
            return true;
        });
        row->addView(btnDown);

        // Toggle Hide/Show
        brls::Button* btnTgl = new brls::Button();
        btnTgl->setText(isHidden ? "Mostrar" : "Ocultar");
        btnTgl->registerClickAction([this, key](brls::View* v) {
            AddonManager::getInstance().toggleCatalogHidden(key);
            this->selectSection(2);
            return true;
        });
        row->addView(btnTgl);

        container->addView(row);
    }

    scroll->setContentView(container);
    return scroll;
}

// -----------------------------------------------------------------------------
// [3] REPRODUCCION
// -----------------------------------------------------------------------------
brls::View* SettingsTab::createPlaybackSection() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    brls::Box* container = new brls::Box(brls::Axis::COLUMN);
    container->setPadding(10, 10, 30, 10);

    // Subtitulos toggle
    brls::Button* subsTgl = new brls::Button();
    auto updateSubs = [subsTgl]() {
        subsTgl->setText(PlaybackSettings::getInstance().subsEnabled() ? "Si" : "No");
    };
    updateSubs();
    subsTgl->registerClickAction([updateSubs](brls::View* v) {
        PlaybackSettings& s = PlaybackSettings::getInstance();
        s.setSubsEnabled(!s.subsEnabled());
        updateSubs();
        return true;
    });
    container->addView(createSettingCard("Subtitulos por defecto", "Mostrar subtitulos automaticamente al reproducir", subsTgl));

    // Idioma subtitulos
    brls::Button* subsLangBtn = new brls::Button();
    auto updateSubsLang = [subsLangBtn]() {
        subsLangBtn->setText(langCodeToLabel(PlaybackSettings::getInstance().subsLang()));
    };
    updateSubsLang();
    subsLangBtn->registerClickAction([updateSubsLang](brls::View* v) {
        auto& s = PlaybackSettings::getInstance();
        const auto& opts = s.getLanguageOptions();
        std::vector<std::string> vals, codes;
        int sel = 0;
        for (size_t i = 0; i < opts.size(); i++) {
            vals.push_back(opts[i].label);
            codes.push_back(opts[i].code);
            if (opts[i].code == s.subsLang()) sel = (int)i;
        }
        auto* dd = new brls::Dropdown("Idioma de subtitulos", vals, [codes, updateSubsLang](int idx) {
            if (idx >= 0 && idx < (int)codes.size()) {
                PlaybackSettings::getInstance().setSubsLang(codes[idx]);
                updateSubsLang();
            }
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
        return true;
    });
    container->addView(createSettingCard("Idioma de subtitulos", "Idioma preferido para subtitulos", subsLangBtn));

    // Pista de audio
    brls::Button* audioLangBtn = new brls::Button();
    auto updateAudioLang = [audioLangBtn]() {
        audioLangBtn->setText(langCodeToLabel(PlaybackSettings::getInstance().audioLang()));
    };
    updateAudioLang();
    audioLangBtn->registerClickAction([updateAudioLang](brls::View* v) {
        auto& s = PlaybackSettings::getInstance();
        const auto& opts = s.getLanguageOptions();
        std::vector<std::string> vals, codes;
        int sel = 0;
        for (size_t i = 0; i < opts.size(); i++) {
            vals.push_back(opts[i].label);
            codes.push_back(opts[i].code);
            if (opts[i].code == s.audioLang()) sel = (int)i;
        }
        auto* dd = new brls::Dropdown("Pista de audio por defecto", vals, [codes, updateAudioLang](int idx) {
            if (idx >= 0 && idx < (int)codes.size()) {
                PlaybackSettings::getInstance().setAudioLang(codes[idx]);
                updateAudioLang();
            }
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
        return true;
    });
    container->addView(createSettingCard("Pista de audio por defecto", "Idioma de audio preferido", audioLangBtn));

    // 4K Toggle
    brls::Button* show4kBtn = new brls::Button();
    auto update4k = [show4kBtn]() {
        show4kBtn->setText(PlaybackSettings::getInstance().show4KSources() ? "Si" : "No");
    };
    update4k();
    show4kBtn->registerClickAction([update4k](brls::View* v) {
        PlaybackSettings& s = PlaybackSettings::getInstance();
        s.setShow4KSources(!s.show4KSources());
        update4k();
        return true;
    });
    container->addView(createSettingCard("Mostrar fuentes 4K", "Mostrar enlaces 4K/8K en la lista", show4kBtn));

    scroll->setContentView(container);
    return scroll;
}

// -----------------------------------------------------------------------------
// [4] CUENTA STREMIO
// -----------------------------------------------------------------------------
brls::View* SettingsTab::createAccountSection() {
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    brls::Box* container = new brls::Box(brls::Axis::COLUMN);
    container->setPadding(10, 10, 30, 10);

    StremioAuth& auth = StremioAuth::getInstance();

    brls::Box* statusBox = new brls::Box(brls::Axis::COLUMN);
    statusBox->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
    statusBox->setCornerRadius(8);
    statusBox->setPadding(16, 20, 16, 20);
    statusBox->setMarginBottom(20);

    brls::Label* stTitle = new brls::Label();
    stTitle->setFontSize(20);
    stTitle->setText(auth.isLoggedIn() ? "Sesion iniciada" : "Sin sesion vinculada");
    statusBox->addView(stTitle);

    if (auth.isLoggedIn()) {
        std::string mail = auth.getUser().email.empty() ? "Cuenta vinculada" : auth.getUser().email;
        brls::Label* uLbl = new brls::Label();
        uLbl->setText(mail);
        uLbl->setFontSize(16);
        uLbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        uLbl->setMarginTop(4);
        statusBox->addView(uLbl);
    }
    container->addView(statusBox);

    auto aliveRef = alive;

    if (auth.isLoggedIn()) {
        // Sync
        brls::Button* syncBtn = new brls::Button();
        syncBtn->setText("Sincronizar");
        syncBtn->registerClickAction([this, aliveRef](brls::View* v) {
            spawnBgThread([this, aliveRef]() {
                std::vector<std::string> urls;
                bool ok = StremioAuth::getInstance().fetchAddons(urls);
                int added = 0;
                if (ok) added = AddonManager::getInstance().installAddons(urls);
                brls::sync([ok, added]() {
                    brls::Dialog* d = new brls::Dialog(
                        ok ? (added > 0 ? "Se sincronizaron " + std::to_string(added) + " complementos."
                                        : "Todos los complementos ya estaban instalados.")
                           : "Error al sincronizar con tu cuenta Stremio.");
                    d->addButton("Aceptar", []() {});
                    d->open();
                });
            });
            return true;
        });
        container->addView(createSettingCard("Sincronizar complementos", "Instalar complementos que tienes en otros dispositivos", syncBtn));

        // Logout
        brls::Button* logoutBtn = new brls::Button();
        logoutBtn->setText("Cerrar sesion");
        logoutBtn->registerClickAction([this](brls::View* v) {
            StremioAuth::getInstance().logout();
            this->selectSection(4);
            return true;
        });
        container->addView(createSettingCard("Cerrar sesion", "Desvincular esta consola de tu cuenta Stremio", logoutBtn));
    } else {
        // Login
        brls::Button* loginBtn = new brls::Button();
        loginBtn->setText("Iniciar sesion");
        loginBtn->registerClickAction([this, aliveRef](brls::View* v) {
            StremioDeviceLink link;
            if (!StremioAuth::getInstance().createDeviceLink(link)) {
                brls::Dialog* d = new brls::Dialog("No se pudo conectar con el servicio de Stremio.\nVerifica tu conexion.");
                d->addButton("Aceptar", []() {});
                d->open();
                return true;
            }

            brls::Dialog* diag = new brls::Dialog(
                "Codigo de activacion:\n\n" + link.code + "\n\nAbre este enlace en tu navegador para autorizar:\n" + link.link);
            diag->setCancelable(false);
            auto cancel = std::make_shared<std::atomic<bool>>(false);
            diag->addButton("Cancelar", [cancel]() { *cancel = true; });
            diag->open();

            spawnBgThread([this, aliveRef, diag, cancel, code = link.code]() {
                int attempts = 0;
                while (!*cancel) {
                    std::string authKey;
                    LinkPollStatus st = StremioAuth::getInstance().pollDeviceLink(code, authKey);
                    if (st == LinkPollStatus::Authorized) {
                        if (StremioAuth::getInstance().loginWithToken(authKey)) {
                            std::vector<std::string> urls;
                            if (StremioAuth::getInstance().fetchAddons(urls)) {
                                AddonManager::getInstance().installAddons(urls);
                            }
                        }
                        brls::sync([this, aliveRef, diag]() {
                            diag->close();
                            if (*aliveRef) this->selectSection(4);
                        });
                        return;
                    }
                    if (st == LinkPollStatus::Error || ++attempts >= 100) {
                        brls::sync([this, aliveRef, diag]() {
                            diag->close();
                            if (*aliveRef) this->selectSection(4);
                        });
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            });
            return true;
        });
        container->addView(createSettingCard("Iniciar sesion con Stremio", "Autoriza este dispositivo desde tu movil o PC", loginBtn));
    }

    scroll->setContentView(container);
    return scroll;
}
