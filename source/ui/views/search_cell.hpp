#pragma once

#include <borealis/views/label.hpp>
#include <borealis/views/image.hpp>
#include "recycling_grid.hpp"
#include "../../core/addon_manager.hpp"

class SearchCell : public RecyclingGridItem {
public:
    SearchCell();

    void setMeta(const MetaItem& item);

    static RecyclingGridItem* create();

    void prepareForReuse() override;
    void cacheForReuse() override;

private:
    MetaItem item;
    brls::Image* poster;
    brls::Label* title;
    uint64_t image_req_id = 0;
};
