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

    if (value == "bottomRight") {
        return UiAnchor::BottomRight;
    }

    if (value == "topLeft") {
        return UiAnchor::TopLeft;
    }

    if (value == "center") {
        return UiAnchor::Center;
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

UiMenuStackMode AttributeStackModeValue(const std::string& tag, UiMenuStackMode fallback) {
    const std::string value = AttributeValue(tag, "stack", AttributeValue(tag, "stackMode", std::string()));
    if (value == "away" || value == "stacked") {
        return UiMenuStackMode::Away;
    }

    if (value == "centered" || value == "center") {
        return UiMenuStackMode::Centered;
    }

    return fallback;
}

UiMenuStackDirection AttributeStackDirectionValue(const std::string& tag, UiMenuStackDirection fallback) {
    const std::string value = AttributeValue(tag, "direction", AttributeValue(tag, "stackDirection", std::string()));
    if (value == "up") {
        return UiMenuStackDirection::Up;
    }

    if (value == "right") {
        return UiMenuStackDirection::Right;
    }

    if (value == "down") {
        return UiMenuStackDirection::Down;
    }

    if (value == "left") {
        return UiMenuStackDirection::Left;
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

bool ActionIsActive(const std::vector<std::string>& activeActions, const std::string& action) {
    return !action.empty() && std::find(activeActions.begin(), activeActions.end(), action) != activeActions.end();
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

const std::string& UiButton::icon() const {
    return icon_;
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
    const UiColor& activeColor,
    const std::string& icon) {
    id_ = id;
    text_ = text;
    icon_ = icon;
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
      stackMode_(UiMenuStackMode::Away),
      stackDirection_(UiMenuStackDirection::Down),
      visible_(true),
      backgroundColor_(UiColor(0.0f, 0.0f, 0.0f, 0.0f)) {
}

const std::string& UiMenu::id() const {
    return id_;
}

const std::string& UiMenu::parentMenuId() const {
    return parentMenuId_;
}

const std::string& UiMenu::parentButtonId() const {
    return parentButtonId_;
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
    UiMenuStackMode stackMode,
    UiMenuStackDirection stackDirection,
    const std::string& parentMenuId,
    const std::string& parentButtonId,
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
    stackMode_ = stackMode;
    stackDirection_ = stackDirection;
    parentMenuId_ = parentMenuId;
    parentButtonId_ = parentButtonId;
    visible_ = visible;
    backgroundColor_ = backgroundColor;
    buttons_.clear();
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
    setDefinition(
        id,
        x,
        y,
        bottom,
        width,
        height,
        buttonWidth,
        buttonHeight,
        spacing,
        anchor,
        flow,
        UiMenuStackMode::Away,
        flow == UiFlow::Up ? UiMenuStackDirection::Up : UiMenuStackDirection::Down,
        std::string(),
        std::string(),
        visible,
        backgroundColor);
}

void UiMenu::addButton(const UiButton& button) {
    buttons_.push_back(button);
}

void UiMenu::removeButtonsWithActionPrefix(const std::string& actionPrefix) {
    if (actionPrefix.empty()) {
        return;
    }

    buttons_.erase(
        std::remove_if(
            buttons_.begin(),
            buttons_.end(),
            [&actionPrefix](const UiButton& button) {
                return button.action().find(actionPrefix) == 0u;
            }),
        buttons_.end());
}

UiRect UiMenu::resolvedRect(int framebufferWidth, int framebufferHeight) const {
    UiRect rect;
    rect.width = width_;
    rect.height = height_ > 0 ? height_ : automaticHeight();
    if (anchor_ == UiAnchor::Center) {
        rect.x = std::max(0, (framebufferWidth - rect.width) / 2 + x_);
        rect.y = std::max(0, (framebufferHeight - rect.height) / 2 + y_);
    } else if (anchor_ == UiAnchor::BottomRight) {
        rect.x = std::max(0, framebufferWidth - x_ - rect.width);
        rect.y = std::max(0, framebufferHeight - bottom_ - rect.height);
    } else {
        rect.x = x_;
        rect.y = anchor_ == UiAnchor::BottomLeft ? std::max(0, framebufferHeight - bottom_ - rect.height) : y_;
    }
    return rect;
}

UiRect UiMenu::resolvedChildRect(const UiRect& parentRect, int framebufferWidth, int framebufferHeight) const {
    UiRect rect;
    rect.width = width_;
    rect.height = height_ > 0 ? height_ : automaticHeight();

    const int parentCenterX = parentRect.x + (parentRect.width / 2);
    const int parentCenterY = parentRect.y + (parentRect.height / 2);
    if (stackDirection_ == UiMenuStackDirection::Up) {
        rect.x = stackMode_ == UiMenuStackMode::Centered ? parentCenterX - (rect.width / 2) + x_ : parentRect.x + x_;
        rect.y = parentRect.y - y_ - rect.height;
    } else if (stackDirection_ == UiMenuStackDirection::Down) {
        rect.x = stackMode_ == UiMenuStackMode::Centered ? parentCenterX - (rect.width / 2) + x_ : parentRect.x + x_;
        rect.y = parentRect.y + parentRect.height + y_;
    } else if (stackDirection_ == UiMenuStackDirection::Right) {
        rect.x = parentRect.x + parentRect.width + x_;
        rect.y = stackMode_ == UiMenuStackMode::Centered ? parentCenterY - (rect.height / 2) + y_ : parentRect.y + y_;
    } else {
        rect.x = parentRect.x - x_ - rect.width;
        rect.y = stackMode_ == UiMenuStackMode::Centered ? parentCenterY - (rect.height / 2) + y_ : parentRect.y + y_;
    }

    rect.x = std::max(0, rect.x);
    rect.y = std::max(0, rect.y);
    return rect;
}

void UiMenu::resolveButtons(int framebufferWidth, int framebufferHeight, const std::string& activeAction, std::vector<UiResolvedButton>& resolvedButtons) const {
    std::vector<std::string> activeActions;
    if (!activeAction.empty()) {
        activeActions.push_back(activeAction);
    }
    resolveButtons(framebufferWidth, framebufferHeight, activeActions, resolvedButtons);
}

void UiMenu::resolveButtons(int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions, std::vector<UiResolvedButton>& resolvedButtons) const {
    if (!visible_) {
        return;
    }

    const UiRect menuRect = resolvedRect(framebufferWidth, framebufferHeight);
    resolveButtonsInRect(menuRect, activeActions, resolvedButtons);
}

void UiMenu::resolveButtonsInRect(const UiRect& menuRect, const std::vector<std::string>& activeActions, std::vector<UiResolvedButton>& resolvedButtons) const {
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
        resolvedButton.icon = button.icon();
        resolvedButton.action = button.action();
        resolvedButton.isActive = ActionIsActive(activeActions, button.action());
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
        const UiFlow flow = AttributeFlowValue(menuTag, "flow", UiFlow::Down);
        const UiMenuStackDirection defaultStackDirection = flow == UiFlow::Up ? UiMenuStackDirection::Up : UiMenuStackDirection::Down;
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
            flow,
            AttributeStackModeValue(menuTag, UiMenuStackMode::Away),
            AttributeStackDirectionValue(menuTag, defaultStackDirection),
            AttributeValue(menuTag, "parentMenu", AttributeValue(menuTag, "parent", std::string())),
            AttributeValue(menuTag, "parentButton", std::string()),
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
                AttributeColorValue(buttonTag, "active", UiColor(0.25f, 0.42f, 0.33f, 0.96f)),
                AttributeValue(buttonTag, "icon", std::string()));
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

    UiMenu speedMenu;
    speedMenu.setDefinition("date_speed", 16, 48, 16, 140, 28, 32, 28, 4, UiAnchor::TopLeft, UiFlow::Down, true, UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    const char* speedButtonIds[] = {"speed_pause", "speed_play", "speed_fast", "speed_fast_forward"};
    const char* speedButtonIcons[] = {"pause", "play", "fast", "fastForward"};
    const char* speedButtonActions[] = {"set_speed_paused", "set_speed_play", "set_speed_fast", "set_speed_fast_forward"};
    for (std::size_t index = 0; index < 4u; ++index) {
        UiButton button;
        button.setDefinition(speedButtonIds[index], std::string(), speedButtonActions[index], static_cast<int>(index) * 36, 0, 32, 28, true, true, false, false, UiColor(0.08f, 0.12f, 0.13f, 0.94f), UiColor(0.22f, 0.43f, 0.35f, 0.96f), speedButtonIcons[index]);
        speedMenu.addButton(button);
    }
    menus_.push_back(speedMenu);

    UiMenu sideMenu;
    sideMenu.setDefinition("side_tools", 0, 16, 72, 132, 0, 132, 38, 8, UiAnchor::BottomLeft, UiFlow::Down, UiMenuStackMode::Away, UiMenuStackDirection::Up, "menu_toggle", "toggle_tools", true, UiColor(0.0f, 0.0f, 0.0f, 0.0f));

    const char* buttonIds[] = {"bulldozer", "street", "road", "one_way", "avenue", "query", "rci_menu"};
    const char* buttonTexts[] = {"Bulldoze", "Street", "Road", "One-Way", "Avenue", "Query", "RCI"};
    const char* buttonActions[] = {"select_bulldozer", "select_road_street", "select_road_road", "select_road_one_way", "select_road_avenue", "select_query", "toggle_rci_tool_menu"};
    const UiColor colors[] = {
        UiColor(0.34f, 0.12f, 0.11f, 0.90f),
        UiColor(0.20f, 0.24f, 0.25f, 0.90f),
        UiColor(0.14f, 0.22f, 0.30f, 0.90f),
        UiColor(0.18f, 0.20f, 0.28f, 0.90f),
        UiColor(0.18f, 0.26f, 0.31f, 0.90f),
        UiColor(0.16f, 0.24f, 0.22f, 0.90f),
        UiColor(0.24f, 0.24f, 0.27f, 0.90f)
    };

    for (std::size_t index = 0; index < 7u; ++index) {
        UiButton button;
        button.setDefinition(buttonIds[index], buttonTexts[index], buttonActions[index], 0, 0, 132, 38, false, false, false, false, colors[index], UiColor(0.52f, 0.66f, 0.47f, 0.96f));
        sideMenu.addButton(button);
    }
    menus_.push_back(sideMenu);

    UiMenu rciToolsMenu;
    rciToolsMenu.setDefinition("rci_tools", 16, 0, 16, 132, 0, 132, 38, 8, UiAnchor::BottomLeft, UiFlow::Down, UiMenuStackMode::Centered, UiMenuStackDirection::Right, "side_tools", "rci_menu", false, UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    const char* rciButtonIds[] = {"rci_residential_low", "rci_residential_high", "rci_industrial", "rci_unzone"};
    const char* rciButtonTexts[] = {"Low Res", "High Res", "Industry", "Unzone"};
    const char* rciButtonActions[] = {"select_rci_residential_low", "select_rci_residential_high", "select_rci_industrial", "select_rci_unzone"};
    const UiColor rciColors[] = {
        UiColor(0.32f, 0.66f, 0.34f, 0.90f),
        UiColor(0.10f, 0.36f, 0.18f, 0.90f),
        UiColor(0.42f, 0.35f, 0.10f, 0.90f),
        UiColor(0.24f, 0.24f, 0.27f, 0.90f)
    };
    for (std::size_t index = 0; index < 4u; ++index) {
        UiButton button;
        button.setDefinition(rciButtonIds[index], rciButtonTexts[index], rciButtonActions[index], 0, 0, 132, 38, false, false, false, false, rciColors[index], UiColor(0.52f, 0.66f, 0.47f, 0.96f));
        rciToolsMenu.addButton(button);
    }
    menus_.push_back(rciToolsMenu);

    UiMenu overlayMenu;
    overlayMenu.setDefinition("side_overlays", 0, 16, 72, 132, 0, 132, 38, 8, UiAnchor::BottomRight, UiFlow::Down, UiMenuStackMode::Away, UiMenuStackDirection::Up, "overlay_toggle", "toggle_overlays", true, UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    const char* overlayButtonIds[] = {"overlay_traffic", "overlay_land_value", "overlay_rci", "overlay_rci_desirability_menu"};
    const char* overlayButtonTexts[] = {"Traffic", "Land Value", "RCI", "RCI Desire"};
    const char* overlayButtonActions[] = {"toggle_overlay_traffic", "toggle_overlay_land_value", "toggle_overlay_rci", "toggle_rci_overlay_menu"};
    const UiColor overlayColors[] = {
        UiColor(0.14f, 0.24f, 0.28f, 0.90f),
        UiColor(0.18f, 0.30f, 0.16f, 0.90f),
        UiColor(0.24f, 0.22f, 0.12f, 0.90f),
        UiColor(0.20f, 0.22f, 0.20f, 0.90f)
    };
    for (std::size_t index = 0; index < 4u; ++index) {
        UiButton button;
        button.setDefinition(overlayButtonIds[index], overlayButtonTexts[index], overlayButtonActions[index], 0, 0, 132, 38, false, false, false, false, overlayColors[index], UiColor(0.52f, 0.66f, 0.47f, 0.96f));
        overlayMenu.addButton(button);
    }
    menus_.push_back(overlayMenu);

    UiMenu rciOverlayMenu;
    rciOverlayMenu.setDefinition("rci_desirability_overlays", 16, 0, 16, 132, 0, 132, 38, 8, UiAnchor::BottomRight, UiFlow::Down, UiMenuStackMode::Centered, UiMenuStackDirection::Left, "side_overlays", "overlay_rci_desirability_menu", false, UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    const char* rciOverlayButtonIds[] = {"overlay_desirability_low_wealth_residential", "overlay_desirability_dirty_industry"};
    const char* rciOverlayButtonTexts[] = {"Low Wealth Res", "Dirty Industry"};
    const char* rciOverlayButtonActions[] = {"toggle_overlay_desirability_low_wealth_residential", "toggle_overlay_desirability_dirty_industry"};
    for (std::size_t index = 0; index < 2u; ++index) {
        UiButton button;
        button.setDefinition(rciOverlayButtonIds[index], rciOverlayButtonTexts[index], rciOverlayButtonActions[index], 0, 0, 132, 38, false, false, false, false, UiColor(0.20f, 0.22f, 0.20f, 0.90f), UiColor(0.52f, 0.66f, 0.47f, 0.96f));
        rciOverlayMenu.addButton(button);
    }
    menus_.push_back(rciOverlayMenu);

    UiMenu regionExitMenu;
    regionExitMenu.setDefinition("region_exit", 16, 16, 16, 92, 40, 92, 40, 0, UiAnchor::TopLeft, UiFlow::Down, true, UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    UiButton regionExitButton;
    regionExitButton.setDefinition("region_exit_game", "Exit", "open_exit_confirm", 0, 0, 92, 40, true, true, true, true, UiColor(0.08f, 0.12f, 0.13f, 0.94f), UiColor(0.08f, 0.12f, 0.13f, 0.94f));
    regionExitMenu.addButton(regionExitButton);
    menus_.push_back(regionExitMenu);

    UiMenu escapeMenu;
    escapeMenu.setDefinition("escape_menu", 0, 0, 16, 220, 56, 180, 40, 0, UiAnchor::Center, UiFlow::Down, false, UiColor(0.035f, 0.047f, 0.058f, 0.94f));
    UiButton exitButton;
    exitButton.setDefinition("exit_game", "Exit", "open_exit_confirm", 20, 8, 180, 40, true, true, true, true, UiColor(0.22f, 0.10f, 0.10f, 0.94f), UiColor(0.46f, 0.17f, 0.14f, 0.96f));
    escapeMenu.addButton(exitButton);
    menus_.push_back(escapeMenu);

    UiMenu exitDialog;
    exitDialog.setDefinition("exit_confirm_dialog", 0, 0, 16, 340, 116, 132, 36, 8, UiAnchor::Center, UiFlow::Down, false, UiColor(0.035f, 0.047f, 0.058f, 0.96f));
    UiButton promptButton;
    promptButton.setDefinition("exit_prompt", "Save before exiting?", std::string(), 18, 14, 304, 34, true, true, true, true, UiColor(0.035f, 0.047f, 0.058f, 0.0f), UiColor(0.035f, 0.047f, 0.058f, 0.0f));
    exitDialog.addButton(promptButton);
    UiButton yesButton;
    yesButton.setDefinition("exit_save_yes", "Yes", "exit_save_yes", 34, 66, 122, 36, true, true, true, true, UiColor(0.14f, 0.30f, 0.20f, 0.94f), UiColor(0.22f, 0.48f, 0.32f, 0.96f));
    exitDialog.addButton(yesButton);
    UiButton noButton;
    noButton.setDefinition("exit_save_no", "No", "exit_save_no", 184, 66, 122, 36, true, true, true, true, UiColor(0.30f, 0.12f, 0.11f, 0.94f), UiColor(0.52f, 0.20f, 0.18f, 0.96f));
    exitDialog.addButton(noButton);
    menus_.push_back(exitDialog);

    UiMenu citySwitchDialog;
    citySwitchDialog.setDefinition("city_switch_confirm_dialog", 0, 0, 16, 340, 116, 132, 36, 8, UiAnchor::Center, UiFlow::Down, false, UiColor(0.035f, 0.047f, 0.058f, 0.96f));
    UiButton citySwitchPromptButton;
    citySwitchPromptButton.setDefinition("city_switch_prompt", "Save city before leaving?", std::string(), 18, 14, 304, 34, true, true, true, true, UiColor(0.035f, 0.047f, 0.058f, 0.0f), UiColor(0.035f, 0.047f, 0.058f, 0.0f));
    citySwitchDialog.addButton(citySwitchPromptButton);
    UiButton citySwitchYesButton;
    citySwitchYesButton.setDefinition("city_switch_save_yes", "Yes", "city_switch_save_yes", 34, 66, 122, 36, true, true, true, true, UiColor(0.14f, 0.30f, 0.20f, 0.94f), UiColor(0.22f, 0.48f, 0.32f, 0.96f));
    citySwitchDialog.addButton(citySwitchYesButton);
    UiButton citySwitchNoButton;
    citySwitchNoButton.setDefinition("city_switch_save_no", "No", "city_switch_save_no", 184, 66, 122, 36, true, true, true, true, UiColor(0.30f, 0.12f, 0.11f, 0.94f), UiColor(0.52f, 0.20f, 0.18f, 0.96f));
    citySwitchDialog.addButton(citySwitchNoButton);
    menus_.push_back(citySwitchDialog);

    UiMenu toggleMenu;
    toggleMenu.setDefinition("menu_toggle", 16, 16, 16, 92, 0, 92, 40, 0, UiAnchor::BottomLeft, UiFlow::Down, true, UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    UiButton toggleButton;
    toggleButton.setDefinition("toggle_tools", "Tools", "toggle_side_menu", 0, 0, 92, 40, false, false, false, false, UiColor(0.08f, 0.12f, 0.13f, 0.94f), UiColor(0.08f, 0.12f, 0.13f, 0.94f));
    toggleMenu.addButton(toggleButton);
    menus_.push_back(toggleMenu);

    UiMenu overlayToggleMenu;
    overlayToggleMenu.setDefinition("overlay_toggle", 16, 16, 16, 132, 0, 132, 40, 0, UiAnchor::BottomRight, UiFlow::Down, true, UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    UiButton overlayToggleButton;
    overlayToggleButton.setDefinition("toggle_overlays", "Overlays", "toggle_overlay_menu", 0, 0, 132, 40, false, false, false, false, UiColor(0.08f, 0.12f, 0.13f, 0.94f), UiColor(0.08f, 0.12f, 0.13f, 0.94f));
    overlayToggleMenu.addButton(overlayToggleButton);
    menus_.push_back(overlayToggleMenu);
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

bool UiLayout::addButtonToMenu(const std::string& menuId, const UiButton& button) {
    UiMenu* menu = findMenu(menuId);
    if (menu == 0) {
        return false;
    }

    menu->addButton(button);
    return true;
}

bool UiLayout::removeButtonsWithActionPrefix(const std::string& menuId, const std::string& actionPrefix) {
    UiMenu* menu = findMenu(menuId);
    if (menu == 0) {
        return false;
    }

    menu->removeButtonsWithActionPrefix(actionPrefix);
    return true;
}

bool UiLayout::resolveMenuRect(const std::string& menuId, int framebufferWidth, int framebufferHeight, UiRect& rect) const {
    return resolveMenuRectRecursive(menuId, framebufferWidth, framebufferHeight, rect, 0);
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

bool UiLayout::hitTestAction(double mouseX, double mouseY, int framebufferWidth, int framebufferHeight, const std::vector<std::string>& menuIds, std::string& action) const {
    std::vector<UiResolvedButton> resolvedButtons;
    std::vector<std::string> activeActions;
    resolveButtons(framebufferWidth, framebufferHeight, activeActions, menuIds, resolvedButtons);

    std::vector<UiResolvedButton>::const_reverse_iterator buttonIterator = resolvedButtons.rbegin();
    for (; buttonIterator != resolvedButtons.rend(); ++buttonIterator) {
        if (buttonIterator->action.empty()) {
            continue;
        }

        if (buttonIterator->rect.contains(mouseX, mouseY)) {
            action = buttonIterator->action;
            return true;
        }
    }

    return false;
}

void UiLayout::resolveButtons(int framebufferWidth, int framebufferHeight, const std::string& activeAction, std::vector<UiResolvedButton>& resolvedButtons) const {
    std::vector<std::string> activeActions;
    if (!activeAction.empty()) {
        activeActions.push_back(activeAction);
    }
    resolveButtons(framebufferWidth, framebufferHeight, activeActions, resolvedButtons);
}

void UiLayout::resolveButtons(int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions, std::vector<UiResolvedButton>& resolvedButtons) const {
    resolvedButtons.clear();
    const std::vector<std::string> menuIds;
    resolveButtons(framebufferWidth, framebufferHeight, activeActions, menuIds, resolvedButtons);
}

void UiLayout::resolveButtons(int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions, const std::vector<std::string>& menuIds, std::vector<UiResolvedButton>& resolvedButtons) const {
    resolvedButtons.clear();
    const bool includeAllMenus = menuIds.empty();
    std::size_t menuIndex = 0;
    for (; menuIndex < menus_.size(); ++menuIndex) {
        const UiMenu& menu = menus_[menuIndex];
        if (!includeAllMenus && std::find(menuIds.begin(), menuIds.end(), menu.id()) == menuIds.end()) {
            continue;
        }

        UiRect menuRect;
        if (!resolveMenuRect(menu.id(), framebufferWidth, framebufferHeight, menuRect)) {
            continue;
        }

        menu.resolveButtonsInRect(menuRect, activeActions, resolvedButtons);
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

bool UiLayout::resolveMenuRectRecursive(const std::string& menuId, int framebufferWidth, int framebufferHeight, UiRect& rect, int depth) const {
    if (depth > static_cast<int>(menus_.size())) {
        return false;
    }

    const UiMenu* menu = findMenu(menuId);
    if (menu == 0 || !visibleInMenuTree(*menu, 0)) {
        return false;
    }

    if (menu->parentMenuId().empty()) {
        rect = menu->resolvedRect(framebufferWidth, framebufferHeight);
        return true;
    }

    UiRect parentRect;
    if (!resolveMenuRectRecursive(menu->parentMenuId(), framebufferWidth, framebufferHeight, parentRect, depth + 1)) {
        return false;
    }

    UiRect anchorRect = parentRect;
    if (!menu->parentButtonId().empty()) {
        const UiMenu* parentMenu = findMenu(menu->parentMenuId());
        if (parentMenu == 0) {
            return false;
        }

        std::vector<std::string> activeActions;
        std::vector<UiResolvedButton> parentButtons;
        parentMenu->resolveButtonsInRect(parentRect, activeActions, parentButtons);
        bool foundParentButton = false;
        std::size_t buttonIndex = 0;
        for (; buttonIndex < parentButtons.size(); ++buttonIndex) {
            if (parentButtons[buttonIndex].buttonId == menu->parentButtonId()) {
                anchorRect = parentButtons[buttonIndex].rect;
                foundParentButton = true;
                break;
            }
        }

        if (!foundParentButton) {
            return false;
        }
    }

    rect = menu->resolvedChildRect(anchorRect, framebufferWidth, framebufferHeight);
    return true;
}

bool UiLayout::visibleInMenuTree(const UiMenu& menu, int depth) const {
    if (depth > static_cast<int>(menus_.size())) {
        return false;
    }
    if (!menu.visible()) {
        return false;
    }
    if (menu.parentMenuId().empty()) {
        return true;
    }

    const UiMenu* parentMenu = findMenu(menu.parentMenuId());
    return parentMenu != 0 && visibleInMenuTree(*parentMenu, depth + 1);
}
