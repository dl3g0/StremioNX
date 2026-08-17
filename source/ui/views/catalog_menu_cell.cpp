#include "catalog_menu_cell.hpp"
#include <borealis/core/application.hpp>
#include <borealis/core/theme.hpp>

CatalogMenuCell::CatalogMenuCell() {
    this->inflateFromXMLRes("xml/views/catalog_menu_cell.xml");
    auto theme    = brls::Application::getTheme();
    selectedColor = theme.getColor("color/tsvitch");
    fontColor     = theme.getColor("brls/text");
}

void CatalogMenuCell::setTitle(const std::string& title) {
    this->labelTitle->setText(title);
}

void CatalogMenuCell::setSelected(bool selected) {
    this->labelTitle->setTextColor(selected ? selectedColor : fontColor);
}

void CatalogMenuCell::prepareForReuse() {
    this->labelTitle->setText("");
    this->labelTitle->setTextColor(fontColor);
}

void CatalogMenuCell::cacheForReuse() {
}

RecyclingGridItem* CatalogMenuCell::create() {
    return new CatalogMenuCell();
}
