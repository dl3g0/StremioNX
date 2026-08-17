#pragma once

#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>

class CustomButton;
class AutoTabFrame;

class MainActivity : public brls::Activity {
public:
    MainActivity();
    ~MainActivity() override;

    void onContentAvailable() override;

    CONTENT_FROM_XML_RES("activity/main.xml");

private:
    BRLS_BIND(AutoTabFrame, tabFrame, "main/tabFrame");
    BRLS_BIND(CustomButton, settingButton, "main/setting");
};
