// Same-capture production candidates; no UI input, no UIA text, no network.
#include "smart_snapper.h"
#include "screen_capturer.h"
#include <QGuiApplication>
#include <QScreen>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QThread>
#include <QThreadPool>
#include <QtGui/qscreen_platform.h>
#include <windows.h>
#include <dwmapi.h>
#include <array>
#include <algorithm>
#include <iostream>

static QJsonArray box(const QRect& r) { return {r.x(),r.y(),r.width(),r.height()}; }
static QJsonArray boxes(const std::vector<QRect>& rs) { QJsonArray a;for(const auto& r:rs)a.append(box(r));return a; }
static QJsonArray details(const std::vector<SmartSnapper::Candidate>& candidates) {
    QJsonArray out;for(const auto& c:candidates)out.append(QJsonObject{{"rect",box(c.rect)},{"sources",int(c.sources)},{"depth",c.depth},{"owner_window",QString::number(c.ownerWindow)},{"generation",QString::number(c.generation)},{"request_serial",QString::number(c.requestSerial)},{"control_type",c.controlType}});return out;
}
static double elapsed(const QElapsedTimer& t) { return t.nsecsElapsed()/1e6; }
static bool save(const QString& p,const QJsonObject& o) { QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(o).toJson())>0; }

int main(int argc,char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QGuiApplication app(argc,argv);
    QString out,scene,pointsPath;int timeout=1500,poll=10;bool captureOnly=false;
    const auto args=app.arguments();
    for(int i=1;i<args.size();++i) {
        if(args[i]=="--capture-only"){captureOnly=true;continue;}
        if(i+1>=args.size())return 2;
        const auto key=args[i],value=args[++i];
        if(key=="--output")out=value;else if(key=="--scene")scene=value;
        else if(key=="--points")pointsPath=value;
        else if(key=="--timeout-ms")timeout=value.toInt();
        else if(key=="--poll-ms")poll=value.toInt();else return 2;
    }
    if(out.isEmpty()||scene.isEmpty()||scene.contains('/')||scene.contains('\\')||timeout<100||poll<1) {
        std::cerr<<"--output DIR --scene ID [--points JSON] [--capture-only] [--timeout-ms 1500] [--poll-ms 10]\n";return 2;
    }
    QDir().mkpath(out);const QString base=QDir(out).absoluteFilePath(scene);
    if(QFile::exists(base+".json")||QFile::exists(base+".png"))return 6; // Never overwrite a capture.
    const auto foreground=GetForegroundWindow();RECT frame{};
    if(FAILED(DwmGetWindowAttribute(foreground,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame))))GetWindowRect(foreground,&frame);
    const auto utc=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QElapsedTimer capture;capture.start();const auto snapshot=ScreenCapturer::captureVirtualScreen();const double captureMs=elapsed(capture);
    if(!snapshot.isValid)return 4;
    const auto image=snapshot.fullSnapshot.toImage();const auto desktop=snapshot.virtualGeometry;
    if(!image.save(base+".png"))return 5;
    QJsonArray screens;
    const auto toLogical=[&](QPoint p) {
        for(auto* s:app.screens()) {
            MONITORINFO info{};info.cbSize=sizeof(info);auto* native=s->nativeInterface<QNativeInterface::QWindowsScreen>();
            if(native&&GetMonitorInfoW(native->handle(),&info)&&QRect(info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top).contains(p))
                return s->geometry().topLeft()+QPoint(qRound((p.x()-info.rcMonitor.left)/s->devicePixelRatio()),qRound((p.y()-info.rcMonitor.top)/s->devicePixelRatio()));
        }return p;
    };
    for(auto* s:app.screens())screens.append(QJsonObject{{"logical",box(s->geometry())},{"dpr",s->devicePixelRatio()}});
    if(captureOnly) {
        if(!save(base+".json",QJsonObject{{"schema",1},{"preview",true},{"scene",scene},{"capture_utc",utc},{"snapshot",scene+".png"},{"desktop",box(desktop)},{"image_pixels",QJsonArray{image.width(),image.height()}},{"dpr",image.devicePixelRatio()},{"capture_ms",captureMs},{"screens",screens}}))return 3;
        std::cout<<"Preview saved: "<<base.toStdString()<<".png\n";return 0;
    }
    QJsonArray requests;
    if(!pointsPath.isEmpty()) {
        QFile f(pointsPath);if(!f.open(QIODevice::ReadOnly))return 7;
        const auto doc=QJsonDocument::fromJson(f.readAll());requests=doc.isArray()?doc.array():doc.object()["points"].toArray();
        if(requests.empty())return 7;
    }else{
        int id=0;for(double fy:{.1,.3,.5,.7,.9})for(double fx:{.1,.3,.5,.7,.9}) {
            const auto p=toLogical({qRound(frame.left+(frame.right-frame.left)*fx),qRound(frame.top+(frame.bottom-frame.top)*fy)})-desktop.topLeft();
            requests.append(QJsonObject{{"id",QString("grid-%1").arg(++id)},{"point",QJsonArray{p.x(),p.y()}}});
        }
    }
    // Warm preprocessing is intentionally separate from per-point time to candidate.
    SmartSnapper cv;cv.configure("Contour",desktop,0);QJsonArray preprocess;
    for(int i=0;i<11;++i){QElapsedTimer t;t.start();cv.processSnapshot(image,image.devicePixelRatio());preprocess.append(elapsed(t));}
    QJsonArray results;
    for(const auto& request:requests) {
        auto row=request.toObject();const auto a=row["point"].toArray();if(a.size()!=2)return 7;
        const QPoint point(a[0].toInt(),a[1].toInt());
        std::array<SmartSnapper,4> snaps;const std::array<QString,4> modes={"Window","Element","Contour","Hybrid"};
        QJsonObject modeResults;
        for(size_t i=0;i<modes.size();++i) {
            auto& snap=snaps[i];const auto mode=modes[i];snap.configure(mode,desktop,0);
            double prep=0;if(mode=="Contour"||mode=="Hybrid") {QElapsedTimer t;t.start();snap.processSnapshot(image,image.devicePixelRatio());prep=elapsed(t);}
            QElapsedTimer timer;timer.start();QJsonArray queryTimes,changes;std::vector<QRect> previous,result;std::vector<SmartSnapper::Candidate> candidates;
            double first=-1,ready=-1,lastChange=0;bool foregroundSame=true;
            do {
                QElapsedTimer q;q.start();candidates=snap.findCandidateDetailsAt(point);const double query=elapsed(q);result.clear();for(const auto& c:candidates)result.push_back(c.rect);const auto now=elapsed(timer);queryTimes.append(query);
                if(first<0&&!result.empty())first=now;
                if(changes.empty()||result!=previous){changes.append(QJsonObject{{"at_ms",now},{"boxes",boxes(result)},{"details",details(candidates)}});previous=result;lastChange=now;}
                const auto context=snap.candidateContext();
                const bool done=(mode=="Window"||mode=="Contour"||(context.controlsReady&&!context.controlsPending)||!context.ownerWindow);
                if(done&&ready<0)ready=now;
                foregroundSame=foregroundSame&&(GetForegroundWindow()==foreground);
                if(done||elapsed(timer)>=timeout)break;
                app.processEvents();QThread::msleep(poll);
            }while(true);
            const auto context=snap.candidateContext();
            modeResults[mode]=QJsonObject{{"preprocess_ms",prep},{"first_candidate_ms",first},{"ready_ms",ready},{"last_candidate_change_ms",lastChange},{"observation_ms",elapsed(timer)},{"query_ms",queryTimes},{"changes",changes},{"final_boxes",boxes(result)},{"final_details",details(candidates)},{"controls_ready",context.controlsReady},{"controls_pending",context.controlsPending},{"snapshot_ready",context.snapshotReady},{"owner_window",QString::number(context.ownerWindow)},{"foreground_unchanged",foregroundSame}};
        }
        row["modes"]=modeResults;results.append(row);
    }
    const QJsonObject output{{"schema",1},{"scene",scene},{"capture_utc",utc},{"snapshot",scene+".png"},{"desktop",box(desktop)},{"image_pixels",QJsonArray{image.width(),image.height()}},{"dpr",image.devicePixelRatio()},{"screens",screens},{"foreground_handle",QString::number(quintptr(foreground))},{"foreground_physical_frame",QJsonArray{int(frame.left),int(frame.top),int(frame.right-frame.left),int(frame.bottom-frame.top)}},{"capture_ms",captureMs},{"preprocess_ms",preprocess},{"poll_ms",poll},{"timeout_ms",timeout},{"points",results},{"foreground_unchanged_at_end",GetForegroundWindow()==foreground}};
    if(!save(base+".json",output))return 3;
    QThreadPool::globalInstance()->waitForDone();
    std::cout<<"Saved "<<results.size()<<" points: "<<base.toStdString()<<".json\n";return 0;
}
