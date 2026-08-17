

#pragma once

#include <borealis/views/label.hpp>

class HintLabel : public brls::Label {
public:
    HintLabel();

    ~HintLabel() override;

    static View* create();
};