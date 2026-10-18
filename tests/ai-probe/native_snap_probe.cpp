#include "core/smart_snapper.h"
#include <QApplication>
#include <QProcess>
#include <QScreen>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QTextStream>
#include <windows.h>
#include <dwmapi.h>
#include <QtGui/qscreen_platform.h>
#include <algorithm>

struct SmartSnapperTestAccess {
 static void regions(SmartSnapper& snap,const std::vector<QRect>& boxes) {
   snap.m_boundingBoxes=boxes;snap.m_snapshotReady=true;
 }
};

int overlappingWindows(QApplication& app) {
 QProcess fixture;fixture.start(QCoreApplication::applicationFilePath(),{"fixture"});
 if(!fixture.waitForReadyRead(5000))return 2;
 const auto values=QString::fromUtf8(fixture.readLine()).trimmed().split(' ');
 if(values.size()!=3)return 3;
 const auto back=reinterpret_cast<HWND>(values[0].toULongLong());
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"EvanOcclusionTest";RegisterClassW(&wc);
 const auto front=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"Front",WS_POPUP|WS_VISIBLE,170,180,180,180,nullptr,nullptr,wc.hInstance,nullptr);
 const auto button=CreateWindowW(L"BUTTON",L"Visible",WS_CHILD|WS_VISIBLE,10,30,100,40,front,nullptr,wc.hInstance,nullptr);
 const auto overlay=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"Excluded overlay",WS_POPUP|WS_VISIBLE,100,100,800,600,nullptr,nullptr,wc.hInstance,nullptr);
 SetWindowPos(overlay,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
 auto* screen=app.primaryScreen();const auto monitor=MonitorFromWindow(back,MONITOR_DEFAULTTONEAREST);
 QRect desktop;for(auto* s:app.screens()){desktop=desktop.united(s->geometry());if(s->nativeInterface<QNativeInterface::QWindowsScreen>()->handle()==monitor)screen=s;}
 MONITORINFO info{};info.cbSize=sizeof(info);GetMonitorInfoW(monitor,&info);
 const auto point=[&](int x,int y){return screen->geometry().topLeft()-desktop.topLeft()+QPoint(qRound((x-info.rcMonitor.left)/screen->devicePixelRatio()),qRound((y-info.rcMonitor.top)/screen->devicePixelRatio()));};
 const auto rect=[&](RECT r){return QRect(point(r.left,r.top),QSize(qRound((r.right-r.left)/screen->devicePixelRatio()),qRound((r.bottom-r.top)/screen->devicePixelRatio())));};
 RECT frontBounds{},buttonBounds{},backButtonBounds{};
 GetWindowRect(front,&frontBounds);GetWindowRect(button,&buttonBounds);GetWindowRect(GetDlgItem(back,101),&backButtonBounds);
 const auto frontRect=rect(frontBounds),buttonRect=rect(buttonBounds),backButtonRect=rect(backButtonBounds);
 const auto frontPoint=buttonRect.center();
 const auto exposedPoint=point(backButtonBounds.right-5,(backButtonBounds.top+backButtonBounds.bottom)/2);
 const QRect crossing=rect(RECT{155,190,420,340});
 const QRect exposed=rect(RECT{355,backButtonBounds.top,backButtonBounds.right,backButtonBounds.bottom});
 int checks=0,failures=0;
 const auto check=[&](bool ok,const char* name){++checks;failures+=!ok;QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<name<<Qt::endl;};
 const auto query=[&](SmartSnapper& snap,QPoint p){
   std::vector<SmartSnapper::Candidate> result;QElapsedTimer timer;timer.start();
   do {result=snap.findCandidateDetailsAt(p);if(!snap.candidateContext().controlsPending)break;app.processEvents();QThread::msleep(20);}while(timer.elapsed()<3000);
   return result;
 };
 for(const auto& mode:{QString("Window"),QString("Element"),QString("Hybrid"),QString("Contour")}) {
   QTextStream(stdout)<<"mode="<<mode<<Qt::endl;
   SmartSnapper snap;snap.configure(mode,desktop,quintptr(overlay));
   SmartSnapperTestAccess::regions(snap,{crossing,buttonRect,exposed,QRect(QPoint(),desktop.size())});
   auto candidates=query(snap,frontPoint);
   check(snap.candidateContext().ownerWindow==quintptr(front),"same-process front window wins over external background; overlay excluded");
   check(!candidates.empty() && frontRect.contains(candidates.front().rect),"visible front-window content wins initial recommendation");
   check(candidates.back().rect==QRect(QPoint(),desktop.size()) && (candidates.back().sources&SmartSnapper::SourceDesktop),"full desktop is always the last and largest candidate");
   if(mode=="Hybrid"||mode=="Contour") {
     const auto cross=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.rect==crossing;});
     check(cross!=candidates.end() && cross!=candidates.begin(),"cross-window contour is retained at lower priority");
   }
   if(mode=="Element"||mode=="Hybrid")
     check(std::any_of(candidates.begin(),candidates.end(),[&](const auto& c){return c.rect==buttonRect&&(c.sources&SmartSnapper::SourceElement);}),"front same-process button is available through UIA");
   candidates=query(snap,exposedPoint);
   check(snap.candidateContext().ownerWindow==quintptr(back),"exposed background point selects its visible owner");
   check(snap.candidateContext().occludedRegion.intersects(backButtonRect),"front window occlusion is recorded for exposed background owner");
   check(candidates.back().rect==QRect(QPoint(),desktop.size()),"exposed background point still offers full desktop");
   if(mode=="Element"||mode=="Hybrid")
     check(std::any_of(candidates.begin(),candidates.end(),[&](const auto& c){return c.rect==backButtonRect;}),"partially hidden UIA control remains selectable");
   if(mode=="Hybrid"||mode=="Contour")
     check(candidates.front().rect==exposed && std::any_of(candidates.begin(),candidates.end(),[&](const auto& c){return c.rect==crossing;}),"fully exposed contour precedes retained occluded contour");
 }
 ShowWindow(front,SW_HIDE);
 SmartSnapper snap;snap.configure("Element",desktop,quintptr(overlay));
 const auto restored=query(snap,backButtonRect.center());
 check(snap.candidateContext().ownerWindow==quintptr(back) && snap.candidateContext().occludedRegion.isEmpty(),"hidden occluder no longer blocks background window");
 check(std::any_of(restored.begin(),restored.end(),[&](const auto& c){return c.rect==backButtonRect;}),"previously covered button returns after occluder hides");
 DestroyWindow(overlay);DestroyWindow(front);PostMessageW(back,WM_CLOSE,0,0);fixture.terminate();fixture.waitForFinished(2000);
 QTextStream(stdout)<<"checks="<<checks<<" failures="<<failures<<Qt::endl;
 return failures?4:0;
}

