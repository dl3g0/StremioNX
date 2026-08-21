#pragma once

#include <borealis/views/label.hpp>
#include <borealis/views/image.hpp>
#include "recycling_grid.hpp"
#include "../../core/addon_manager.hpp"
#include <memory>

class CatalogCell : public RecyclingGridItem {
public:
    CatalogCell();
    ~CatalogCell() override;

    void setMetaItem(const MetaItem& item, size_t index);

    static RecyclingGridItem* create();

    void prepareForReuse() override;
    void cacheForReuse() override;

private:
    brls::Image* poster;
    brls::Label* title;
    uint64_t image_req_id = 0;
    size_t currentIndex;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
