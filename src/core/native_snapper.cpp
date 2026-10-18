#include "smart_snapper.h"
#include <QGuiApplication>
#include <QScreen>
#include <QThreadPool>
#include <QElapsedTimer>
#include <windows.h>
#include <QtGui/qscreen_platform.h>
#include <dwmapi.h>
#include <UIAutomation.h>
#include <algorithm>
#include <functional>

namespace {
struct MonitorMap { QRect logical,physical; double scale; };
std::vector<MonitorMap> monitorMaps() {
    std::vector<MonitorMap> maps;
    for(auto* screen:QGuiApplication::screens()) {
        // QScreen::name() can be a friendly name (e.g. P275MR), not a Win32 device name.
        auto* native=screen->nativeInterface<QNativeInterface::QWindowsScreen>();
        MONITORINFO info{};info.cbSize=sizeof(info);
        if(native&&GetMonitorInfoW(native->handle(),&info)) {
            const auto& r=info.rcMonitor;
            maps.push_back({screen->geometry(),QRect(r.left,r.top,r.right-r.left,r.bottom-r.top),screen->devicePixelRatio()});
        }
    } return maps;
}
QPoint physicalPoint(QPoint p,const std::vector<MonitorMap>& maps) {
    for(const auto& m:maps)if(m.logical.contains(p))return m.physical.topLeft()+QPoint(qRound((p.x()-m.logical.x())*m.scale),qRound((p.y()-m.logical.y())*m.scale));return p;
}
QRect logicalRect(QRect r,const std::vector<MonitorMap>& maps) {
    for(const auto& m:maps)if(m.physical.contains(r.center()))return QRect(m.logical.topLeft()+QPoint(qRound((r.x()-m.physical.x())/m.scale),qRound((r.y()-m.physical.y())/m.scale)),QSize(qRound(r.width()/m.scale),qRound(r.height()/m.scale)));return r;
}
struct WindowQuery { QPoint point; HWND excluded; HWND window=nullptr; QRect rect; std::vector<QRect> occluders; };
BOOL CALLBACK findWindow(HWND window,LPARAM data) {
    auto& q=*reinterpret_cast<WindowQuery*>(data);if(window==q.excluded||!IsWindowVisible(window)||IsIconic(window))return TRUE;
    DWORD cloaked=0;DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
    RECT r{};if(FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r))))GetWindowRect(window,&r);
    QRect rect(r.left,r.top,r.right-r.left,r.bottom-r.top);
    if(!rect.contains(q.point)) { if(rect.isValid())q.occluders.push_back(rect);return TRUE; }
    q.window=window;q.rect=rect;return FALSE;
}
void controls(HWND window,QPoint point,std::vector<SmartSnapper::Candidate>& result) {
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(initialized))return;
    IUIAutomation* automation=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))) {
        IUIAutomation2* timed=nullptr;if(SUCCEEDED(automation->QueryInterface(IID_PPV_ARGS(&timed)))){timed->put_ConnectionTimeout(200);timed->put_TransactionTimeout(200);timed->Release();}
        IUIAutomationElement* root=nullptr;IUIAutomationTreeWalker* walker=nullptr;
        if(SUCCEEDED(automation->ElementFromHandle(window,&root))&&root&&SUCCEEDED(automation->get_ControlViewWalker(&walker))&&walker) {
            QElapsedTimer budget;budget.start();
            // Overlapping siblings can represent a renderer and the actual control tree.
            // A hit on the renderer must not hide the other matching branches.
            const std::function<void(IUIAutomationElement*,int)> visit=[&](IUIAutomationElement* current,int depth) {
                if(depth>=16||budget.elapsed()>=600)return;
                IUIAutomationElement* child=nullptr;walker->GetFirstChildElement(current,&child);
                for(int visited=0;child&&visited<200&&budget.elapsed()<600;++visited) {
                    RECT r{};child->get_CurrentBoundingRectangle(&r);QRect rect(r.left,r.top,r.right-r.left,r.bottom-r.top);
                    if(rect.isValid()&&rect.contains(point)) {
                        BOOL offscreen=FALSE;
                        if(SUCCEEDED(child->get_CurrentIsOffscreen(&offscreen))&&!offscreen) {
                            CONTROLTYPEID type=SmartSnapper::ControlUnknown;
                            if(FAILED(child->get_CurrentControlType(&type)))type=SmartSnapper::ControlUnknown;
                            result.push_back({rect,SmartSnapper::SourceElement,depth+1,0,0,0,type});
                        }
                        visit(child,depth+1);
                    }
                    IUIAutomationElement* next=nullptr;walker->GetNextSiblingElement(child,&next);child->Release();child=next;
                }
                if(child)child->Release();
            };
            visit(root,0);walker->Release();
        }
        if(root)root->Release();automation->Release();
    }CoUninitialize();
}
}
void SmartSnapper::ControlQuery::setTarget(quintptr owner, const QRect& rect, const QPoint& physicalPoint)
{
    if (hasKey && ownerWindow == owner && windowRect == rect && point == physicalPoint) return;
    hasKey = true;
    ownerWindow = owner;
    windowRect = rect;
    point = physicalPoint;
    ++requestSerial;
    completed = false;
    rectangles.clear();
}

