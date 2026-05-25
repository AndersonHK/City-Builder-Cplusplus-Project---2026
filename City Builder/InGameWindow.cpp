#include "InGameWindow.h"

#include "SimpleXml.h"

#include <algorithm>
#include <sstream>

TextFieldElement::TextFieldElement()
    : x_(0),
      y_(0),
      width_(100),
      height_(16),
      hasExplicitPosition_(true),
      hasExplicitWidth_(true) {
}

const std::string& TextFieldElement::id() const {
    return id_;
}

int TextFieldElement::x() const {
    return x_;
}

int TextFieldElement::y() const {
    return y_;
}

int TextFieldElement::width() const {
    return width_;
}

int TextFieldElement::height() const {
    return height_;
}

bool TextFieldElement::hasExplicitPosition() const {
    return hasExplicitPosition_;
}

bool TextFieldElement::hasExplicitWidth() const {
    return hasExplicitWidth_;
}

const std::string& TextFieldElement::text() const {
    return text_;
}

void TextFieldElement::setDefinition(const std::string& id, int x, int y, int width, int height, bool hasExplicitPosition, bool hasExplicitWidth) {
    id_ = id;
    x_ = x;
    y_ = y;
    width_ = width;
    height_ = height;
    hasExplicitPosition_ = hasExplicitPosition;
    hasExplicitWidth_ = hasExplicitWidth;
}

void TextFieldElement::setLayout(int x, int y, int width) {
    if (!hasExplicitPosition_) {
        x_ = x;
        y_ = y;
    }
    if (!hasExplicitWidth_) {
        width_ = width;
    }
}

void TextFieldElement::setText(const std::string& text) {
    text_ = text;
}

InGameWindow::InGameWindow()
    : x_(24),
      y_(24),
      width_(440),
      height_(220),
      declaredHeight_(220),
      marginLeft_(16),
      marginTop_(16),
      marginRight_(16),
      marginBottom_(16),
      elementSpacing_(4),
      hugElements_(false),
      visible_(false) {
}

bool InGameWindow::loadFromXmlFile(const std::string& filePath) {
    const std::string xml = XmlReadFileToString(filePath);
    if (xml.empty()) {
        textFields_.clear();
        return false;
    }

    const std::string::size_type windowStart = xml.find("<window");
    if (windowStart == std::string::npos) {
        textFields_.clear();
        return false;
    }

    const std::string::size_type windowEnd = xml.find('>', windowStart);
    if (windowEnd == std::string::npos) {
        textFields_.clear();
        return false;
    }

    const std::string windowTag = xml.substr(windowStart, windowEnd - windowStart + 1u);
    id_ = XmlAttributeValue(windowTag, "id", "window");
    x_ = XmlAttributeIntValue(windowTag, "x", 24);
    y_ = XmlAttributeIntValue(windowTag, "y", 24);
    width_ = XmlAttributeIntValue(windowTag, "width", 440);
    declaredHeight_ = XmlAttributeIntValue(windowTag, "height", 220);
    height_ = declaredHeight_;
    const int uniformMargin = XmlAttributeIntValue(windowTag, "margin", 16);
    marginLeft_ = XmlAttributeIntValue(windowTag, "marginLeft", uniformMargin);
    marginTop_ = XmlAttributeIntValue(windowTag, "marginTop", uniformMargin);
    marginRight_ = XmlAttributeIntValue(windowTag, "marginRight", uniformMargin);
    marginBottom_ = XmlAttributeIntValue(windowTag, "marginBottom", uniformMargin);
    elementSpacing_ = XmlAttributeIntValue(windowTag, "spacing", 4);
    hugElements_ = XmlAttributeBoolValue(windowTag, "hugElements", false);
    textFields_.clear();

    std::string::size_type searchStart = windowEnd + 1u;
    while (true) {
        const std::string::size_type textFieldStart = xml.find("<textField", searchStart);
        if (textFieldStart == std::string::npos) {
            break;
        }

        const std::string::size_type textFieldEnd = xml.find('>', textFieldStart);
        if (textFieldEnd == std::string::npos) {
            break;
        }

        const std::string textFieldTag = xml.substr(textFieldStart, textFieldEnd - textFieldStart + 1u);
        const bool hasExplicitPosition = XmlAttributeExists(textFieldTag, "x") && XmlAttributeExists(textFieldTag, "y");
        const bool hasExplicitWidth = XmlAttributeExists(textFieldTag, "width");
        TextFieldElement element;
        element.setDefinition(
            XmlAttributeValue(textFieldTag, "id", "text"),
            XmlAttributeIntValue(textFieldTag, "x", marginLeft_),
            XmlAttributeIntValue(textFieldTag, "y", marginTop_),
            XmlAttributeIntValue(textFieldTag, "width", width_ - marginLeft_ - marginRight_),
            XmlAttributeIntValue(textFieldTag, "height", 18),
            hasExplicitPosition,
            hasExplicitWidth);
        textFields_.push_back(element);
        searchStart = textFieldEnd + 1u;
    }

    if (textFields_.empty()) {
        textFields_.clear();
        return false;
    }

    updateLayout();
    return true;
}