int main(int argc,char** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc>1&&QString::fromLocal8Bit(argv[1])=="fixture"){
   WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"EvanSnapTest";RegisterClassW(&wc);
   HWND window=CreateWindowW(wc.lpszClassName,L"Evan 吸附测试",WS_OVERLAPPEDWINDOW,150,150,500,350,nullptr,nullptr,wc.hInstance,nullptr);
   HWND child=CreateWindowW(L"BUTTON",L"Test control",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,40,60,180,50,window,reinterpret_cast<HMENU>(101),wc.hInstance,nullptr);
   // A large sibling without descendants comes first, like Chromium's D3D pane.
   HWND pane=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,450,280,window,nullptr,wc.hInstance,nullptr);
   SetWindowPos(pane,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
   ShowWindow(window,SW_SHOW);SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);UpdateWindow(window);RECT r{};GetWindowRect(child,&r);
   QTextStream(stdout)<<quintptr(window)<<" "<<(r.left+r.right)/2<<" "<<(r.top+r.bottom)/2<<Qt::endl;
   MSG message;while(GetMessageW(&message,nullptr,0,0)){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
 }
 QApplication app(argc,argv);
 if(argc==2&&QString::fromLocal8Bit(argv[1])=="--occlusion")return overlappingWindows(app);
 if(argc==4&&QString::fromLocal8Bit(argv[1])=="--point") {
   const QPoint physical(QString::fromLocal8Bit(argv[2]).toInt(),QString::fromLocal8Bit(argv[3]).toInt());
   const auto monitor=MonitorFromPoint(POINT{physical.x(),physical.y()},MONITOR_DEFAULTTONEAREST);
   QRect desktop;QPoint logical;
   for(auto* screen:app.screens()) {
     desktop=desktop.united(screen->geometry());
     if(screen->nativeInterface<QNativeInterface::QWindowsScreen>()->handle()==monitor) {
       MONITORINFO info{};info.cbSize=sizeof(info);GetMonitorInfoW(monitor,&info);
       logical=screen->geometry().topLeft()+QPoint(qRound((physical.x()-info.rcMonitor.left)/screen->devicePixelRatio()),qRound((physical.y()-info.rcMonitor.top)/screen->devicePixelRatio()));
     }
   }
   SmartSnapper snap;snap.configure("Element",desktop,0);std::vector<QRect> boxes;
   QElapsedTimer timer;timer.start();while(timer.elapsed()<1500){boxes=snap.findCandidatesAt(logical-desktop.topLeft());app.processEvents();QThread::msleep(50);}
   for(const auto& r:boxes)QTextStream(stdout)<<r.x()<<","<<r.y()<<" "<<r.width()<<"x"<<r.height()<<Qt::endl;
   const auto context=snap.candidateContext();
   return std::any_of(boxes.begin(),boxes.end(),[&](const auto& r){return r!=context.windowRect&&r!=QRect(QPoint(),desktop.size());})?0:4;
 }
 for(auto* screen:app.screens()) { DEVMODEW mode{};mode.dmSize=sizeof(mode);QTextStream(stdout)<<"screen="<<screen->name()<<" dpr="<<screen->devicePixelRatio()<<" EnumDisplaySettings="<<EnumDisplaySettingsW(screen->name().toStdWString().c_str(),ENUM_CURRENT_SETTINGS,&mode)<<Qt::endl; }
 QProcess fixture;fixture.start(QCoreApplication::applicationFilePath(),{"fixture"});if(!fixture.waitForReadyRead(5000))return 2;
 const auto values=QString::fromUtf8(fixture.readLine()).trimmed().split(' ');if(values.size()!=3)return 3;
 const auto hwnd=reinterpret_cast<HWND>(values[0].toULongLong());QPoint physical(values[1].toInt(),values[2].toInt());QPoint logical=physical;
 auto* screen=app.primaryScreen();
 const auto monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
 for(auto* candidate:app.screens())if(candidate->nativeInterface<QNativeInterface::QWindowsScreen>()->handle()==monitor)screen=candidate;
 MONITORINFO info{};info.cbSize=sizeof(info);GetMonitorInfoW(monitor,&info);
 const auto toLogical=[&](QPoint p){return screen->geometry().topLeft()+QPoint(qRound((p.x()-info.rcMonitor.left)/screen->devicePixelRatio()),qRound((p.y()-info.rcMonitor.top)/screen->devicePixelRatio()));};
 logical=toLogical(physical);
 RECT frame{};DwmGetWindowAttribute(hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame));
 RECT control{};GetWindowRect(GetDlgItem(hwnd,101),&control);
 QRect desktop;for(auto* screen:app.screens())desktop=desktop.united(screen->geometry());SmartSnapper snap;snap.configure("Window",desktop,0);const auto window=snap.findCandidatesAt(logical-desktop.topLeft());
 snap.configure("Element",desktop,0);std::vector<QRect> elements;QElapsedTimer timer;timer.start();while(timer.elapsed()<5000){elements=snap.findCandidatesAt(logical-desktop.topLeft());if(snap.candidateContext().controlsReady)break;app.processEvents();QThread::msleep(100);}
 const auto expected=[&](RECT r){return QRect(toLogical(QPoint(r.left,r.top))-desktop.topLeft(),QSize(qRound((r.right-r.left)/screen->devicePixelRatio()),qRound((r.bottom-r.top)/screen->devicePixelRatio())));};
 const bool ok=window.size()==2&&window.front()==expected(frame)&&window.back()==QRect(QPoint(),desktop.size())&&elements.size()>1&&elements.front()==expected(control);
 const auto printRect=[](QRect r){return QString("(%1,%2 %3x%4)").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());};
 QTextStream(stdout)<<"expected window="<<printRect(expected(frame))<<" actual="<<(window.empty()?"none":printRect(window.front()))<<"; expected control="<<printRect(expected(control))<<" actual="<<(elements.empty()?"none":printRect(elements.front()))<<"; result="<<(ok?"PASS":"FAIL")<<Qt::endl;
 PostMessageW(hwnd,WM_CLOSE,0,0);fixture.terminate();fixture.waitForFinished(2000);return ok?0:4;
}
