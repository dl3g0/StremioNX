#pragma once

#include <borealis.hpp>
#include <memory>
#include <vector>
#include <string>

class SettingsTab : public brls::Box {
public:
    SettingsTab();
    ~SettingsTab() override;

    static brls::View* create();

private:
    std::shared_ptr<bool> alive;

    brls::Box* topBar = nullptr;
    brls::Box* contentContainer = nullptr;
    std::vector<brls::Button*> navButtons;

    int currentSection = 0;

    void selectSection(int index);
    brls::View* createAboutSection();
    brls::View* createAddonsSection();
    brls::View* createCatalogSection();
    brls::View* createPlaybackSection();
    brls::View* createAccountSection();

    void updateNavButtonStyles();
};
