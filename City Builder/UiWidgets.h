#pragma once

#include <string>
#include <vector>

struct UiColor {
    float r;
    float g;
    float b;
    float a;

    UiColor();
    UiColor(float red, float green, float blue, float alpha);
};

struct UiRect {
    int x;
    int y;
    int width;
    int height;

    UiRect();
    bool contains(double pointX, double pointY) const;
};

class UiButton {
public:
    UiButton();

    const std::string& id() const;
    const std::string& text() const;
    const std::string& icon() const;
    const std::string& action() const;
    int x() const;
    int y() const;
    int width() const;
    int height() const;
    bool hasExplicitX() const;
    bool hasExplicitY() const;
    bool hasExplicitWidth() const;
    bool hasExplicitHeight() const;
    const UiColor& color() const;
    const UiColor& activeColor() const;

    void setDefinition(
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
        const std::string& icon = std::string());

private:
    std::string id_;
    std::string text_;
    std::string icon_;
    std::string action_;
    int x_;
    int y_;
    int width_;
    int height_;
    bool hasExplicitX_;
    bool hasExplicitY_;
    bool hasExplicitWidth_;
    bool hasExplicitHeight_;
    UiColor color_;
    UiColor activeColor_;
};

struct UiResolvedButton {
    std::string menuId;
    std::string buttonId;
    std::string text;
    std::string icon;
    std::string action;
    UiRect rect;
    UiColor color;
    bool isActive;

    UiResolvedButton();
};

enum class UiAnchor {
    TopLeft,
    BottomLeft,
    BottomRight,
    Center
};

enum class UiFlow {
    Down,
    Up
};

enum class UiMenuStackMode {
    Away,
    Centered
};

enum class UiMenuStackDirection {
    Up,
    Right,
    Down,
    Left
};

class UiMenu {
public:
    UiMenu();

    const std::string& id() const;
    const std::string& parentMenuId() const;
    const std::string& parentButtonId() const;
    bool visible() const;
    void setVisible(bool visible);
    const UiColor& backgroundColor() const;
    const std::vector<UiButton>& buttons() const;

    void setDefinition(
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
        const UiColor& backgroundColor);
    void setDefinition(
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
        const UiColor& backgroundColor);
    void addButton(const UiButton& button);
    void removeButtonsWithActionPrefix(const std::string& actionPrefix);
    UiRect resolvedRect(int framebufferWidth, int framebufferHeight) const;
    UiRect resolvedChildRect(const UiRect& parentRect, int framebufferWidth, int framebufferHeight) const;
    void resolveButtonsInRect(const UiRect& menuRect, const std::vector<std::string>& activeActions, std::vector<UiResolvedButton>& resolvedButtons) const;
    void resolveButtons(int framebufferWidth, int framebufferHeight, const std::string& activeAction, std::vector<UiResolvedButton>& resolvedButtons) const;
    void resolveButtons(int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions, std::vector<UiResolvedButton>& resolvedButtons) const;

private:
    int automaticHeight() const;

    std::string id_;
    int x_;
    int y_;
    int bottom_;
    int width_;
    int height_;
    int buttonWidth_;
    int buttonHeight_;
    int spacing_;
    UiAnchor anchor_;
    UiFlow flow_;
    UiMenuStackMode stackMode_;
    UiMenuStackDirection stackDirection_;
    std::string parentMenuId_;
    std::string parentButtonId_;
    bool visible_;
    UiColor backgroundColor_;
    std::vector<UiButton> buttons_;
};

class UiLayout {
public:
    UiLayout();

    bool loadFromXmlFile(const std::string& filePath);
    const std::vector<UiMenu>& menus() const;
    void setMenuVisible(const std::string& menuId, bool visible);
    void toggleMenu(const std::string& menuId);
    bool menuVisible(const std::string& menuId) const;
    bool addButtonToMenu(const std::string& menuId, const UiButton& button);
    bool removeButtonsWithActionPrefix(const std::string& menuId, const std::string& actionPrefix);
    bool resolveMenuRect(const std::string& menuId, int framebufferWidth, int framebufferHeight, UiRect& rect) const;
    bool hitTestAction(double mouseX, double mouseY, int framebufferWidth, int framebufferHeight, std::string& action) const;
    bool hitTestAction(double mouseX, double mouseY, int framebufferWidth, int framebufferHeight, const std::vector<std::string>& menuIds, std::string& action) const;
    void resolveButtons(int framebufferWidth, int framebufferHeight, const std::string& activeAction, std::vector<UiResolvedButton>& resolvedButtons) const;
    void resolveButtons(int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions, std::vector<UiResolvedButton>& resolvedButtons) const;
    void resolveButtons(int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions, const std::vector<std::string>& menuIds, std::vector<UiResolvedButton>& resolvedButtons) const;

private:
    UiMenu* findMenu(const std::string& menuId);
    const UiMenu* findMenu(const std::string& menuId) const;
    bool resolveMenuRectRecursive(const std::string& menuId, int framebufferWidth, int framebufferHeight, UiRect& rect, int depth) const;
    bool visibleInMenuTree(const UiMenu& menu, int depth) const;

    std::vector<UiMenu> menus_;
};
