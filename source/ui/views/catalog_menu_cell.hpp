#pragma once

#include "recycling_grid.hpp"
#include <borealis/core/bind.hpp>
#include <borealis/views/label.hpp>

class CatalogMenuCell : public RecyclingGridItem {
public:
    explicit CatalogMenuCell();
    
    void setTitle(const std::string& title);
    void setSelected(bool selected);
    
    void prepareForReuse() override;
    void cacheForReuse() override;
    
    static RecyclingGridItem* create();

private:
    BRLS_BIND(brls::Label, labelTitle, "title");
    NVGcolor selectedColor{};
    NVGcolor fontColor{};
};
