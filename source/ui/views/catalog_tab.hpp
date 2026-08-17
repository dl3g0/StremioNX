#pragma once

#include <borealis/core/box.hpp>
#include <borealis/core/bind.hpp>
#include <memory>
#include <mutex>
#include "recycling_grid.hpp"
#include "../../core/addon_manager.hpp"

class CatalogDataSource;
class CatalogMenuDataSource;

class CatalogTab : public brls::Box {
public:
    CatalogTab();
    ~CatalogTab() override;

    static brls::View* create();

private:
    BRLS_BIND(RecyclingGrid, categories_grid, "catalog/categories_grid");
    BRLS_BIND(RecyclingGrid, items_grid, "catalog/items_grid");

    std::shared_ptr<bool> alive;
    CatalogDataSource* itemsDataSource = nullptr;
    CatalogMenuDataSource* menuDataSource = nullptr;

    AddonManager::CatalogsChangedToken changeToken = 0;

    std::mutex reload_mutex;
    bool reload_in_flight = false;

    void loadCatalogs();
    void reloadCatalogs();
};
