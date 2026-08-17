

#pragma once

#include <borealis/core/box.hpp>
#include <borealis/core/event.hpp>

class CustomButton : public brls::Box {
public:
    CustomButton();

    ~CustomButton() override;

    static View *create();

    void onFocusGained() override;

    void onFocusLost() override;

    brls::Event<bool> *getFocusEvent();

    View *getNextFocus(brls::FocusDirection direction, View *currentView) override;

    void setCustomNavigation(std::function<brls::View *(brls::FocusDirection)> navigation);

private:
    brls::Event<bool> focusEvent;
    std::function<brls::View *(brls::FocusDirection)> customNavigation = nullptr;
};