#include "smart_snapper.h"
#include <QCoreApplication>
#include <cstdio>
#include <algorithm>

struct SmartSnapperTestAccess {
    static int run() {
        int checks=0,failures=0;
        const auto check=[&](bool passed,const char* name) {
            ++checks;failures+=!passed;
            std::printf("%s %s\n",passed?"PASS":"FAIL",name);
        };
        using S=SmartSnapper;
        const QRect window(0,0,500,400),screen(0,0,800,600);
        S::CandidateContext context;
        context.ownerWindow=42;context.windowRect=window;context.generation=7;
        const std::vector<S::Candidate> native{
            {{100,100,90,30},S::SourceElement,5,42,7,9},
            {{80,100,300,30},S::SourceElement,4,42,7,9},
            {{70,70,350,250},S::SourceElement,3,42,7,9},
            {window,S::SourceWindow,0,42,7,9}};
        auto fused=S::fuseCandidates(native,{{101,101,88,28},screen},{120,110},context);
        check(fused.size()==4,"near duplicate collapses while row/table/window levels remain");
        check(fused.front().rect==native.front().rect &&
              fused.front().sources==(S::SourceElement|S::SourceContour) &&
              fused.front().depth==5 && fused.front().requestSerial==9,
              "UIA geometry, depth, request and merged provenance survive");
        fused=S::fuseCandidates(native,{{60,60,450,200},screen},{120,110},context);
        S::prioritizeVisibleCandidates(fused,context,screen);
        check(fused.size()==6 && fused[fused.size()-2].rect==QRect(60,60,450,200) && fused.back().rect==screen,
              "cross-window CV is retained after visible candidates and before full screen");
        context.occludedRegion=QRegion(QRect(200,0,100,400));
        fused=S::fuseCandidates(native,{{100,100,250,40},{101,101,88,28},screen},{120,110},context);
        S::prioritizeVisibleCandidates(fused,context,screen);
        check(fused.size()==6 && fused.front().rect==native.front().rect && fused.back().rect==screen,
              "occluded UIA and CV stay selectable behind exposed content; full screen is last");
        check(std::all_of(native.begin(),native.end(),[&](const auto& c){
                  return std::any_of(fused.begin(),fused.end(),[&](const auto& f){return f.rect==c.rect;});
              }),"visibility demotion preserves every native candidate");
        check(fused.front().sources==(S::SourceElement|S::SourceContour),
              "visible control still merges visual evidence beside an occluder");
        context.occludedRegion={};
        fused=S::fuseCandidates(native,{{101,101,88,28},screen},{120,110},context);
        check(fused.size()==4,"content remains available after occluder leaves");
        fused=S::fuseCandidates({},{{10,10,20,20},screen},{15,15},{});
        S::prioritizeVisibleCandidates(fused,{},screen);
        check(fused.size()==2 && fused.front().rect==QRect(10,10,20,20) && fused.back().rect==screen,
              "no owner still retains visual candidates and full screen");
        std::vector<S::Candidate> pending;
        S::prioritizeVisibleCandidates(pending,{},screen);
        check(pending.size()==1 && pending.front().rect==screen,
              "full screen is available before any asynchronous analysis completes");
        S::prioritizeVisibleCandidates(pending,{},screen);
        check(pending.size()==1 && (pending.front().sources&S::SourceDesktop),"full screen is not duplicated");
        fused=S::fuseCandidates({{{10,10,10,10},S::SourceElement,4,42,7,9}},
                               {{12,12,10,10},screen},{15,15},context);
        check(fused.size()==2,"four-pixel proximity alone cannot merge small controls");
        fused=S::fuseCandidates(native,{{101,101,88,28},screen},{450,350},context);
        check(fused.size()==4,"off-point CV is excluded");
        context.windowRect=screen;
        fused=S::fuseCandidates({{screen,S::SourceWindow,0,42,7,9}},
                               {screen},{120,110},context);
        check(fused.size()==1 && fused.front().sources==S::SourceWindow,
              "full snapshot root is excluded even for a full-screen owner");
        S::prioritizeVisibleCandidates(fused,context,screen);
        check(fused.size()==1 && fused.front().sources==(S::SourceWindow|S::SourceDesktop),
              "maximized owner and full screen share a single exact rectangle");

        S::ControlQuery state;
        state.setTarget(42,window,{0,0});
        check(state.hasKey && state.requestSerial==1 && !state.completed,
              "first physical zero point creates a request key");
        auto request=++state.requestSerial;
        state.busy=true;state.publish(request,{{{100,100,90,30},5}});
        check(state.fresh(std::chrono::steady_clock::now()) && state.rectangles.size()==1 && state.completedSerial==request,
              "matching request publishes cache including depth");
        state.completedAt-=std::chrono::milliseconds(1001);
        check(!state.fresh(std::chrono::steady_clock::now()) && state.completed && state.rectangles.size()==1,
              "expired same-target values survive for refresh without a disappearing frame");
        state.setTarget(43,window,{0,0});
        check(!state.completed && state.rectangles.empty(),"same point on another HWND invalidates cache");
        state.busy=true;state.publish(request,{{{100,100,90,30},5}});
        check(!state.completed && state.rectangles.empty() && !state.busy,
              "late result cannot publish under a newer target request");
        request=++state.requestSerial;state.publish(request,{});
        check(state.fresh(std::chrono::steady_clock::now()),"empty UIA result is a completed cache entry");
        state.completedAt-=std::chrono::milliseconds(1001);
        check(!state.fresh(std::chrono::steady_clock::now()),"stationary point expires for content refresh");
        request=state.requestSerial;state.setTarget(43,window.translated(1,0),{0,0});
        check(state.requestSerial!=request && !state.completed,"moving same HWND invalidates geometry key");
        request=state.requestSerial;state.setTarget(43,window.translated(1,0),{1,0});
        check(state.requestSerial!=request,"point change advances request identity");

        S live;live.configure("Hybrid",screen,0);
        const auto old=live.m_query;
        old->setTarget(42,window,{10,10});request=old->requestSerial;
        live.configure("Hybrid",screen,0);
        old->publish(request,{{{100,100,90,30},5}});
        check(live.m_query!=old && !live.m_query->completed && live.m_generation==2,
              "new capture owns separate state from late previous-capture results");
        const auto current=live.m_query;
        const auto generation=live.m_generation;
        S analyzed;analyzed.configure("Hybrid",screen,0);
        QImage image(screen.size(),QImage::Format_RGB32);image.fill(Qt::white);
        analyzed.processSnapshot(image);
        live.adoptSnapshotRegions(std::move(analyzed));
        check(live.m_query==current && live.m_generation==generation &&
              live.m_snapshotReady && !live.m_boundingBoxes.empty(),
              "CV adoption preserves capture generation and native request state");

        std::vector<S::Candidate> ranked{
            {{100,100,130,30},S::SourceElement,8,42,7,9},
            {{90,90,170,90},S::SourceContour,-1,42,7,0},
            {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.front().rect==QRect(90,90,170,90),
              "complete visual card precedes its accessible text leaf");
        check(ranked.size()==3 && ranked[1].rect==QRect(100,100,130,30) &&
              ranked[1].depth==8 && ranked[1].requestSerial==9,
              "ranking preserves small controls and their provenance for navigation");
        ranked={
            {{100,100,90,30},S::SourceElement,5,42,7,9},
            {{80,100,300,30},S::SourceElement,4,42,7,9},
            {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.front().rect==QRect(80,100,300,30),
              "complete accessible row precedes a same-height cell");
        ranked={
            {{100,100,90,18},S::SourceContour,-1,42,7,0},
            {{80,96,300,30},S::SourceElement,4,42,7,9},
            {{10,10,480,350},S::SourceContour,-1,42,7,0},
            {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.front().rect==QRect(80,96,300,30) &&
              ranked[1].rect==QRect(100,100,90,18),
              "glyph fragment and large layout pane do not displace a complete control");
        ranked={
            {{80,170,300,50},S::SourceContour,-1,42,7,0},
            {{80,90,300,170},S::SourceContour,-1,42,7,0},
            {{80,80,300,180},S::SourceContour,-1,42,7,0},
            {{90,180,180,30},S::SourceElement,6,42,7,9},
            {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.front().rect==QRect(80,80,300,180),
              "aligned CV paragraph fragments yield to the complete content block");
        check(ranked[1].rect==QRect(80,170,300,50) &&
              ranked[2].rect==QRect(90,180,180,30) &&
              ranked[3].rect==QRect(80,90,300,170) && ranked.size()==5,
              "top three expose distinct extents while similar blocks remain selectable");
        const auto once=ranked;
        S::rankCandidates(ranked,window);
        check(std::equal(ranked.begin(),ranked.end(),once.begin(),[](const auto& a,const auto& b) {
                  return a.rect==b.rect && a.sources==b.sources && a.depth==b.depth &&
                         a.ownerWindow==b.ownerWindow && a.generation==b.generation &&
                         a.requestSerial==b.requestSerial;
              }),"repeating ranking is deterministic and preserves all candidate metadata");
        ranked={{{100,100,16,16},S::SourceElement,6,42,7,9},
                {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.front().rect==QRect(100,100,16,16),
              "a sole small control remains the default ahead of its window");
        const QRect wideWindow(0,0,1200,900);
        ranked={
            {{10,10,180,870},S::SourceElement|S::SourceContour,3,42,7,9},
            {{20,200,150,32},S::SourceContour,-1,42,7,0},
            {wideWindow,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,wideWindow);
        check(ranked.front().rect==QRect(20,200,150,32) && ranked.size()==3 &&
              ranked[1].rect==QRect(10,10,180,870),
              "window-height sidebar yields to its content row and remains selectable");
        ranked={
            {{10,10,1180,140},S::SourceElement|S::SourceContour,3,42,7,9},
            {{100,30,180,40},S::SourceContour,-1,42,7,0},
            {wideWindow,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,wideWindow);
        check(ranked.front().rect==QRect(100,30,180,40),
              "window-width layout pane symmetrically yields to complete child content");
        ranked={
            {{10,10,1180,40},S::SourceElement|S::SourceContour,3,42,7,9},
            {{100,15,100,30},S::SourceContour,-1,42,7,0},
            {wideWindow,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,wideWindow);
        check(ranked.front().rect==QRect(10,10,1180,40),
              "full-width short row or toolbar remains a complete candidate");
        ranked={
            {{10,10,180,870},S::SourceElement|S::SourceContour,3,42,7,9},
            {wideWindow,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,wideWindow);
        check(ranked.front().rect==QRect(10,10,180,870),
              "spanning geometry alone does not demote a control without child content");

        const S::Candidate legacy{{100,100,90,30},S::SourceElement,5,42,7,9};
        check(legacy.controlType==S::ControlUnknown,
              "legacy aggregate candidates retain unknown UIA type rather than inferred semantics");
        S::ControlQuery typedCache;
        typedCache.setTarget(42,window,{120,110});
        typedCache.publish(typedCache.requestSerial,{{{80,100,300,30},4,S::ControlDataItem}});
        check(typedCache.rectangles.size()==1 &&
              typedCache.rectangles.front().controlType==S::ControlDataItem,
              "completed control cache preserves the provider control type");
        context.windowRect=window;
        const S::Candidate typedRow{{80,100,300,30},S::SourceElement,4,42,7,9,S::ControlDataItem};
        fused=S::fuseCandidates({typedRow,{window,S::SourceWindow,0,42,7,9}},
                               {{81,101,298,28},screen},{120,110},context);
        check(fused.size()==2 && fused.front().rect==typedRow.rect &&
              fused.front().controlType==S::ControlDataItem &&
              fused.front().sources==(S::SourceElement|S::SourceContour) &&
              fused.front().depth==4 && fused.front().requestSerial==9,
              "near duplicate CV merge retains accessible control type and native provenance");
        fused=S::fuseCandidates({
            {{80,100,300,30},S::SourceElement,8,42,7,9,S::ControlText},typedRow},
            {},{120,110},context);
        check(fused.size()==1 && fused.front().controlType==S::ControlDataItem &&
              fused.front().depth==4,
              "same-box label and item collapse to the complete item's metadata");
        fused=S::fuseCandidates({
            {{80,100,300,30},S::SourceElement,4,42,7,9,S::ControlButton},
            {{81,101,298,28},S::SourceElement,5,42,7,9,S::ControlEdit}},
            {},{120,110},context);
        check(fused.size()==2 && std::any_of(fused.begin(),fused.end(),[](const auto& c) {
                  return c.controlType==S::ControlButton;
              }) && std::any_of(fused.begin(),fused.end(),[](const auto& c) {
                  return c.controlType==S::ControlEdit;
              }),"different known control types keep separate nearby targets");
        fused=S::fuseCandidates({
            {{80,100,300,30},S::SourceElement,4,42,7,9,S::ControlButton},
            {{83,100,300,30},S::SourceElement,4,42,7,9,S::ControlButton}},
            {},{120,110},context);
        check(fused.size()==2,
              "same-type sibling controls keep distinct nearby targets");
        ranked={
            {{100,100,90,18},S::SourceElement,8,42,7,9,S::ControlText},
            typedRow,{window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.size()==3 && ranked.front().rect==typedRow.rect &&
              ranked.front().controlType==S::ControlDataItem &&
              std::any_of(ranked.begin(),ranked.end(),[](const auto& c) {
                  return c.rect==QRect(100,100,90,18) && c.controlType==S::ControlText;
              }),"typed complete row precedes its text leaf while the small leaf remains selectable");
        ranked={
            {{100,100,16,16},S::SourceElement,8,42,7,9,S::ControlButton},
            {{90,90,170,90},S::SourceElement,5,42,7,9,S::ControlPane},
            {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.size()==3 && ranked.front().controlType==S::ControlButton,
              "provider-identified small button precedes its generic layout pane");
        ranked={
            {{100,100,90,30},S::SourceElement,8,42,7,9,S::ControlText},
            {{90,90,170,90},S::SourceElement,5,42,7,9,S::ControlButton},
            {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.front().controlType==S::ControlButton && ranked.size()==3,
              "provider-identified complete button precedes an otherwise eligible text leaf");
        ranked={
            {{50,80,220,240},S::SourceElement,8,42,7,9,S::ControlListItem},
            {{52,110,216,30},S::SourceContour,-1,42,7,0},
            {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.front().rect==QRect(52,110,216,30) && ranked.size()==3,
              "large accessible item wrapper does not overrule a complete visual row by type alone");
        ranked={
            {{60,220,340,100},S::SourceElement|S::SourceContour,7,42,7,9,S::ControlGroup},
            {{72,232,316,44},S::SourceElement,8,42,7,9,S::ControlEdit},
            {window,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,window);
        check(ranked.front().rect==QRect(60,220,340,100) && ranked.size()==3,
              "corroborated complete input card precedes its inner edit without dropping either");
        fused=S::fuseCandidates({
            {{50,100,200,31},S::SourceElement,8,42,7,9,S::ControlGroup},
            {{50,100,200,30},S::SourceElement,7,42,7,9,S::ControlListItem},
            {{50,96,200,504},S::SourceElement,6,42,7,9,S::ControlGroup},
            {{50,100,200,500},S::SourceElement,5,42,7,9,S::ControlList},
            {{40,40,240,900},S::SourceElement|S::SourceContour,4,42,7,9,S::ControlGroup},
            {wideWindow,S::SourceWindow,0,42,7,9}}, {},{100,110},
            S::CandidateContext{42,wideWindow,7});
        check(fused.size()==4 && std::any_of(fused.begin(),fused.end(),[](const auto& c) {
                  return c.rect==QRect(50,100,200,30) && c.controlType==S::ControlListItem && c.depth==7;
              }),"near duplicate generic wrappers collapse without losing the complete item's identity");
        std::stable_sort(fused.begin(),fused.end(),[](const auto& a,const auto& b) {
            return qint64(a.rect.width())*a.rect.height()<qint64(b.rect.width())*b.rect.height();
        });
        const auto rowAt=std::find_if(fused.begin(),fused.end(),[](const auto& c) {
            return c.rect==QRect(50,100,200,30);
        });
        const auto panelAt=std::find_if(fused.begin(),fused.end(),[](const auto& c) {
            return c.rect==QRect(40,40,240,900);
        });
        check(rowAt!=fused.end() && panelAt!=fused.end() && panelAt-rowAt<=2,
              "near duplicate wrappers do not add wheel steps between complete content levels");
        ranked={
            {{60,100,210,96},S::SourceElement,8,42,7,9,S::ControlText},
            {{40,80,600,700},S::SourceElement,6,42,7,9,S::ControlGroup},
            {wideWindow,S::SourceWindow,0,42,7,9}};
        S::rankCandidates(ranked,wideWindow);
        check(ranked.front().controlType==S::ControlText && ranked.size()==3,
              "complete accessible paragraph is not demoted solely for its Text type");
        std::printf("checks %d failures %d\n",checks,failures);
        return failures?1:0;
    }
};

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    return SmartSnapperTestAccess::run();
}
