#include "catalog_cell.hpp"
#include <memory>
#include <borealis/core/application.hpp>
#include "../../ui/application.hpp" // For DetailsActivity
#include "../../core/http_client.hpp"
#include "../../core/task_queue.hpp"
#include <webp/decode.h>
#include <nanovg.h>
extern "C" {
#include <borealis/extern/nanovg/stb_image.h>
}

static void applyPosterTexture(brls::Image* poster, const std::string& url, uint8_t* rgba, int w, int h, uint64_t req_id) {
    int cached = PosterTextureCache::find(url);
    if (cached > 0) {
        brls::Logger::info("PosterTask: cache hit for req={}", req_id);
        poster->setFreeTexture(false);
        poster->innerSetImage(cached);
        return;
    }
    brls::Logger::info("PosterTask: upload start {}x{} for req={}", w, h, req_id);
    NVGcontext* vg = brls::Application::getNVGContext();
    int tex = nvgCreateImageRGBA(vg, w, h, 0, rgba);
    brls::Logger::info("PosterTask: texture {} created for req={}", tex, req_id);
    if (tex > 0) {
        int existing = PosterTextureCache::put(url, tex);
        if (existing > 0) {
            nvgDeleteImage(vg, tex);
            tex = existing;
        }
        int evicted = PosterTextureCache::evictIfNeeded();
        if (evicted > 0) nvgDeleteImage(vg, evicted);
        poster->setFreeTexture(false);
        poster->innerSetImage(tex);
        brls::Logger::info("PosterTask: innerSetImage done for req={}", req_id);
    }
}

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
    poster->setFreeTexture(false);
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
    auto alive = this->alive;

    if (!item.poster_url.empty()) {
        std::string url = item.poster_url;
        if (url.find("//") == 0) {
            url = "https:" + url;
        }
        ASYNC_RETAIN
        TaskQueue::getInstance().push([this, alive, url, req_id, ASYNC_TOKEN]() {
            brls::Logger::info("PosterTask: start for {} req={}", url, req_id);
            if (!*alive) {
                brls::Logger::info("PosterTask: cell gone for req={}", req_id);
                brls::sync([ASYNC_TOKEN]() { ASYNC_RELEASE });
                return;
            }
            int cached = PosterTextureCache::find(url);
            if (cached > 0) {
                brls::Logger::info("PosterTask: cache hit pre for {} req={}", url, req_id);
                brls::sync([this, alive, url, cached, req_id, ASYNC_TOKEN]() {
                    ASYNC_RELEASE
                    if (!*alive) {
                        brls::Logger::info("PosterTask: cell gone, drop cached req={}", req_id);
                        return;
                    }
                    if (this->image_req_id == req_id) {
                        this->poster->setFreeTexture(false);
                        this->poster->innerSetImage(cached);
                        brls::Logger::info("PosterTask: cache set for req={}", req_id);
                    }
                });
                return;
            }
            std::vector<unsigned char> data;
            if (HttpClient::getInstance().getBinary(url, data)) {
                brls::Logger::info("PosterTask: downloaded {} bytes for {}", data.size(), url);
                bool isWebp = data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P';
                brls::Logger::info("PosterTask: isWebp={} for req={}", isWebp, req_id);
                
                if (isWebp) {
                    int w, h;
                    uint8_t* rgba = WebPDecodeRGBA(data.data(), data.size(), &w, &h);
                    brls::Logger::info("PosterTask: WebPDecodeRGBA done {}x{} for req={}", w, h, req_id);
                    if (rgba) {
                        if (w > 2048 || h > 2048 || w <= 0 || h <= 0) {
                            brls::Logger::info("PosterTask: oversized webp {}x{} for req={}", w, h, req_id);
                            WebPFree(rgba);
                            brls::sync([ASYNC_TOKEN]() { ASYNC_RELEASE });
                            return;
                        }
                        brls::sync([this, alive, url, req_id, rgba, w, h, ASYNC_TOKEN]() {
                            std::unique_ptr<uint8_t, void (*)(void*)> guard(rgba, WebPFree);
                            ASYNC_RELEASE
                            if (!*alive) {
                                brls::Logger::info("PosterTask: cell gone, drop decoded req={}", req_id);
                                return;
                            }
                            if (this->image_req_id != req_id) {
                                brls::Logger::info("PosterTask: stale req={} skip upload", req_id);
                                return;
                            }
                            if (brls::Application::isInputBlocks()) {
                                brls::Logger::info("PosterTask: transition, skip upload for req={}", req_id);
                                return;
                            }
                            if (PosterUploadDeferred()) {
                                brls::Logger::info("PosterTask: defer upload for req={}", req_id);
                                std::shared_ptr<uint8_t> held(guard.release(), WebPFree);
                                brls::delay(150, [this, alive, url, req_id, w, h, held]() mutable {
                                    if (!*alive) {
                                        brls::Logger::info("PosterTask: cell gone, drop deferred req={}", req_id);
                                        return;
                                    }
                                    if (this->image_req_id == req_id) {
                                        applyPosterTexture(this->poster, url, held.get(), w, h, req_id);
                                    }
                                });
                                return;
                            }
                            applyPosterTexture(this->poster, url, rgba, w, h, req_id);
                        });
                        return;
                    }
                }

                // Fallback / Not WebP
                int w, h;
                stbi_uc* rgba = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, nullptr, 4);
                brls::Logger::info("PosterTask: stbi decoded {}x{} for req={}", w, h, req_id);
                if (rgba) {
                    if (w > 2048 || h > 2048 || w <= 0 || h <= 0) {
                        brls::Logger::info("PosterTask: oversized image {}x{} for req={}", w, h, req_id);
                        stbi_image_free(rgba);
                        brls::sync([ASYNC_TOKEN]() { ASYNC_RELEASE });
                        return;
                    }
                    brls::sync([this, alive, url, req_id, rgba, w, h, ASYNC_TOKEN]() {
                        std::unique_ptr<stbi_uc, void (*)(void*)> guard(rgba, stbi_image_free);
                        ASYNC_RELEASE
                        if (!*alive) {
                            brls::Logger::info("PosterTask: cell gone, drop decoded req={}", req_id);
                            return;
                        }
                        if (this->image_req_id != req_id) {
                            brls::Logger::info("PosterTask: stale req={} skip upload", req_id);
                            return;
                        }
                        if (brls::Application::isInputBlocks()) {
                            brls::Logger::info("PosterTask: transition, skip upload for req={}", req_id);
                            return;
                        }
                        if (PosterUploadDeferred()) {
                            brls::Logger::info("PosterTask: defer upload for req={}", req_id);
                            std::shared_ptr<stbi_uc> held(guard.release(), stbi_image_free);
                            brls::delay(150, [this, alive, url, req_id, w, h, held]() mutable {
                                if (!*alive) {
                                    brls::Logger::info("PosterTask: cell gone, drop deferred req={}", req_id);
                                    return;
                                }
                                if (this->image_req_id == req_id) {
                                    applyPosterTexture(this->poster, url, held.get(), w, h, req_id);
                                }
                            });
                            return;
                        }
                        applyPosterTexture(this->poster, url, rgba, w, h, req_id);
                    });
                } else {
                    brls::Logger::info("PosterTask: stbi decode failed for req={}", req_id);
                    brls::sync([ASYNC_TOKEN]() { ASYNC_RELEASE });
                }
            } else {
                brls::Logger::info("PosterTask: download failed for {}", url);
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

CatalogCell::~CatalogCell() {
    *alive = false;
}

void CatalogCell::prepareForReuse() {
    title->setText("");
    poster->clear();
}

void CatalogCell::cacheForReuse() {
    // Optional caching logic
}
