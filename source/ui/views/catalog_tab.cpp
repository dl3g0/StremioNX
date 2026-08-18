#include "catalog_tab.hpp"
#include "catalog_menu_cell.hpp"
#include "catalog_cell.hpp"
#include "../../core/addon_manager.hpp"
#include "../../core/logger.hpp"
#include <borealis/core/application.hpp>
#include <pthread.h>
#include <atomic>
#include "../application.hpp"

using namespace brls;

class CatalogMenuDataSource : public RecyclingGridDataSource {
public:
    using OnGroupSelected = std::function<void(const std::string&, const std::string&)>;
    explicit CatalogMenuDataSource(std::vector<CatalogDef> catalogs, OnGroupSelected cb = nullptr)
        : list(std::move(catalogs)), onGroupSelected(cb) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        CatalogMenuCell* item = dynamic_cast<CatalogMenuCell*>(recycler->dequeueReusableCell("Cell"));
        if (!item) {
            item = new CatalogMenuCell();
        }
        item->setTitle(list[index].name + " (" + list[index].type + ")");
        item->setSelected(index == selectedIndex);
        return item;
    }

    size_t getItemCount() override { return list.size(); }

    void setSelectedIndex(RecyclingGrid* recycler, size_t index) {
        if (index >= list.size()) return;
        
        std::vector<RecyclingGridItem*>& items = recycler->getGridItems();
        for (auto& i : items) {
            auto* cell = dynamic_cast<CatalogMenuCell*>(i);
            if (cell) cell->setSelected(false);
        }

        selectedIndex = index;
        auto* item = dynamic_cast<CatalogMenuCell*>(recycler->getGridItemByIndex(index));
        if (item) item->setSelected(true);

        if (onGroupSelected) onGroupSelected(list[index].type, list[index].id);
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        setSelectedIndex(recycler, index);
    }

    void clearData() override { list.clear(); }

private:
    std::vector<CatalogDef> list;
    OnGroupSelected onGroupSelected;
    size_t selectedIndex = 0;
};

class CatalogDataSource : public RecyclingGridDataSource {
public:
    CatalogDataSource(std::vector<MetaItem> items) : items(std::move(items)) {}
    
    size_t getItemCount() override {
        return items.size();
    }
    
    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        CatalogCell* cell = dynamic_cast<CatalogCell*>(recycler->dequeueReusableCell("Cell"));
        if (!cell) {
            cell = new CatalogCell();
        }
        cell->setMetaItem(items[index], index);
        return cell;
    }
    
    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        brls::Application::pushActivity(new DetailsActivity(items[index]));
    }
    
    void clearData() override {
        items.clear();
    }
    
public:
    std::vector<MetaItem> items;
};

CatalogTab::CatalogTab() {
    this->inflateFromXMLRes("xml/fragment/catalog_tab.xml");
    alive = std::make_shared<bool>(true);
    
    // Set grid properties that cannot be set via XML
    categories_grid->registerCell("Cell", []() { return new CatalogMenuCell(); });
    
    items_grid->registerCell("Cell", []() { return CatalogCell::create(); });
    
    // Refresh the catalog list automatically whenever addons/catalogs change
    // (e.g. an addon was added via the web server) so a restart is not needed.
    // addAddon/removeAddon already re-fetch manifests and toggle/move already
    // re-sort the cached lists before notifying, so here we only need to
    // reload the menu from the in-memory data (no network).
    std::shared_ptr<bool> aliveRef = alive;
    changeToken = AddonManager::getInstance().addCatalogsChangedCallback([aliveRef, this]() {
        brls::sync([aliveRef, this]() {
            if (*aliveRef) this->loadCatalogs();
        });
    });
    
    reloadCatalogs();
}

CatalogTab::~CatalogTab() {
    *alive = false;
    AddonManager::getInstance().removeCatalogsChangedCallback(changeToken);
    // Do NOT delete itemsDataSource or menuDataSource manually,
    // RecyclingGrid takes ownership and deletes them.
}

void CatalogTab::reloadCatalogs() {
    // Only one manifest reload at a time. The web server can fire many
    // change callbacks (addon add/remove, reorder, hide) while a previous
    // reload is still running; each one spawning its own fetchManifest()
    // thread exhausted the applet heap and crashed inside curl.
    {
        std::lock_guard<std::mutex> lock(reload_mutex);
        if (reload_in_flight) return;
        reload_in_flight = true;
    }
    
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    
    auto threadFunc = [](void* arg) -> void* {
        CatalogTab* tab = static_cast<CatalogTab*>(arg);
        if (AddonManager::getInstance().fetchManifest()) {
            brls::sync([tab]() {
                if (*(tab->alive)) tab->loadCatalogs();
            });
        }
        {
            std::lock_guard<std::mutex> lock(tab->reload_mutex);
            tab->reload_in_flight = false;
        }
        return nullptr;
    };
    
    pthread_create(&thread, &attr, threadFunc, this);
    pthread_detach(thread);
    pthread_attr_destroy(&attr);
}

void CatalogTab::loadCatalogs() {
    auto catalogs = AddonManager::getInstance().getAvailableCatalogs();
    
    menuDataSource = new CatalogMenuDataSource(catalogs, [this](const std::string& type, const std::string& id) {
        // Fetch items for this catalog
        struct ThreadData {
            std::string type;
            std::string id;
            CatalogTab* view;
            std::shared_ptr<bool> alive;
        };
        
        ThreadData* data = new ThreadData{type, id, this, this->alive};
        
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
        
        auto threadFunc = [](void* arg) -> void* {
            ThreadData* d = static_cast<ThreadData*>(arg);
            AddonManager::getInstance().setActiveCatalog(d->type, d->id);
            if (AddonManager::getInstance().fetchCurrentCatalog()) {
                auto items = AddonManager::getInstance().getCatalog(); // copy
                
                auto alive = d->alive;
                auto view = d->view;
                brls::sync([alive, view, items]() {
                    if (*alive) {
                        view->itemsDataSource = new CatalogDataSource(items);
                        view->items_grid->setDataSource(view->itemsDataSource);
                    }
                });
            }
            delete d;
            return nullptr;
        };
        
        pthread_create(&thread, &attr, threadFunc, data);
        pthread_detach(thread);
        pthread_attr_destroy(&attr);
    });
    
    // Add pagination
    items_grid->onNextPage([this]() {
        if (AddonManager::getInstance().isLoading()) return;
        
        struct ThreadData {
            CatalogTab* view;
            std::shared_ptr<bool> alive;
        };
        
        ThreadData* data = new ThreadData{this, this->alive};
        
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
        
        auto threadFunc = [](void* arg) -> void* {
            ThreadData* d = static_cast<ThreadData*>(arg);
            if (AddonManager::getInstance().fetchCurrentCatalog(true)) {
                auto items = AddonManager::getInstance().getCatalog(); // copy
                
                auto alive = d->alive;
                auto view = d->view;
                brls::sync([alive, view, items]() {
                    if (*alive) {
                        if (view->itemsDataSource) {
                            view->itemsDataSource->items = items;
                            view->items_grid->notifyDataChanged();
                        }
                    }
                });
            }
            delete d;
            return nullptr;
        };
        
        pthread_create(&thread, &attr, threadFunc, data);
        pthread_detach(thread);
        pthread_attr_destroy(&attr);
    });

    categories_grid->setDataSource(menuDataSource);
    
    if (catalogs.size() > 0) {
        menuDataSource->setSelectedIndex(categories_grid, 0);
    }
}

brls::View* CatalogTab::create() {
    return new CatalogTab();
}
