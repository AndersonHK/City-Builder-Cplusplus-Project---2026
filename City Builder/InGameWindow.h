#pragma once

#include <cstddef>
#include <string>
#include <vector>

class TextFieldElement {
public:
    TextFieldElement();

    const std::string& id() const;
    int x() const;
    int y() const;
    int width() const;
    int height() const;
    bool hasExplicitPosition() const;
    bool hasExplicitWidth() const;
    const std::string& text() const;

    void setDefinition(const std::string& id, int x, int y, int width, int height, bool hasExplicitPosition = true, bool hasExplicitWidth = true);
    void setLayout(int x, int y, int width);
    void setText(const std::string& text);

private:
    std::string id_;
    int x_;
    int y_;
    int width_;
    int height_;
    bool hasExplicitPosition_;
    bool hasExplicitWidth_;
    std::string text_;
};

class InGameWindow {
public:
    InGameWindow();

    bool loadFromXmlFile(const std::string& filePath);
    void setFallbackDefinition();
    void setVisible(bool visible);
    bool visible() const;
    const std::string& id() const;
    int x() const;
    int y() const;
    int width() const;
    int height() const;
    const std::vector<TextFieldElement>& textFields() const;

    void clearText();
    void setText(const std::string& elementId, const std::string& text);
    void setLineText(std::size_t lineIndex, const std::string& text);
    void updateLayout();

private:
    TextFieldElement* findTextField(const std::string& elementId);

    std::string id_;
    int x_;
    int y_;
    int width_;
    int height_;
    int declaredHeight_;
    int marginLeft_;
    int marginTop_;
    int marginRight_;
    int marginBottom_;
    int elementSpacing_;
    bool hugElements_;
    bool visible_;
    std::vector<TextFieldElement> textFields_;
};
