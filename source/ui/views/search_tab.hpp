#pragma once

#include <memory>
#include <borealis/core/box.hpp>
#include <borealis/core/bind.hpp>
#include "recycling_grid.hpp"
#include "../../core/addon_manager.hpp"

namespace brls {
class Button;
class Label;
}  // namespace brls

class SearchDataSource;

class SearchTab : public brls::Box {
public:
    SearchTab();
    ~SearchTab() override;

    static brls::View* create();

private:
    std::shared_ptr<bool> alive;

    brls::Box* searchBox = nullptr;
    brls::Label* queryLabel = nullptr;
    brls::Label* emptyLabel = nullptr;
    brls::Button* btnMovies = nullptr;
    brls::Button* btnSeries = nullptr;
    brls::Button* btnAll = nullptr;
    RecyclingGrid* grid = nullptr;

    std::string query;
    std::string type = "all";
    bool searching = false;
    SearchDataSource* dataSource = nullptr;

    void openKeyboard();
    void setType(const std::string& t);
    void updateTypeButtons();
    void runSearch();
    void onResults(const std::vector<MetaItem>& items);
};
