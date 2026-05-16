#include "UiWidgets.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
std::string ReadFileToString(const std::string& filePath) {
    std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
    if (!file) {
        return std::string();
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::string AttributeValue(const std::string& tag, const std::string& attributeName, const std::string& fallback) {
    const std::string needle = attributeName + "=\"";
    const std::string::size_type attributeStart = tag.find(needle);
    if (attributeStart == std::string::npos) {
        return fallback;
    }

    const std::string::size_type valueStart = attributeStart + needle.size();
    const std::string::size_type valueEnd = tag.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return fallback;
    }

    return tag.substr(valueStart, valueEnd - valueStart);
}

bool AttributeExists(const std::string& tag, const std::string& attributeName) {
    const std::string needle = attributeName + "=\"";
    return tag.find(needle) != std::string::npos;
}

int AttributeIntValue(const std::string& tag, const std::string& attributeName, int fallback) {
    const std::string value = AttributeValue(tag, attributeName, std::string());
    if (value.empty()) {
        return fallback;
    }

    return std::atoi(value.c_str());
}

float AttributeFloatValue(const std::string& tag, const std::string& attributeName, float fallback) {
    const std::string value = AttributeValue(tag, attributeName, std::string());
    if (value.empty()) {
        return fallback;
    }

    return static_cast<float>(std::atof(value.c_str()));
}

bool AttributeBoolValue(const std::string& tag, const std::string& attributeName, bool fallback) {
    const std::string value = AttributeValue(tag, attributeName, std::string());
    if (value.empty()) {
        return fallback;
    }

    return value == "true" || value == "1" || value == "yes";
}

UiAnchor AttributeAnchorValue(const std::string& tag, const std::string& attributeName, UiAnchor fallback) {
    const std::string value = AttributeValue(tag, attributeName, std::string());
    if (value == "bottomLeft") {
        return UiAnchor::BottomLeft;
    }

    if (value == "topLeft") {
        return UiAnchor::TopLeft;
    }

    return fallback;
}

UiFlow AttributeFlowValue(const std::string& tag, const std::string& attributeName, UiFlow fallback) {
    const std::string value = AttributeValue(tag, attributeName, std::string());
    if (value == "up") {
        return UiFlow::Up;
    }

    if (value == "down") {
        return UiFlow::Down;
    }

    return fallback;
}

UiColor AttributeColorValue(const std::string& tag, const std::string& prefix, const UiColor& fallback) {
    return UiColor(
        AttributeFloatValue(tag, prefix + "R", fallback.r),
        AttributeFloatValue(tag, prefix + "G", fallback.g),
        AttributeFloatValue(tag, prefix + "B", fallback.b),
        AttributeFloatValue(tag, prefix + "A", fallback.a));
}
}

UiColor::UiColor()
    : r(0.0f),
      g(0.0f),
      b(0.0f),
      a(0.0f) {
}

UiColor::UiColor(float red, float green, float blue, float alpha)
    : r(red),
      g(green),
      b(blue),
      a(alpha) {
}

UiRect::UiRect()
    : x(0),
      y(0),
      width(0),
      height(0) {
}

bool UiRect::contains(double pointX, double pointY) const {
    return pointX >= static_cast<double>(x) &&
        pointY >= static_cast<double>(y) &&
        pointX < static_cast<double>(x + width) &&
        pointY < static_cast<double>(y + height);
}

UiButton::UiButton()
    : x_(0),
      y_(0),
      width_(96),
      height_(32),
      hasExplicitX_(false),
      hasExplicitY_(false),
      hasExplicitWidth_(false),
      hasExplicitHeight_(false),
      color_(UiColor(0.15f, 0.20f, 0.22f, 0.88f)),
      activeColor_(UiColor(0.25f, 0.42f, 0.33f, 0.96f)) {
}

const std::string& UiButton::id() const {
    return id_;
}

const std::string& UiButton::text() const {
    return text_;
}

const std::string& UiButton::action() const {
    return action_;
}

int UiButton::x() const {
    return x_;
}

int UiButton::y() const {
    return y_;
}

int UiButton::width() const {
    return width_;
}

int UiButton::height() const {
    return height_;
}

bool UiButton::hasExplicitX() const {
    return hasExplicitX_;
}

bool UiButton::hasExplicitY() const {
    return hasExplicitY_;
}

bool UiButton::hasExplicitWidth() const {
    return hasExplicitWidth_;
}

bool UiButton::hasExplicitHeight() const {
    return hasExplicitHeight_;
}

