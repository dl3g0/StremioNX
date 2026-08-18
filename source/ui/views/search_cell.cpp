#include "search_cell.hpp"
#include <borealis/core/application.hpp>
#include <borealis/core/box.hpp>
#include <borealis/core/thread.hpp>
#include "../../core/http_client.hpp"
#include "../../core/task_queue.hpp"
#include <webp/decode.h>
#include <nanovg.h>

SearchCell::SearchCell() {
    this->setFocusable(true);
    this->setHighlightCornerRadius(8);
    this->setAxis(brls::Axis::COLUMN);
    this->setWidth(brls::View::AUTO);
    this->setHeight(brls::View::AUTO);
    this->setMaxWidth(230);

    // The poster fills the whole cell so the focus highlight matches the
    // poster exactly; the title is overlaid on its bottom edge.
    brls::Box* pic_box = new brls::Box();
    pic_box->setWidthPercentage(100);
    pic_box->setHeightPercentage(100);
    pic_box->setBackgroundColor(brls::Application::getTheme()["color/grey_3"]);
    pic_box->setCornerRadius(4);

    poster = new brls::Image();
    poster->setScalingType(brls::ImageScalingType::FIT);
    poster->setHeightPercentage(100);
    poster->setWidthPercentage(100);
    poster->setCornerRadius(4);

    pic_box->addView(poster);

    brls::Box* title_bar = new brls::Box();
    title_bar->setAxis(brls::Axis::ROW);
    title_bar->setJustifyContent(brls::JustifyContent::CENTER);
    title_bar->setAlignItems(brls::AlignItems::CENTER);
    title_bar->setWidthPercentage(100);
    title_bar->setHeight(38);
    title_bar->setPositionType(brls::PositionType::ABSOLUTE);
    title_bar->setPositionBottom(0);
    title_bar->setBackgroundColor(nvgRGBA(0, 0, 0, 165));

    title = new brls::Label();
    title->setFontSize(15);
    title->setSingleLine(true);
    title->setWidth(210);
    title->setTextColor(nvgRGBA(255, 255, 255, 255));
    title->setHorizontalAlign(brls::HorizontalAlign::CENTER);

    title_bar->addView(title);
    pic_box->addView(title_bar);

    this->addView(pic_box);
}

void SearchCell::setMeta(const MetaItem& it) {
    item = it;
    title->setText(it.name);

    poster->clear();

    this->image_req_id++;
    uint64_t req_id = this->image_req_id;

    if (!it.poster_url.empty()) {
        std::string url = it.poster_url;
        if (url.find("//") == 0) {
            url = "https:" + url;
        }
        ASYNC_RETAIN
        TaskQueue::getInstance().push([this, url, req_id, ASYNC_TOKEN]() {
            if (this->image_req_id != req_id) {
                brls::sync([ASYNC_TOKEN]() { ASYNC_RELEASE });
                return;
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

RecyclingGridItem* SearchCell::create() {
    return new SearchCell();
}

void SearchCell::prepareForReuse() {
    title->setText("");
    poster->clear();
}

void SearchCell::cacheForReuse() {
}