void InGameWindow::setVisible(bool visible) {
    visible_ = visible;
}

bool InGameWindow::visible() const {
    return visible_;
}

const std::string& InGameWindow::id() const {
    return id_;
}

int InGameWindow::x() const {
    return x_;
}

int InGameWindow::y() const {
    return y_;
}

int InGameWindow::width() const {
    return width_;
}

int InGameWindow::height() const {
    return height_;
}

const std::vector<TextFieldElement>& InGameWindow::textFields() const {
    return textFields_;
}

void InGameWindow::clearText() {
    std::size_t elementIndex = 0;
    for (; elementIndex < textFields_.size(); ++elementIndex) {
        textFields_[elementIndex].setText(std::string());
    }
}

void InGameWindow::setText(const std::string& elementId, const std::string& text) {
    TextFieldElement* element = findTextField(elementId);
    if (element != 0) {
        element->setText(text);
    }
}

void InGameWindow::setLineText(std::size_t lineIndex, const std::string& text) {
    std::ostringstream idBuilder;
    idBuilder << "line" << lineIndex;
    setText(idBuilder.str(), text);
}

void InGameWindow::updateLayout() {
    const int flowWidth = width_ - marginLeft_ - marginRight_;
    const int defaultFieldWidth = flowWidth > 1 ? flowWidth : 1;
    int flowY = marginTop_;
    int contentBottom = marginTop_;
    bool hasVisibleElement = false;

    std::size_t elementIndex = 0;
    for (; elementIndex < textFields_.size(); ++elementIndex) {
        TextFieldElement& element = textFields_[elementIndex];
        if (element.text().empty()) {
            continue;
        }

        const int layoutX = element.hasExplicitPosition() ? element.x() : marginLeft_;
        const int layoutY = element.hasExplicitPosition() ? element.y() : flowY;
        const int layoutWidth = element.hasExplicitWidth() ? element.width() : defaultFieldWidth;
        element.setLayout(layoutX, layoutY, layoutWidth);
        contentBottom = std::max(contentBottom, element.y() + element.height());
        if (!element.hasExplicitPosition()) {
            flowY = element.y() + element.height() + elementSpacing_;
        }
        hasVisibleElement = true;
    }

    if (hugElements_) {
        height_ = hasVisibleElement ? contentBottom + marginBottom_ : marginTop_ + marginBottom_;
    } else {
        height_ = declaredHeight_;
    }
}

TextFieldElement* InGameWindow::findTextField(const std::string& elementId) {
    std::size_t elementIndex = 0;
    for (; elementIndex < textFields_.size(); ++elementIndex) {
        if (textFields_[elementIndex].id() == elementId) {
            return &textFields_[elementIndex];
        }
    }

    return 0;
}