const UiColor& UiButton::color() const {
    return color_;
}

const UiColor& UiButton::activeColor() const {
    return activeColor_;
}

void UiButton::setDefinition(
    const std::string& id,
    const std::string& text,
    const std::string& action,
    int x,
    int y,
    int width,
    int height,
    bool hasExplicitX,
    bool hasExplicitY,
    bool hasExplicitWidth,
    bool hasExplicitHeight,
    const UiColor& color,
    const UiColor& activeColor) {
    id_ = id;
    text_ = text;
    action_ = action;
    x_ = x;
    y_ = y;
    width_ = width;
    height_ = height;
    hasExplicitX_ = hasExplicitX;
    hasExplicitY_ = hasExplicitY;
    hasExplicitWidth_ = hasExplicitWidth;
    hasExplicitHeight_ = hasExplicitHeight;
    color_ = color;
    activeColor_ = activeColor;
}

UiResolvedButton::UiResolvedButton()
    : isActive(false) {
}

UiMenu::UiMenu()
    : x_(16),
      y_(16),
      bottom_(16),
      width_(128),
      height_(0),
      buttonWidth_(128),
      buttonHeight_(36),
      spacing_(8),
      anchor_(UiAnchor::TopLeft),
      flow_(UiFlow::Down),
      visible_(true),
      backgroundColor_(UiColor(0.0f, 0.0f, 0.0f, 0.0f)) {
}

const std::string& UiMenu::id() const {
    return id_;
}

bool UiMenu::visible() const {
    return visible_;
}

void UiMenu::setVisible(bool visible) {
    visible_ = visible;
}

const UiColor& UiMenu::backgroundColor() const {
    return backgroundColor_;
}

const std::vector<UiButton>& UiMenu::buttons() const {
    return buttons_;
}

void UiMenu::setDefinition(
    const std::string& id,
    int x,
    int y,
    int bottom,
    int width,
    int height,
    int buttonWidth,
    int buttonHeight,
    int spacing,
    UiAnchor anchor,
    UiFlow flow,
    bool visible,
    const UiColor& backgroundColor) {
    id_ = id;
    x_ = x;
    y_ = y;
    bottom_ = bottom;
    width_ = width;
    height_ = height;
    buttonWidth_ = buttonWidth;
    buttonHeight_ = buttonHeight;
    spacing_ = spacing;
    anchor_ = anchor;
    flow_ = flow;
    visible_ = visible;
    backgroundColor_ = backgroundColor;
    buttons_.clear();
}

void UiMenu::addButton(const UiButton& button) {
    buttons_.push_back(button);
}

UiRect UiMenu::resolvedRect(int framebufferWidth, int framebufferHeight) const {
    (void)framebufferWidth;
    UiRect rect;
    rect.x = x_;
    rect.width = width_;
    rect.height = height_ > 0 ? height_ : automaticHeight();
    rect.y = anchor_ == UiAnchor::BottomLeft ? std::max(0, framebufferHeight - bottom_ - rect.height) : y_;
    return rect;
}

void UiMenu::resolveButtons(int framebufferWidth, int framebufferHeight, const std::string& activeAction, std::vector<UiResolvedButton>& resolvedButtons) const {
    if (!visible_) {
        return;
    }

    const UiRect menuRect = resolvedRect(framebufferWidth, framebufferHeight);
    int flowCursor = flow_ == UiFlow::Up ? menuRect.height : 0;

    std::size_t buttonIndex = 0;
    for (; buttonIndex < buttons_.size(); ++buttonIndex) {
        const UiButton& button = buttons_[buttonIndex];
        const int buttonWidth = button.hasExplicitWidth() ? button.width() : buttonWidth_;
        const int buttonHeight = button.hasExplicitHeight() ? button.height() : buttonHeight_;

        UiResolvedButton resolvedButton;
        resolvedButton.menuId = id_;
        resolvedButton.buttonId = button.id();
        resolvedButton.text = button.text();
        resolvedButton.action = button.action();
        resolvedButton.isActive = !activeAction.empty() && button.action() == activeAction;
        resolvedButton.color = resolvedButton.isActive ? button.activeColor() : button.color();
        resolvedButton.rect.x = menuRect.x + (button.hasExplicitX() ? button.x() : 0);
        resolvedButton.rect.width = buttonWidth;
        resolvedButton.rect.height = buttonHeight;

        if (button.hasExplicitY()) {
            resolvedButton.rect.y = menuRect.y + button.y();
        } else if (flow_ == UiFlow::Up) {
            flowCursor -= buttonHeight;
            resolvedButton.rect.y = menuRect.y + flowCursor;
            flowCursor -= spacing_;
        } else {
            resolvedButton.rect.y = menuRect.y + flowCursor;
            flowCursor += buttonHeight + spacing_;
        }

        resolvedButtons.push_back(resolvedButton);
    }
}

