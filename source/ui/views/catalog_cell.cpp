#include "catalog_cell.hpp"
#include <borealis/core/application.hpp>
#include "../../ui/application.hpp" // For DetailsActivity
#include "../../core/http_client.hpp"
#include "../../core/task_queue.hpp"
#include <webp/decode.h>
#include <nanovg.h>

CatalogCell::CatalogCell() {
    this->setFocusable(true);
    this->setHighlightCornerRadius(8);
    this->setAxis(brls::Axis::COLUMN);
    this->setWidth(brls::View::AUTO);
    this->setHeight(brls::View::AUTO);
    this->setMaxWidth(230); // Prevent Yoga from expanding cell for long text

    // Box pic_box
    brls::Box* pic_box = new brls::Box();
    pic_box->setWidthPercentage(100);
    pic_box->setHeightPercentage(85);
    pic_box->setBackgroundColor(brls::Application::getTheme()["color/grey_3"]);
    pic_box->setCornerRadius(4);
    
    poster = new brls::Image();
    poster->setScalingType(brls::ImageScalingType::FIT);
    poster->setHeightPercentage(100);
    poster->setWidthPercentage(100);
    poster->setCornerRadius(4);
    
    pic_box->addView(poster);
    this->addView(pic_box);

    // Box text_box
    brls::Box* text_box = new brls::Box();
    text_box->setAxis(brls::Axis::COLUMN);
    text_box->setWidthPercentage(100);
    text_box->setHeightPercentage(15);

    title = new brls::Label();
    title->setFontSize(16);
    title->setSingleLine(false);
    title->setWidth(210); 
    title->setMarginTop(10); // Add top margin to separate from poster
    title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    
    text_box->addView(title);
    this->addView(text_box);
    
    this->registerClickAction([this](brls::View* view) {
        auto items = AddonManager::getInstance().getCatalog();
        if (currentIndex < items.size()) {
            brls::Application::pushActivity(new DetailsActivity(items[currentIndex]));
        }
        return true;
    });
}

void CatalogCell::setMetaItem(const MetaItem& item, size_t index) {
    currentIndex = index;
    title->setText(item.name);
    
    poster->clear(); // Clear previous image
    
    this->image_req_id++;
    uint64_t req_id = this->image_req_id;

    if (!item.poster_url.empty()) {
        std::string url = item.poster_url;
        if (url.find("//") == 0) {
            url = "https:" + url;
        }
        ASYNC_RETAIN
        TaskQueue::getInstance().push([this, url, req_id, ASYNC_TOKEN]() {
            if (this->image_req_id != req_id) {
                brls::sync([ASYNC_TOKEN]() { ASYNC_RELEASE });
                return; // Abort early if cell was reused
            }
            std::vector<unsigned char> data;
            if (HttpClient::getInstance().getBinary(url, data)) {
                bool isWebp = data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P';
                
                if (isWebp) {
                    int w, h;
                    uint8_t* rgba = WebPDecodeRGBA(data.data(), data.size(), &w, &h);
                    if (rgba) {
                        brls::sync([this, req_id, rgba, w, h, ASYNC_TOKEN]() {
                            ASYNC_RELEASE
                            if (this->image_req_id == req_id) {
                                NVGcontext* vg = brls::Application::getNVGContext();
                                int tex = nvgCreateImageRGBA(vg, w, h, 0, rgba);
                                if (tex > 0) this->poster->innerSetImage(tex);
                            }
                            WebPFree(rgba);
                        });
                        return;
                    }
                }

                // Fallback / Not WebP
                brls::sync([this, req_id, data = std::move(data), ASYNC_TOKEN]() {
                    ASYNC_RELEASE
                    if (this->image_req_id == req_id && !data.empty()) {
                        this->poster->setImageFromMem(data.data(), data.size());
                    }
                });
            } else {
                brls::sync([ASYNC_TOKEN]() {
                    ASYNC_RELEASE
                });
            }
        });
    }
}

RecyclingGridItem* CatalogCell::create() {
    return new CatalogCell();
}

void CatalogCell::prepareForReuse() {
    title->setText("");
    poster->clear();
}

void CatalogCell::cacheForReuse() {
    // Optional caching logic
}
