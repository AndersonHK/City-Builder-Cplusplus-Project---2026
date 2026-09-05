#include "UiWidgets.h"

#include "Localization.h"
#include "SimpleXml.h"

#include <algorithm>
#include <stdexcept>

namespace {
UiAnchor AttributeAnchorValue(const std::string& tag, const std::string& attributeName, UiAnchor fallback) {
    const std::string value = XmlAttributeValue(tag, attributeName, std::string());
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
    const std::string value = XmlAttributeValue(tag, attributeName, std::string());
    if (value == "up") {
        return UiFlow::Up;
    }

    if (value == "down") {
        return UiFlow::Down;
    }

    return fallback;
}

UiMenuStackMode AttributeStackModeValue(const std::string& tag, UiMenuStackMode fallback) {
    const std::string value = XmlAttributeValue(tag, "stack", XmlAttributeValue(tag, "stackMode", std::string()));
    if (value == "away" || value == "stacked") {
        return UiMenuStackMode::Away;
    }

    if (value == "centered" || value == "center") {
        return UiMenuStackMode::Centered;
    }

    return fallback;
}

UiMenuStackDirection AttributeStackDirectionValue(const std::string& tag, UiMenuStackDirection fallback) {
    const std::string value = XmlAttributeValue(tag, "direction", XmlAttributeValue(tag, "stackDirection", std::string()));
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
        XmlAttributeFloatValue(tag, prefix + "R", fallback.r),
        XmlAttributeFloatValue(tag, prefix + "G", fallback.g),
        XmlAttributeFloatValue(tag, prefix + "B", fallback.b),
        XmlAttributeFloatValue(tag, prefix + "A", fallback.a));
}

bool ActionIsActive(const std::vector<std::string>& activeActions, const std::string& action) {
    return !action.empty() && std::find(activeActions.begin(), activeActions.end(), action) != activeActions.end();
}

std::string LocalizedButtonText(const std::string& buttonTag, const std::string& buttonId, const LocalizationCatalog* localization) {
    std::string textStringId = XmlAttributeValue(buttonTag, "textStringId", std::string());
    if (textStringId.empty()) {
        textStringId = XmlAttributeValue(buttonTag, "stringId", std::string());
    }

    if (!textStringId.empty()) {
        return localization == 0
            ? XmlAttributeValue(buttonTag, "text", std::string())
            : localization->stringForKey(textStringId, "UI button " + buttonId);
    }

    const bool hasTextAttribute = XmlAttributeExists(buttonTag, "text");
    const std::string literalText = hasTextAttribute ? XmlAttributeValue(buttonTag, "text", std::string()) : buttonId;
    if (localization != 0 && !literalText.empty()) {
        throw std::runtime_error("UI button '" + buttonId + "' has visible text without a textStringId.");
    }

    return literalText;
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
}

bool UiLayout::loadFromXmlFile(const std::string& filePath) {
    return loadFromXmlFile(filePath, 0);
}

bool UiLayout::loadFromXmlFile(const std::string& filePath, const LocalizationCatalog* localization) {
    const std::string xml = XmlReadFileToString(filePath);
    if (xml.empty()) {
        menus_.clear();
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

        const int defaultWidth = XmlAttributeIntValue(menuTag, "width", 128);
        const int defaultButtonWidth = XmlAttributeIntValue(menuTag, "buttonWidth", defaultWidth);
        const int defaultButtonHeight = XmlAttributeIntValue(menuTag, "buttonHeight", 36);
        const UiFlow flow = AttributeFlowValue(menuTag, "flow", UiFlow::Down);
        const UiMenuStackDirection defaultStackDirection = flow == UiFlow::Up ? UiMenuStackDirection::Up : UiMenuStackDirection::Down;
        UiMenu menu;
        menu.setDefinition(
            XmlAttributeValue(menuTag, "id", "menu"),
            XmlAttributeIntValue(menuTag, "x", 16),
            XmlAttributeIntValue(menuTag, "y", 16),
            XmlAttributeIntValue(menuTag, "bottom", 16),
            defaultWidth,
            XmlAttributeIntValue(menuTag, "height", 0),
            defaultButtonWidth,
            defaultButtonHeight,
            XmlAttributeIntValue(menuTag, "spacing", 8),
            AttributeAnchorValue(menuTag, "anchor", UiAnchor::TopLeft),
            flow,
            AttributeStackModeValue(menuTag, UiMenuStackMode::Away),
            AttributeStackDirectionValue(menuTag, defaultStackDirection),
            XmlAttributeValue(menuTag, "parentMenu", XmlAttributeValue(menuTag, "parent", std::string())),
            XmlAttributeValue(menuTag, "parentButton", std::string()),
            XmlAttributeBoolValue(menuTag, "visible", true),
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
            const std::string buttonId = XmlAttributeValue(buttonTag, "id", "button");
            const std::string buttonIcon = XmlAttributeValue(buttonTag, "icon", std::string());
            const UiColor buttonColor = AttributeColorValue(buttonTag, "color", UiColor(0.15f, 0.20f, 0.22f, 0.88f));
            button.setDefinition(
                buttonId,
                LocalizedButtonText(buttonTag, buttonId, localization),
                XmlAttributeValue(buttonTag, "action", std::string()),
                XmlAttributeIntValue(buttonTag, "x", 0),
                XmlAttributeIntValue(buttonTag, "y", 0),
                XmlAttributeIntValue(buttonTag, "width", defaultButtonWidth),
                XmlAttributeIntValue(buttonTag, "height", defaultButtonHeight),
                XmlAttributeExists(buttonTag, "x"),
                XmlAttributeExists(buttonTag, "y"),
                XmlAttributeExists(buttonTag, "width"),
                XmlAttributeExists(buttonTag, "height"),
                buttonColor,
                AttributeColorValue(buttonTag, "active", UiColor(0.25f, 0.42f, 0.33f, 0.96f)),
                buttonIcon);
            menu.addButton(button);
            buttonSearchStart = buttonEnd + 1u;
        }

        loadedMenus.push_back(menu);
        searchStart = menuClose + 7u;
    }

    if (loadedMenus.empty()) {
        menus_.clear();
        return false;
    }

    menus_ = loadedMenus;
    return true;
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
