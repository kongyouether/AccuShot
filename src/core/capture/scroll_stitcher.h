#pragma once

#include <QImage>

class ScrollStitcher
{
public:
    enum class Direction { Vertical, Horizontal };
    ScrollStitcher() = default;

    // 重置并初始化长截图画布
    void reset(const QImage& initialFrame, Direction direction = Direction::Vertical);

    // 沿所选轴输出有符号位移；只有画布扩展时返回 true，回滚不会重复拼接。
    bool appendFrame(const QImage& newFrame, int& outDelta);

    // 获取当前缝合后的完整长截图
    QImage currentStitchedImage() const;

    // 当前已拼接总高度
    int currentTotalHeight() const;
    int currentTotalWidth() const { return m_stitchedImage.width(); }
    Direction direction() const { return m_direction; }

    // 当前已捕获帧数
    int frameCount() const { return m_frameCount; }

    // 是否已初始化
    bool isValid() const { return !m_stitchedImage.isNull(); }

private:
    bool computeOffset(const QImage& previous, const QImage& current, int& outDelta) const;

    QImage m_stitchedImage; // 累积拼接的大图
    QImage m_lastFrame;     // 上一帧参考视口画面
    int m_frameCount = 0;
    Direction m_direction = Direction::Vertical;
    int m_viewportStart = 0;
};
