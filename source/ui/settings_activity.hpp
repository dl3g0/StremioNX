#pragma once
#include <borealis.hpp>

class SettingsActivity : public brls::Activity {
public:
    SettingsActivity();
    ~SettingsActivity() override;

    brls::View* createContentView() override;
    
private:
    brls::View* createAboutTab();
    brls::View* createAddonsTab();
    brls::View* createCatalogManagerTab();
    brls::View* createPlaybackTab();
};
