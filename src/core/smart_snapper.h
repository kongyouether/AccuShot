#pragma once

#include <QImage>
#include <QRect>
#include <QPoint>
#include <QRegion>
#include <vector>
#include <QString>
#include <memory>
#include <mutex>
#include <chrono>

class SmartSnapper
{
public:
    enum Source { SourceWindow = 1, SourceElement = 2, SourceContour = 4, SourceDesktop = 8 };
    // UI Automation control IDs; zero means unavailable, including older recordings.
    enum ControlType {
        ControlUnknown = 0,
        ControlButton = 50000, ControlCheckBox = 50002, ControlComboBox = 50003,
        ControlEdit = 50004, ControlHyperlink = 50005, ControlImage = 50006,
        ControlListItem = 50007, ControlList = 50008, ControlMenuItem = 50011,
        ControlRadioButton = 50013, ControlTabItem = 50019, ControlText = 50020,
        ControlTree = 50023, ControlTreeItem = 50024, ControlCustom = 50025,
        ControlGroup = 50026, ControlDataGrid = 50028, ControlDataItem = 50029,
        ControlDocument = 50030, ControlPane = 50033, ControlTable = 50036
    };
    struct Candidate {
        QRect rect;
        int sources = 0;
        int depth = -1;
        quintptr ownerWindow = 0;
        quint64 generation = 0;
        quint64 requestSerial = 0;
        int controlType = ControlUnknown;
    };
    struct CandidateContext {
        quintptr ownerWindow = 0;
        QRect windowRect;
        quint64 generation = 0;
        bool controlsPending = false;
        // A completed same-target value remains usable while its TTL refresh is pending.
        bool controlsReady = false;
        bool snapshotReady = false;
        // Desktop-local area covered by windows above the owner in Z order.
        QRegion occludedRegion;
    };
    SmartSnapper() = default;
    // Pass an empty desktop for standalone image analysis without native ownership.
    void configure(const QString& mode, const QRect& desktop, quintptr excludedWindow);
    ~SmartSnapper() = default;

    // Analyse one immutable snapshot; outputs are logical coordinates. Call off the UI thread.
    void processSnapshot(const QImage& snapshot, qreal dpr = 1.0);

    // Query ranked visual regions (surfaces, outlines, content blocks), in logical coordinates.
    std::vector<QRect> findCandidatesAt(const QPoint& pos) const;
    std::vector<Candidate> findCandidateDetailsAt(const QPoint& pos) const;
    // Reorder a complete Hybrid candidate list; retain geometry and provenance.
    // Shared by live fusion and offline replay of recorded production candidates.
    static void rankCandidates(std::vector<Candidate>& candidates, const QRect& windowRect);
    // Identity and completion status from the most recent candidate query.
    CandidateContext candidateContext() const { return m_context; }

    // Adopt only immutable CV output; preserve the live native/UIA request state.
    void adoptSnapshotRegions(SmartSnapper&& analyzed);

    // 清除分析结果
    void clear();

private:
    friend struct SmartSnapperTestAccess;
    struct ControlRectangle { QRect rect; int depth; int controlType = ControlUnknown; };
    struct ControlQuery {
        std::mutex mutex;
        bool busy = false, hasKey = false, completed = false;
        quintptr ownerWindow = 0;
        QRect windowRect;
        QPoint point;
        quint64 requestSerial = 0;
        quint64 completedSerial = 0;
        std::chrono::steady_clock::time_point completedAt;
        std::vector<ControlRectangle> rectangles;
        void setTarget(quintptr owner, const QRect& rect, const QPoint& physicalPoint);
        bool fresh(std::chrono::steady_clock::time_point now) const;
        void publish(quint64 serial, std::vector<ControlRectangle> result);
    };
    std::shared_ptr<ControlQuery> m_query = std::make_shared<ControlQuery>();
    QString m_mode="Contour";
    QRect m_desktop;
    quintptr m_excludedWindow=0;
    quint64 m_generation = 0;
    bool m_snapshotReady = false;
    mutable CandidateContext m_context;
    std::vector<QRect> nativeCandidates(const QPoint& pos) const;
    std::vector<Candidate> nativeCandidateDetails(const QPoint& pos) const;
    static std::vector<Candidate> fuseCandidates(std::vector<Candidate> native,
                                                const std::vector<QRect>& visual,
                                                const QPoint& pos,
                                                const CandidateContext& context);
    static void prioritizeVisibleCandidates(std::vector<Candidate>& candidates,
                                            const CandidateContext& context, const QRect& desktop);
    std::vector<QRect> m_boundingBoxes;
};
