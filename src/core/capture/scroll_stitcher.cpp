#include "scroll_stitcher.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

void ScrollStitcher::reset(const QImage& initialFrame, Direction direction)
{
    m_direction = direction;
    m_stitchedImage = initialFrame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_stitchedImage.setDevicePixelRatio(1.0);
    m_lastFrame = m_stitchedImage;
    m_viewportStart = 0;
    m_frameCount = initialFrame.isNull() ? 0 : 1;
}

QImage ScrollStitcher::currentStitchedImage() const
{
    return m_stitchedImage;
}

int ScrollStitcher::currentTotalHeight() const
{
    return m_stitchedImage.height();
}

bool ScrollStitcher::computeOffset(const QImage& previous, const QImage& current, int& outDelta) const
{
    const bool horizontal = m_direction == Direction::Horizontal;
    const int length = horizontal ? previous.width() : previous.height();
    const int breadth = horizontal ? previous.height() : previous.width();
    if (length < 60 || breadth < 40) return false;

    // 排除与滚动轴平行的边缘（滚动条、阴影），网格采样控制每次抓帧的计算量。
    const int margin = qBound(8, breadth / 20, 24);
    const int crossStep = qMax(1, (breadth - 2 * margin) / 32);
    const auto difference = [&](int delta) {
        const int overlap = length - std::abs(delta);
        const int axisStep = qMax(1, overlap / 32);
        double sum = 0;
        int count = 0;
        for (int axis = 0; axis < overlap; axis += axisStep) {
            const int prevAxis = axis + qMax(delta, 0);
            const int currAxis = axis + qMax(-delta, 0);
            for (int cross = margin; cross < breadth - margin; cross += crossStep) {
                const QRgb a = horizontal
                    ? reinterpret_cast<const QRgb*>(previous.constScanLine(cross))[prevAxis]
                    : reinterpret_cast<const QRgb*>(previous.constScanLine(prevAxis))[cross];
                const QRgb b = horizontal
                    ? reinterpret_cast<const QRgb*>(current.constScanLine(cross))[currAxis]
                    : reinterpret_cast<const QRgb*>(current.constScanLine(currAxis))[cross];
                sum += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b))
                    + std::abs(qBlue(a) - qBlue(b));
                ++count;
            }
        }
        return sum / (count * 3.0);
    };
    const double stationaryError = difference(0);
    if (stationaryError < 3.5) return false;

    double bestError = std::numeric_limits<double>::max();
    int bestDelta = 0;
    const int maxDelta = qMin(length - 30, static_cast<int>(length * 0.85));
    for (int distance = 1; distance <= maxDelta; ++distance) {
        for (int sign : {1, -1}) {
            const double error = difference(distance * sign);
            if (error < bestError) {
                bestError = error;
                bestDelta = distance * sign;
            }
        }
        if (bestError == 0) break;
    }
    if (bestError >= 16.0 || bestError >= stationaryError * 0.70) return false;
    outDelta = bestDelta;
    return true;
}

bool ScrollStitcher::appendFrame(const QImage& newFrame, int& outDelta)
{
    outDelta = 0;
    // 帧尺寸属于抓屏入口契约；不匹配不能参与像素索引或拼接。
    if (!isValid() || newFrame.isNull() || newFrame.size() != m_lastFrame.size()) return false;
    QImage current = newFrame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    current.setDevicePixelRatio(1.0);
    int delta = 0;
    if (!computeOffset(m_lastFrame, current, delta)) return false;

    const bool horizontal = m_direction == Direction::Horizontal;
    const int oldLength = horizontal ? m_stitchedImage.width() : m_stitchedImage.height();
    const int frameLength = horizontal ? current.width() : current.height();
    const int nextStart = m_viewportStart + delta;
    const int prepend = qMax(0, -nextStart);
    const int append = qMax(0, nextStart + frameLength - oldLength);
    const int newLength = oldLength + prepend + append;
    if (newLength > 35000) return false;

    m_lastFrame = current;
    m_viewportStart = nextStart + prepend;
    outDelta = delta;
    if (prepend == 0 && append == 0) return false;

    QImage canvas(horizontal ? newLength : current.width(),
                  horizontal ? current.height() : newLength, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.drawImage(horizontal ? prepend : 0, horizontal ? 0 : prepend, m_stitchedImage);
    if (prepend > 0) {
        const QRect strip(0, 0, horizontal ? prepend : current.width(),
                          horizontal ? current.height() : prepend);
        painter.drawImage(strip, current, strip);
    }
    if (append > 0) {
        const int destination = oldLength + prepend;
        const int source = frameLength - append;
        painter.drawImage(QRect(horizontal ? destination : 0, horizontal ? 0 : destination,
                                horizontal ? append : current.width(), horizontal ? current.height() : append),
                          current, QRect(horizontal ? source : 0, horizontal ? 0 : source,
                                         horizontal ? append : current.width(), horizontal ? current.height() : append));
    }
    painter.end();
    m_stitchedImage = canvas;
    ++m_frameCount;
    return true;
}
