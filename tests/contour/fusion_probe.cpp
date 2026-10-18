// Read-only production SmartSnapper timing. Does not create a window,
// move the cursor, activate a target, or retain UIA text/content.
#include "smart_snapper.h"
#include "screen_capturer.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QThreadPool>
#include <QDateTime>
#include <QtGui/qscreen_platform.h>
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>

static QJsonArray box(const QRect& r) { return {r.x(),r.y(),r.width(),r.height()}; }
static QJsonArray boxes(const std::vector<QRect>& rs) { QJsonArray a;for(const auto& r:rs)a.append(box(r));return a; }
int main(int argc,char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QGuiApplication app(argc,argv);
    if(argc!=2)return 2;
    const auto foreground=GetForegroundWindow();
    RECT frame{};if(FAILED(DwmGetWindowAttribute(foreground,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame))))GetWindowRect(foreground,&frame);
    QRect desktop;QJsonArray screens;
    for(auto* s:app.screens()) { desktop=desktop.united(s->geometry());screens.append(QJsonObject{{"logical",box(s->geometry())},{"dpr",s->devicePixelRatio()}}); }
    const auto toLogical=[&](QPoint p) {
        for(auto* s:app.screens()) {
            MONITORINFO info{};info.cbSize=sizeof(info);
            if(GetMonitorInfoW(s->nativeInterface<QNativeInterface::QWindowsScreen>()->handle(),&info)&&QRect(info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top).contains(p))
                return s->geometry().topLeft()+QPoint(qRound((p.x()-info.rcMonitor.left)/s->devicePixelRatio()),qRound((p.y()-info.rcMonitor.top)/s->devicePixelRatio()));
        }return p;
    };
    const auto snapshot=ScreenCapturer::captureVirtualScreen();if(!snapshot.isValid)return 4;
    const auto image=snapshot.fullSnapshot.toImage();
    const QString imagePath=QFileInfo(argv[1]).absolutePath()+"/fusion-current.png";
    if(!image.save(imagePath))return 5;
    SmartSnapper contour;contour.configure("Contour",desktop,0);QJsonArray contourTimes;
    for(int i=0;i<11;++i){QElapsedTimer t;t.start();contour.processSnapshot(image,image.devicePixelRatio());contourTimes.append(t.nsecsElapsed()/1e6);}
    QJsonArray points;
    for(double fy:{.15,.5,.85})for(double fx:{.15,.5,.85}) {
        const QPoint physical(qRound(frame.left+(frame.right-frame.left)*fx),qRound(frame.top+(frame.bottom-frame.top)*fy));
        const QPoint local=toLogical(physical)-desktop.topLeft();
        QJsonArray windowTimes;SmartSnapper window;window.configure("Window",desktop,0);std::vector<QRect> windowBoxes;
        for(int i=0;i<25;++i){QElapsedTimer t;t.start();windowBoxes=window.findCandidatesAt(local);windowTimes.append(t.nsecsElapsed()/1e6);}
        QJsonArray trials;
        for(int i=0;i<2;++i) {
            SmartSnapper element;element.configure("Element",desktop,0);QElapsedTimer timer;timer.start();
            double firstDifferent=-1;QJsonArray durations;std::vector<QRect> result;
            while(timer.elapsed()<900) {
                QElapsedTimer query;query.start();result=element.findCandidatesAt(local);durations.append(query.nsecsElapsed()/1e6);
                if(firstDifferent<0&&result!=windowBoxes)firstDifferent=timer.nsecsElapsed()/1e6;
                app.processEvents();QThread::msleep(10);
            }
            trials.append(QJsonObject{{"first_result_different_from_window_ms",firstDifferent},{"final_boxes",boxes(result)},{"query_ms",durations},{"foreground_unchanged",GetForegroundWindow()==foreground}});
        }
        const auto cv=contour.findCandidatesAt(local);std::vector<QRect> merged;
        if(!trials.empty()) {
            for(const auto& b:trials.last().toObject()["final_boxes"].toArray()) { auto a=b.toArray();merged.emplace_back(a[0].toInt(),a[1].toInt(),a[2].toInt(),a[3].toInt()); }
        }
        for(const auto& r:cv)if(std::find(merged.begin(),merged.end(),r)==merged.end())merged.push_back(r);
        points.append(QJsonObject{{"physical_point",QJsonArray{physical.x(),physical.y()}},{"local_logical_point",QJsonArray{local.x(),local.y()}},{"window_boxes",boxes(windowBoxes)},{"window_query_ms",windowTimes},{"element_trials",trials},{"cv_boxes",boxes(cv)},{"union_boxes_ui_first",boxes(merged)}});
    }
    // Wait only for our own in-process Qt workers; no external process is touched.
    QThreadPool::globalInstance()->waitForDone();
    QJsonObject output{{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"screens",screens},{"desktop",box(desktop)},{"foreground_handle",QString::number(quintptr(foreground))},{"foreground_physical_frame",QJsonArray{int(frame.left),int(frame.top),int(frame.right-frame.left),int(frame.bottom-frame.top)}},{"contour_preprocess_ms",contourTimes},{"snapshot",QFileInfo(imagePath).fileName()},{"points",points}};
    QFile file(argv[1]);if(!file.open(QIODevice::WriteOnly))return 3;file.write(QJsonDocument(output).toJson());return 0;
}