int UiMenu::automaticHeight() const {
    if (buttons_.empty()) {
        return 0;
    }

    return static_cast<int>(buttons_.size()) * buttonHeight_ + static_cast<int>(buttons_.size() - 1u) * spacing_;
}

UiLayout::UiLayout() {
    setFallbackDefinition();
}

bool UiLayout::loadFromXmlFile(const std::string& filePath) {
    const std::string xml = ReadFileToString(filePath);
    if (xml.empty()) {
        setFallbackDefinition();
        return false;
    }

    std::vector<UiMenu> loadedMenus;
    std::string::size_type searchStart = 0u;
    while (true) {
        const std::string::size_type menuStart = xml.find("<menu", searchStart);
        if (menuStart == std::string::npos) {
            break;
        }

        const std::string::size_type menuTagEnd = xml.find('>', menuStart);
        if (menuTagEnd == std::string::npos) {
            break;
        }

        const std::string menuTag = xml.substr(menuStart, menuTagEnd - menuStart + 1u);
        const std::string::size_type menuClose = xml.find("</menu>", menuTagEnd);
        if (menuClose == std::string::npos) {
            break;
        }

        const int defaultWidth = AttributeIntValue(menuTag, "width", 128);
        const int defaultButtonWidth = AttributeIntValue(menuTag, "buttonWidth", defaultWidth);
        const int defaultButtonHeight = AttributeIntValue(menuTag, "buttonHeight", 36);
        UiMenu menu;
        menu.setDefinition(
            AttributeValue(menuTag, "id", "menu"),
            AttributeIntValue(menuTag, "x", 16),
            AttributeIntValue(menuTag, "y", 16),
            AttributeIntValue(menuTag, "bottom", 16),
            defaultWidth,
            AttributeIntValue(menuTag, "height", 0),
            defaultButtonWidth,
            defaultButtonHeight,
            AttributeIntValue(menuTag, "spacing", 8),
            AttributeAnchorValue(menuTag, "anchor", UiAnchor::TopLeft),
            AttributeFlowValue(menuTag, "flow", UiFlow::Down),
            AttributeBoolValue(menuTag, "visible", true),
            AttributeColorValue(menuTag, "background", UiColor(0.0f, 0.0f, 0.0f, 0.0f)));

        std::string::size_type buttonSearchStart = menuTagEnd + 1u;
        while (buttonSearchStart < menuClose) {
            const std::string::size_type buttonStart = xml.find("<button", buttonSearchStart);
            if (buttonStart == std::string::npos || buttonStart >= menuClose) {
                break;
            }

            const std::string::size_type buttonEnd = xml.find('>', buttonStart);
            if (buttonEnd == std::string::npos || buttonEnd > menuClose) {
                break;
            }

            const std::string buttonTag = xml.substr(buttonStart, buttonEnd - buttonStart + 1u);
            UiButton button;
            const UiColor buttonColor = AttributeColorValue(buttonTag, "color", UiColor(0.15f, 0.20f, 0.22f, 0.88f));
            button.setDefinition(
                AttributeValue(buttonTag, "id", "button"),
                AttributeValue(buttonTag, "text", AttributeValue(buttonTag, "id", "button")),
                AttributeValue(buttonTag, "action", std::string()),
                AttributeIntValue(buttonTag, "x", 0),
                AttributeIntValue(buttonTag, "y", 0),
                AttributeIntValue(buttonTag, "width", defaultButtonWidth),
                AttributeIntValue(buttonTag, "height", defaultButtonHeight),
                AttributeExists(buttonTag, "x"),
                AttributeExists(buttonTag, "y"),
                AttributeExists(buttonTag, "width"),
                AttributeExists(buttonTag, "height"),
                buttonColor,
                AttributeColorValue(buttonTag, "active", UiColor(0.25f, 0.42f, 0.33f, 0.96f)));
            menu.addButton(button);
            buttonSearchStart = buttonEnd + 1u;
        }

        loadedMenus.push_back(menu);
        searchStart = menuClose + 7u;
    }

    if (loadedMenus.empty()) {
        setFallbackDefinition();
        return false;
    }

    menus_ = loadedMenus;
    return true;
}

