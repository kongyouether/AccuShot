#include "toolbar_icons.h"
#include "../../core/theme_manager.h"

#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <utility>

namespace {

// Render the approved vectors at the requested size and device pixel ratio.
// Checked buttons need an On pixmap: Selected mode alone does not cover them.
class VectorIconEngine final : public QIconEngine {
public:
    VectorIconEngine(QByteArray svg, QColor color, bool branded = false)
        : m_svg(std::move(svg)), m_color(std::move(color)), m_branded(branded) {}

    QIconEngine* clone() const override { return new VectorIconEngine(*this); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        QByteArray svg = m_svg;
        if (!m_branded) {
            const auto palette = ThemeManager::instance().palette();
            QColor color = m_color;
            if (mode == QIcon::Disabled) color = palette.disabled;
            else if (state == QIcon::On || mode == QIcon::Selected) color = palette.selectedText;
            svg.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
            svg.replace("var(--surface)", palette.surface.name(QColor::HexRgb).toUtf8());
        }
        QSvgRenderer renderer(svg);
        renderer.render(painter, QRectF(rect));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap result(size * scale);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

private:
    QByteArray m_svg;
    QColor m_color;
    bool m_branded;
};

QByteArray resourceSvg(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QString actionIconName(ToolAction action) {
    switch (action) {
    case ToolAction::Rect: return "rect";
    case ToolAction::Arrow: return "arrow";
    case ToolAction::Pencil: return "pencil";
    case ToolAction::Text: return "text";
    case ToolAction::Mosaic: return "mosaic";
    case ToolAction::CharEdit: return "char";
    case ToolAction::Ocr: return "ocr";
    case ToolAction::Markdown: return "markdown";
    case ToolAction::FormulaOcr: return "formula";
    case ToolAction::ScrollCapture: return "scrollv";
    case ToolAction::GifRecord: return "gif";
    case ToolAction::Undo: return "undo";
    case ToolAction::Pin: return "pin";
    case ToolAction::Save: return "save";
    case ToolAction::Cancel: return "close";
    case ToolAction::Confirm: return "confirm";
    case ToolAction::Translate: return "translate";
    case ToolAction::ScreenTranslate: return "screen";
    case ToolAction::CopyText: return "copytext";
    }
    return {};
}

} // namespace

QIcon uiIcon(const QString& name, const QColor& color) {
    const QByteArray svg = resourceSvg(":/icons/ui/" + name + ".svg");
    return svg.isEmpty() ? QIcon() : QIcon(new VectorIconEngine(svg, color));
}

QIcon appIcon() {
    // Windows shell, window and tray all use the same explicit 16–256px frames.
    return QIcon(QStringLiteral(":/icons/app.ico"));
}

QIcon toolbarIcon(ToolAction action, const QColor& color) {
    return uiIcon(actionIconName(action), color);
}

QIcon toolbarMoreIcon(const QColor& color) { return uiIcon("more", color); }

QIcon toolbarGripIcon(const QColor& color) {
    return QIcon(new VectorIconEngine(QByteArrayLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
        "<g fill=\"currentColor\"><circle cx=\"9\" cy=\"6\" r=\"1.1\"/>"
        "<circle cx=\"15\" cy=\"6\" r=\"1.1\"/><circle cx=\"9\" cy=\"12\" r=\"1.1\"/>"
        "<circle cx=\"15\" cy=\"12\" r=\"1.1\"/><circle cx=\"9\" cy=\"18\" r=\"1.1\"/>"
        "<circle cx=\"15\" cy=\"18\" r=\"1.1\"/></g></svg>"), color));
}

QIcon toolbarChevronIcon(const QColor& color) {
    return QIcon(new VectorIconEngine(QByteArrayLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
        "<path d=\"M7 10L12 15L17 10\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>"), color));
}