bool SmartSnapper::ControlQuery::fresh(std::chrono::steady_clock::time_point now) const
{
    return completed && now - completedAt < std::chrono::milliseconds(1000);
}

void SmartSnapper::ControlQuery::publish(quint64 serial, std::vector<ControlRectangle> result)
{
    busy = false;
    if (serial != requestSerial) return;
    rectangles = std::move(result);
    completed = true;
    completedSerial = serial;
    completedAt = std::chrono::steady_clock::now();
}

std::vector<QRect> SmartSnapper::nativeCandidates(const QPoint& pos) const
{
    std::vector<QRect> result;
    for (const auto& candidate : nativeCandidateDetails(pos)) result.push_back(candidate.rect);
    return result;
}

std::vector<SmartSnapper::Candidate> SmartSnapper::nativeCandidateDetails(const QPoint& pos) const {
    const auto maps=monitorMaps();const auto point=physicalPoint(pos+m_desktop.topLeft(),maps);
    WindowQuery query{point,reinterpret_cast<HWND>(m_excludedWindow)};EnumWindows(findWindow,reinterpret_cast<LPARAM>(&query));
    std::vector<Candidate> result;
    if(!query.window) {
        const auto state=m_query;std::lock_guard<std::mutex> lock(state->mutex);
        state->setTarget(0,{},point);
        return result;
    }
    const auto owner=reinterpret_cast<quintptr>(query.window);
    const QRect windowRect=logicalRect(query.rect,maps).translated(-m_desktop.topLeft())
                               .intersected(QRect(QPoint(),m_desktop.size()));
    m_context.ownerWindow=owner;
    m_context.windowRect=windowRect;
    for(const auto& rect:query.occluders)
        m_context.occludedRegion += logicalRect(rect,maps).translated(-m_desktop.topLeft()).intersected(windowRect);
    quint64 serial=0;
    if(m_mode=="Element" || m_mode=="Hybrid") {
        const auto state=m_query;std::lock_guard<std::mutex> lock(state->mutex);
        state->setTarget(owner,query.rect,point);
        const bool ready=state->fresh(std::chrono::steady_clock::now());
        // Keep same-target completed values visible while their TTL refresh runs.
        if(state->completed)for(const auto& control:state->rectangles) {
            const QRect rect=logicalRect(control.rect,maps).translated(-m_desktop.topLeft()).intersected(windowRect);
            if(rect.isValid()&&rect.contains(pos))
                result.push_back({rect,SourceElement,control.depth,owner,m_generation,state->completedSerial,control.controlType});
        }
        if(!state->busy && !ready) {
            state->busy=true;
            const auto request=++state->requestSerial;
            QThreadPool::globalInstance()->start([state,window=query.window,point,request] {
                std::vector<Candidate> rectangles;controls(window,point,rectangles);
                std::vector<ControlRectangle> values;
                for(const auto& candidate:rectangles) values.push_back({candidate.rect,candidate.depth,candidate.controlType});
                std::lock_guard<std::mutex> lock(state->mutex);state->publish(request,std::move(values));
            });
        }
        serial=state->requestSerial;
        m_context.controlsReady=state->completed;
        m_context.controlsPending=state->busy;
    }
    result.push_back({windowRect,SourceWindow,0,owner,m_generation,serial});
    std::stable_sort(result.begin(),result.end(),[](const Candidate& a,const Candidate& b){return qint64(a.rect.width())*a.rect.height()<qint64(b.rect.width())*b.rect.height();});
    std::vector<Candidate> unique;
    for(const auto& candidate:result) {
        const auto old=std::find_if(unique.begin(),unique.end(),[&](const Candidate& value){return value.rect==candidate.rect;});
        if(old==unique.end()) unique.push_back(candidate);
        else {
            const int sources=old->sources|candidate.sources;
            // Several providers expose the same box as both a control and its label.
            if(old->controlType==ControlUnknown || old->controlType==ControlText ||
               old->controlType==ControlPane || old->controlType==ControlGroup) {
                if(candidate.controlType!=ControlUnknown && candidate.controlType!=ControlText &&
                   candidate.controlType!=ControlPane && candidate.controlType!=ControlGroup)
                    *old=candidate;
                else if(old->controlType==ControlUnknown && candidate.controlType!=ControlUnknown)*old=candidate;
            }
            old->sources=sources;
        }
    }
    return unique;
}