void UiLayout::setFallbackDefinition() {
    menus_.clear();

    UiMenu sideMenu;
    sideMenu.setDefinition("side_tools", 16, 16, 72, 132, 0, 132, 38, 8, UiAnchor::BottomLeft, UiFlow::Down, true, UiColor(0.0f, 0.0f, 0.0f, 0.0f));

    const char* buttonIds[] = {"bulldozer", "road", "query", "rci_residential", "rci_industrial"};
    const char* buttonTexts[] = {"Bulldoze", "Road", "Query", "Residence", "Industry"};
    const char* buttonActions[] = {"select_bulldozer", "select_road_street", "select_query", "select_rci_residential", "select_rci_industrial"};
    const UiColor colors[] = {
        UiColor(0.34f, 0.12f, 0.11f, 0.90f),
        UiColor(0.14f, 0.22f, 0.30f, 0.90f),
        UiColor(0.16f, 0.24f, 0.22f, 0.90f),
        UiColor(0.12f, 0.34f, 0.18f, 0.90f),
        UiColor(0.42f, 0.35f, 0.10f, 0.90f)
    };

    for (std::size_t index = 0; index < 5u; ++index) {
        UiButton button;
        button.setDefinition(buttonIds[index], buttonTexts[index], buttonActions[index], 0, 0, 132, 38, false, false, false, false, colors[index], UiColor(0.52f, 0.66f, 0.47f, 0.96f));
        sideMenu.addButton(button);
    }
    menus_.push_back(sideMenu);

    UiMenu toggleMenu;
    toggleMenu.setDefinition("menu_toggle", 16, 16, 16, 92, 0, 92, 40, 0, UiAnchor::BottomLeft, UiFlow::Down, true, UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    UiButton toggleButton;
    toggleButton.setDefinition("toggle_tools", "Tools", "toggle_side_menu", 0, 0, 92, 40, false, false, false, false, UiColor(0.08f, 0.12f, 0.13f, 0.94f), UiColor(0.08f, 0.12f, 0.13f, 0.94f));
    toggleMenu.addButton(toggleButton);
    menus_.push_back(toggleMenu);
}

const std::vector<UiMenu>& UiLayout::menus() const {
    return menus_;
}

void UiLayout::setMenuVisible(const std::string& menuId, bool visible) {
    UiMenu* menu = findMenu(menuId);
    if (menu != 0) {
        menu->setVisible(visible);
    }
}

void UiLayout::toggleMenu(const std::string& menuId) {
    UiMenu* menu = findMenu(menuId);
    if (menu != 0) {
        menu->setVisible(!menu->visible());
    }
}

bool UiLayout::menuVisible(const std::string& menuId) const {
    const UiMenu* menu = findMenu(menuId);
    return menu != 0 && menu->visible();
}

bool UiLayout::hitTestAction(double mouseX, double mouseY, int framebufferWidth, int framebufferHeight, std::string& action) const {
    std::vector<UiResolvedButton> resolvedButtons;
    resolveButtons(framebufferWidth, framebufferHeight, std::string(), resolvedButtons);

    std::vector<UiResolvedButton>::const_reverse_iterator buttonIterator = resolvedButtons.rbegin();
    for (; buttonIterator != resolvedButtons.rend(); ++buttonIterator) {
        if (buttonIterator->rect.contains(mouseX, mouseY) && !buttonIterator->action.empty()) {
            action = buttonIterator->action;
            return true;
        }
    }

    return false;
}

void UiLayout::resolveButtons(int framebufferWidth, int framebufferHeight, const std::string& activeAction, std::vector<UiResolvedButton>& resolvedButtons) const {
    resolvedButtons.clear();
    std::size_t menuIndex = 0;
    for (; menuIndex < menus_.size(); ++menuIndex) {
        menus_[menuIndex].resolveButtons(framebufferWidth, framebufferHeight, activeAction, resolvedButtons);
    }
}

UiMenu* UiLayout::findMenu(const std::string& menuId) {
    std::size_t menuIndex = 0;
    for (; menuIndex < menus_.size(); ++menuIndex) {
        if (menus_[menuIndex].id() == menuId) {
            return &menus_[menuIndex];
        }
    }

    return 0;
}

const UiMenu* UiLayout::findMenu(const std::string& menuId) const {
    std::size_t menuIndex = 0;
    for (; menuIndex < menus_.size(); ++menuIndex) {
        if (menus_[menuIndex].id() == menuId) {
            return &menus_[menuIndex];
        }
    }

    return 0;
}
