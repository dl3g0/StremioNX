#include "search_tab.hpp"
#include "search_cell.hpp"
#include <pthread.h>
#include <nanovg.h>
#include <borealis/views/button.hpp>
#include <borealis/views/label.hpp>
#include <borealis/core/application.hpp>
#include "../../core/addon_manager.hpp"
#include "../application.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

using namespace brls;

// Data source for the search results grid. Selection opens the Details screen.
class SearchDataSource : public RecyclingGridDataSource {
public:
    explicit SearchDataSource(std::vector<MetaItem> items) : items(std::move(items)) {}

    size_t getItemCount() override { return items.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        SearchCell* cell = dynamic_cast<SearchCell*>(recycler->getGridItemByIndex(index));
        if (!cell) {
            cell = new SearchCell();
        }
        cell->setMeta(items[index]);
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        if (index < items.size()) {
            brls::Application::pushActivity(new DetailsActivity(items[index]));
        }
    }

    void clearData() override { items.clear(); }

    std::vector<MetaItem> items;
};

SearchTab::SearchTab() : Box(Axis::COLUMN) {
    alive = std::make_shared<bool>(true);

    Theme theme = Application::getTheme();
    NVGcolor tsvitch = theme["color/tsvitch"];
    NVGcolor idleBg = nvgRGBA(0, 0, 0, 0);

    // Top row: search box + type filter
    Box* topRow = new Box(Axis::ROW);
    topRow->setPadding(20, 20, 10, 20);
    topRow->setAlignItems(AlignItems::CENTER);
    topRow->setWidthPercentage(100);

    searchBox = new Box(Axis::ROW);
    searchBox->setFocusable(true);
    searchBox->setCornerRadius(8);
    searchBox->setBackgroundColor(theme["color/grey_3"]);
    searchBox->setGrow(1.0f);
    searchBox->setPadding(14, 20, 14, 20);
    searchBox->setAlignItems(AlignItems::CENTER);

    queryLabel = new Label();
    queryLabel->setText("Buscar películas o series...");
    queryLabel->setFontSize(20);
    queryLabel->setTextColor(theme["brls/text_disabled"]);
    searchBox->addView(queryLabel);

    searchBox->registerClickAction([this](View* view) {
        this->openKeyboard();
        return true;
    });

    topRow->addView(searchBox);

    btnMovies = new Button();
    btnMovies->setText("Películas");
    btnMovies->setMarginLeft(10);
    btnMovies->registerClickAction([this](View* view) {
        this->setType("movie");
        return true;
    });
    topRow->addView(btnMovies);

    btnSeries = new Button();
    btnSeries->setText("Series");
    btnSeries->setMarginLeft(10);
    btnSeries->registerClickAction([this](View* view) {
        this->setType("series");
        return true;
    });
    topRow->addView(btnSeries);

    btnAll = new Button();
    btnAll->setText("Todo");
    btnAll->setMarginLeft(10);
    btnAll->registerClickAction([this](View* view) {
        this->setType("all");
        return true;
    });
    topRow->addView(btnAll);

    this->addView(topRow);

    // Empty/status label
    emptyLabel = new Label();
    emptyLabel->setText("Ingresá un término para buscar películas o series.");
    emptyLabel->setFontSize(20);
    emptyLabel->setMarginTop(40);
    emptyLabel->setHorizontalAlign(HorizontalAlign::CENTER);
    this->addView(emptyLabel);

    // Results grid
    grid = new RecyclingGrid();
    grid->setPaddingLeft(20);
    grid->setPaddingRight(20);
    grid->setPaddingTop(20);
    grid->setGrow(1.0f);
    grid->spanCount = 4;
    grid->estimatedRowHeight = 390;
    grid->estimatedRowSpace = 15;
    grid->preFetchLine = 2;
    grid->registerCell("SearchCell", []() { return SearchCell::create(); });
    grid->setDataSource(new SearchDataSource({}));
    this->addView(grid);

    (void)tsvitch;
    (void)idleBg;
    setType("all");
}

SearchTab::~SearchTab() {
    *alive = false;
}

brls::View* SearchTab::create() {
    return new SearchTab();
}

void SearchTab::openKeyboard() {
    std::string result;
#ifdef __SWITCH__
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return;
    // swkbdCreate only allocates the config; the default preset must be applied
    // or swkbdShow crashes the applet (as borealis's own Switch IME does).
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, "Buscar en StremioNX");
    if (!query.empty()) swkbdConfigSetInitialText(&kbd, query.c_str());
    char out[256] = {0};
    rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return;
    result = std::string(out);
#else
    result = query; // Desktop fallback: keep the current query
#endif

    size_t start = result.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return;
    size_t end = result.find_last_not_of(" \t\r\n");
    result = result.substr(start, end - start + 1);
    if (result.empty()) return;

    query = result;
    queryLabel->setText(result);
    queryLabel->setTextColor(brls::Application::getTheme()["brls/text"]);
    runSearch();
}

void SearchTab::setType(const std::string& t) {
    if (type == t) return;
    type = t;
    updateTypeButtons();
    if (!query.empty()) runSearch();
}

void SearchTab::updateTypeButtons() {
    NVGcolor active = brls::Application::getTheme()["color/tsvitch"];
    NVGcolor idle = nvgRGBA(0, 0, 0, 0);
    auto apply = [active, idle](Button* btn, bool isActive) {
        btn->setBackgroundColor(isActive ? active : idle);
    };
    apply(btnMovies, type == "movie");
    apply(btnSeries, type == "series");
    apply(btnAll, type == "all");
}

void SearchTab::runSearch() {
    if (query.empty() || searching) return;
    searching = true;
    emptyLabel->setVisibility(brls::Visibility::GONE);
    grid->showSkeleton(12);

    struct SearchTask {
        SearchTab* tab;
        std::string query;
        std::string type;
        std::shared_ptr<bool> alive;
    };

    SearchTask* task = new SearchTask{this, query, type, alive};

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

    auto threadFunc = [](void* arg) -> void* {
        SearchTask* t = static_cast<SearchTask*>(arg);
        AddonManager::getInstance().searchCatalog(t->type, t->query, [t](const std::vector<MetaItem>& items) {
            // brls::sync() is asynchronous in this Borealis fork: it only
            // queues the function and runs it later on the UI thread. The task
            // must NOT be freed before that, or the queued lambda reads freed
            // memory (use-after-free crash). Copy out the fields we need and
            // keep the shared alive-flag alive, then free the task.
            SearchTab* tab = t->tab;
            std::shared_ptr<bool> alive = t->alive;
            delete t;
            brls::sync([tab, alive, items]() {
                if (*alive) {
                    tab->onResults(items);
                }
            });
        });
        return nullptr;
    };

    pthread_create(&thread, &attr, threadFunc, task);
    pthread_detach(thread);
    pthread_attr_destroy(&attr);
}

void SearchTab::onResults(const std::vector<MetaItem>& items) {
    searching = false;

    dataSource = new SearchDataSource(items);
    grid->setDataSource(dataSource);

    if (items.empty()) {
        emptyLabel->setText("Sin resultados para \"" + query + "\"");
        emptyLabel->setVisibility(brls::Visibility::VISIBLE);
    } else {
        emptyLabel->setVisibility(brls::Visibility::GONE);
    }
}
