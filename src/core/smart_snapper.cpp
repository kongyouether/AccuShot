#include "smart_snapper.h"
#include <algorithm>
#include <cmath>
#include <vector>

void SmartSnapper::clear()
{
    m_boundingBoxes.clear();
    m_snapshotReady = false;
}

void SmartSnapper::adoptSnapshotRegions(SmartSnapper&& analyzed)
{
    m_boundingBoxes = std::move(analyzed.m_boundingBoxes);
    m_snapshotReady = analyzed.m_snapshotReady;
}

namespace {
struct Region { QRect rect; int pixels; bool touchesEdge; };
bool controlUnit(int type) {
    switch(type) {
    case SmartSnapper::ControlButton: case SmartSnapper::ControlCheckBox:
    case SmartSnapper::ControlComboBox: case SmartSnapper::ControlEdit:
    case SmartSnapper::ControlHyperlink: case SmartSnapper::ControlListItem:
    case SmartSnapper::ControlMenuItem: case SmartSnapper::ControlRadioButton:
    case SmartSnapper::ControlTabItem: case SmartSnapper::ControlTreeItem:
    case SmartSnapper::ControlDataItem: return true;
    default: return false;
    }
}
// Complete four-neighbour traversal: no parity sub-lattices and no truncated regions.
std::vector<Region> components(std::vector<uint8_t> mask, int w, int h) {
    std::vector<Region> result;
    std::vector<int> queue; queue.reserve(w * h);
    for (int seed = 0; seed < w * h; ++seed) {
        if (!mask[seed]) continue;
        queue.clear(); queue.push_back(seed); mask[seed] = 0;
        int left = seed % w, right = left, top = seed / w, bottom = top;
        for (size_t head = 0; head < queue.size(); ++head) {
            const int i = queue[head], x = i % w, y = i / w;
            left = std::min(left,x); right = std::max(right,x);
            top = std::min(top,y); bottom = std::max(bottom,y);
            const auto visit = [&](int n) { if(mask[n]) { mask[n]=0;queue.push_back(n); } };
            if(x) visit(i-1); if(x+1<w) visit(i+1);
            if(y) visit(i-w); if(y+1<h) visit(i+w);
        }
        result.push_back({QRect(left,top,right-left+1,bottom-top+1),int(queue.size()),
                          left==0 || top==0 || right==w-1 || bottom==h-1});
    }
    return result;
}
// Separable sliding sums keep morphology linear in image size, not kernel area.
std::vector<uint8_t> dilate(const std::vector<uint8_t>& src,int w,int h,int rx,int ry) {
    std::vector<uint8_t> tmp(w*h),out(w*h);
    for(int y=0;y<h;++y) {
        int sum=0;
        for(int x=0;x<w+rx;++x) {
            if(x<w) sum+=src[y*w+x];
            if(x>2*rx) sum-=src[y*w+x-2*rx-1];
            if(x>=rx) tmp[y*w+x-rx]=sum>0;
        }
    }
    for(int x=0;x<w;++x) {
        int sum=0;
        for(int y=0;y<h+ry;++y) {
            if(y<h) sum+=tmp[y*w+x];
            if(y>2*ry) sum-=tmp[(y-2*ry-1)*w+x];
            if(y>=ry) out[(y-ry)*w+x]=sum>0;
        }
    }
    return out;
}
}

void SmartSnapper::processSnapshot(const QImage& snapshot, qreal dpr)
{
    clear();
    if((m_mode!="Contour" && m_mode!="Hybrid") || snapshot.isNull() || snapshot.width()<32 || snapshot.height()<32) return;
    if(dpr<=0) dpr=1;
    // Work in approximately two logical pixels per sample; cap work for large desktops.
    const double scale=std::max(2.0*dpr, double(snapshot.width())/1600.0);
    const int w=std::max(2,qRound(snapshot.width()/scale));
    const int h=std::max(2,qRound(snapshot.height()/scale));
    const QImage gray=snapshot.scaled(w,h,Qt::IgnoreAspectRatio,Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_Grayscale8);
    std::vector<uint8_t> edge(w*h),lowBackground(w*h,1),background(w*h,1);
    for(int y=1;y<h-1;++y) {
        const auto *a=gray.constScanLine(y-1),*b=gray.constScanLine(y),*c=gray.constScanLine(y+1);
        for(int x=1;x<w-1;++x) {
            const int gx=a[x+1]+2*b[x+1]+c[x+1]-a[x-1]-2*b[x-1]-c[x-1];
            const int gy=c[x-1]+2*c[x]+c[x+1]-a[x-1]-2*a[x]-a[x+1];
            const int mag=std::abs(gx)+std::abs(gy);
            edge[y*w+x]=mag>36;
            background[y*w+x]=mag<=36;
            lowBackground[y*w+x]=mag<=12;
        }
    }
    struct Candidate { QRect rect; int priority; };
    std::vector<Candidate> candidates;
    const double sx=double(snapshot.width())/w/dpr,sy=double(snapshot.height())/h/dpr;
    const QRect root(0,0,qRound(snapshot.width()/dpr),qRound(snapshot.height()/dpr));
    auto add=[&](QRect r,int priority) {
        QRect logical(QPoint(qRound(r.x()*sx),qRound(r.y()*sy)),
                      QPoint(qRound((r.x()+r.width())*sx)-1,qRound((r.y()+r.height())*sy)-1));
        logical=logical.intersected(root);
        if(logical.width()<24 || logical.height()<12) return;
        if(double(logical.width())*logical.height()>root.width()*double(root.height())*.4) priority=3;
        for(auto& old:candidates) {
            if(std::abs(old.rect.left()-logical.left())<=4 && std::abs(old.rect.top()-logical.top())<=4 &&
               std::abs(old.rect.right()-logical.right())<=4 && std::abs(old.rect.bottom()-logical.bottom())<=4) return;
        }
        candidates.push_back({logical,priority});
    };
    for(auto* mask:{&lowBackground,&background}) {
        for(const auto& r:components(std::move(*mask),w,h)) {
            if(r.touchesEdge || r.rect.width()<12 || r.rect.height()<7) continue;
            if(r.pixels < r.rect.width()*r.rect.height()*.65) continue;
            // Include the border, rather than returning only the empty inner surface.
            add(r.rect.adjusted(-1,-1,1,1),0);
        }
    }
    // Pair horizontal borders before grouping joins neighbouring outlined rows.
    // The two upright sides distinguish a closed control from text baselines.
    struct Stroke { int left,right,y; };
    std::vector<Stroke> strokes;
    for(int y=1;y<h-1;++y) for(int x=1;x<w-1;) {
        if(!edge[y*w+x]) { ++x;continue; }
        const int left=x;while(x<w-1 && edge[y*w+x]) ++x;
        if(x-left<24) continue;
        for(auto it=strokes.rbegin();it!=strokes.rend() && y-it->y<=32;++it) {
            if(y-it->y<6 || std::abs(left-it->left)>2 || std::abs(x-it->right)>2) continue;
            // This pass detects complete UI rows, not glyph-height outlines.
            // Other passes still retain their original small text candidates.
            if(qRound((y+1)*sy)-qRound(it->y*sy)<24) continue;
            int sides=0;
            for(int row=it->y;row<=y;++row) {
                bool a=false,b=false;
                for(int dx=-2;dx<=2;++dx) {
                    if(left+dx>=0) a |= edge[row*w+left+dx]!=0;
                    if(x-1+dx<w) b |= edge[row*w+x-1+dx]!=0;
                }
                sides+=a&&b;
            }
            if(sides>=(y-it->y+1)*.8) add(QRect(left,it->y,x-left,y-it->y+1),1);
            break;
        }
        strokes.push_back({left,x,y});
    }
    // Screen-spanning separators are layout boundaries, not links between content blocks.
    // Keep them in the background masks above, but remove them before foreground grouping.
    for(int y=0;y<h;++y) for(int x=0;x<w;) {
        if(!edge[y*w+x]) { ++x;continue; }
        const int start=x;while(x<w && edge[y*w+x]) ++x;
        if(x-start>w*.65) std::fill(edge.begin()+y*w+start,edge.begin()+y*w+x,0);
    }
    for(int x=0;x<w;++x) for(int y=0;y<h;) {
        if(!edge[y*w+x]) { ++y;continue; }
        const int start=y;while(y<h && edge[y*w+x]) ++y;
        if(y-start>h*.65) for(int row=start;row<y;++row) edge[row*w+x]=0;
    }
    const auto partition=[&](const QRect& tight) {
        // A long internal vertical border can separate a picture from an
        // adjoining caption even when anti-aliasing connects their edges.
        // Keep the full block and add its parts; never discard small targets.
        if(tight.height()<40 || tight.width()<48) return;
        int previous=tight.left();
        for(int x=tight.left()+12;x<=tight.right()-12;++x) {
            int longest=0,run=0;
            for(int y=tight.top();y<=tight.bottom();++y) {
                run=edge[y*w+x]?run+1:0;longest=std::max(longest,run);
            }
            if(longest<tight.height()*.85) continue;
            // Repeated texture can also fill an entire Sobel column. A straight
            // border has a stable narrow strip along its length.
            int variation=0;
            for(int y=tight.top()+1;y<=tight.bottom();++y)
                for(int dx=-1;dx<=1;++dx)
                    variation+=std::abs(int(gray.constScanLine(y)[x+dx])-gray.constScanLine(y-1)[x+dx]);
            if(variation>tight.height()*3*12) continue;
            int boundary=x+1;
            // A Sobel border occupies several neighbouring columns.
            while(boundary<=tight.right()-12) {
                int count=0;
                for(int y=tight.top();y<=tight.bottom();++y) count+=edge[y*w+boundary]!=0;
                if(count<tight.height()*.85) break;
                ++boundary;
            }
            if(boundary-previous>=12) add(QRect(previous,tight.top(),boundary-previous,tight.height()),1);
            previous=boundary;x=boundary;
        }
        if(previous!=tight.left() && tight.right()+1-previous>=12)
            add(QRect(previous,tight.top(),tight.right()+1-previous,tight.height()),1);
    };
    // Foreground components recover icons and closed outlines. Small glyph-height
    // fragments belong to the block pass instead of competing with whole headings.
    for(const auto& r:components(edge,w,h)) {
        if(r.rect.width()>=12 && r.rect.height()>=14 && !r.touchesEdge) {
            const auto tight=r.rect.adjusted(1,1,-1,-1);
            add(tight,1);
            partition(tight);
        }
    }
    // Group glyphs and adjacent lines into content blocks, keeping the tight original extent.
    for(const auto& r:components(dilate(edge,w,h,12,3),w,h)) {
        if(r.touchesEdge) continue;
        const auto tight=r.rect.adjusted(13,4,-13,-4);
        add(tight,2);
        partition(tight);
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
        if(a.priority!=b.priority) return a.priority<b.priority;
        return int64_t(a.rect.width())*a.rect.height()<int64_t(b.rect.width())*b.rect.height();
    });
    for(const auto& c:candidates) m_boundingBoxes.push_back(c.rect);
    m_boundingBoxes.push_back(root);
    m_snapshotReady = true;
}

std::vector<QRect> SmartSnapper::findCandidatesAt(const QPoint& pos) const
{
    std::vector<QRect> matched;
    for (const auto& candidate : findCandidateDetailsAt(pos)) matched.push_back(candidate.rect);
    return matched;
}

std::vector<SmartSnapper::Candidate> SmartSnapper::findCandidateDetailsAt(const QPoint& pos) const
{
    m_context = {};
    m_context.generation = m_generation;
    m_context.snapshotReady = m_snapshotReady;
    std::vector<Candidate> matched;
    // An empty desktop denotes image-only analysis/replay, without live windows.
    if (m_mode != "Contour" || !m_desktop.isEmpty()) {
        matched = nativeCandidateDetails(pos);
    }
    if (m_mode == "Hybrid") {
        matched = fuseCandidates(std::move(matched), m_boundingBoxes, pos, m_context);
    } else if (m_mode == "Contour") {
        // Preserve precomputed contour order; append the owner window as a larger level.
        auto native = std::move(matched);
        matched.clear();
        for (const auto& box : m_boundingBoxes) {
            if (box.contains(pos))
                matched.push_back({box, SourceContour, -1, m_context.ownerWindow, m_generation, 0});
        }
        for (const auto& candidate : native)
            if (std::none_of(matched.begin(), matched.end(), [&](const auto& c) { return c.rect == candidate.rect; }))
                matched.push_back(candidate);
    }
    if (!m_desktop.isEmpty())
        prioritizeVisibleCandidates(matched, m_context, QRect(QPoint(), m_desktop.size()));
    return matched;
}

void SmartSnapper::prioritizeVisibleCandidates(std::vector<Candidate>& candidates,
    const CandidateContext& context, const QRect& desktop)
{
    // Visibility changes recommendation order, never the set available to the wheel.
    std::stable_partition(candidates.begin(), candidates.end(), [&](const Candidate& c) {
        return context.windowRect.contains(c.rect) && !context.occludedRegion.intersects(c.rect);
    });
    Candidate fullScreen{desktop, SourceDesktop, 0, 0, context.generation, 0};
    const auto existing = std::find_if(candidates.begin(), candidates.end(), [&](const Candidate& c) { return c.rect == desktop; });
    if (existing != candidates.end()) {
        fullScreen = *existing;
        fullScreen.sources |= SourceDesktop;
        candidates.erase(existing);
    }
    candidates.push_back(fullScreen); // Also present before CV/UIA finishes, or with no owner.
}

std::vector<SmartSnapper::Candidate> SmartSnapper::fuseCandidates(
    std::vector<Candidate> native, const std::vector<QRect>& visual,
    const QPoint& pos, const CandidateContext& context)
{
    const auto equivalent = [](const QRect& a, const QRect& b) {
        if (std::abs(a.left()-b.left()) > 4 || std::abs(a.top()-b.top()) > 4 ||
            std::abs(a.right()-b.right()) > 4 || std::abs(a.bottom()-b.bottom()) > 4) return false;
        const QRect intersection = a.intersected(b);
        const qint64 overlap = qint64(intersection.width())*intersection.height();
        const qint64 total = qint64(a.width())*a.height()+qint64(b.width())*b.height()-overlap;
        return total > 0 && double(overlap)/total >= .85;
    };
    std::vector<Candidate> result;
    const auto add = [&](Candidate candidate) {
        for (auto& old : result) {
            if (!equivalent(old.rect, candidate.rect)) continue;
            const auto visible = [&](const QRect& r) {
                return context.windowRect.contains(r) && !context.occludedRegion.intersects(r);
            };
            if (visible(old.rect) != visible(candidate.rect)) continue;
            // Distinct UIA controls may overlap by a few pixels. Without an
            // object identity, near geometry alone cannot prove duplication.
            if ((old.sources & SourceElement) && (candidate.sources & SourceElement) &&
                controlUnit(old.controlType) && controlUnit(candidate.controlType) &&
                old.rect!=candidate.rect) continue;
            const int sources = old.sources | candidate.sources;
            if (((candidate.sources & SourceElement) && !(old.sources & SourceElement)) ||
                (controlUnit(candidate.controlType) && !controlUnit(old.controlType)) ||
                (old.controlType==ControlUnknown && candidate.controlType!=ControlUnknown)) old = candidate;
            old.sources = sources;
            return;
        }
        result.push_back(candidate);
    };
    for (const auto& candidate : native) add(candidate);
    // The full desktop is appended exactly once after visibility prioritization.
    for (size_t i = 0; i + 1 < visual.size(); ++i) {
        const auto& rect = visual[i];
        if (rect.contains(pos))
            add({rect, SourceContour, -1, context.ownerWindow, context.generation, 0});
    }
    rankCandidates(result, context.windowRect);
    return result;
}

void SmartSnapper::rankCandidates(std::vector<Candidate>& candidates, const QRect& windowRect)
{
    const auto area = [](const QRect& r) { return qint64(r.width())*r.height(); };
    struct Ranked { Candidate candidate; int tier, evidence, completeChildren; };
    std::vector<Ranked> ranked;
    ranked.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const auto& rect = candidate.rect;
        const bool contour = candidate.sources & SourceContour;
        const bool element = candidate.sources & SourceElement;
        // A window or a large layout pane is useful for navigation, but rarely
        // the content unit intended by a click. Glyph-height boxes stay available.
        bool layout = (candidate.sources & SourceWindow) ||
                      area(rect) >= area(windowRect)*.4;
        const bool spansWindow = (rect.height()>=windowRect.height()*.85 && rect.width()>64) ||
                                 (rect.width()>=windowRect.width()*.85 && rect.height()>64);
        int completeChildren = 0;
        bool wholeRow = false;
        for (const auto& child : candidates) {
            if (&candidate == &child || area(child.rect) >= area(rect) ||
                !rect.adjusted(-4,-4,4,4).contains(child.rect)) continue;
            // Narrow sidebars and shallow full-width panes can occupy little
            // area yet still be layout. Require smaller complete content, and
            // preserve row-sized bands such as a full-width 32px toolbar.
            if (spansWindow && child.rect.width()>=24 && child.rect.height()>=24 &&
                area(child.rect)<area(rect)*.5) layout=true;
            // Matching horizontal boundaries identify line/paragraph fragments
            // inside a complete visual block, rather than unrelated nested panes.
            if (contour && (child.sources & SourceContour) &&
                std::abs(rect.left()-child.rect.left()) <= std::max(4,rect.width()/50) &&
                std::abs(rect.right()-child.rect.right()) <= std::max(4,rect.width()/50))
                ++completeChildren;
            // An accessible row can include a narrow text/cell descendant.
            if (element && rect.height()<=64 && child.rect.width()<=rect.width()*.8 &&
                std::abs(rect.height()-child.rect.height())<=std::max(8,rect.height()/4))
                wholeRow = true;
            // A labelled control is a complete unit. Text by itself can also
            // be a complete paragraph, so do not demote every Text provider.
            if (element && controlUnit(candidate.controlType) &&
                child.controlType==ControlText) ++completeChildren;
        }
        const bool unit=controlUnit(candidate.controlType);
        // Provider types are hints: a Group can be a complete card and a
        // ListItem can be a large layout wrapper. Keep visual evidence first.
        const int evidence = contour && element ? 0 : (contour || wholeRow ? 1 : 2);
        const int tier = layout ? 2 : (unit ? 0 : (rect.height()<24 || rect.width()<24 ? 1 : 0));
        if (wholeRow) ++completeChildren;
        ranked.push_back({candidate,tier,evidence,completeChildren});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [&](const Ranked& a, const Ranked& b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        if (a.evidence != b.evidence) return a.evidence < b.evidence;
        if (a.completeChildren != b.completeChildren) return a.completeChildren > b.completeChildren;
        return area(a.candidate.rect) < area(b.candidate.rect);
    });
    // Give the first three choices distinct extents when possible. Similar CV
    // thresholds can produce several almost identical nested blocks; delaying
    // those leaves room for another useful row/card level without deleting any.
    for (size_t i=1; i<std::min(size_t(3),ranked.size()); ++i) {
        const auto repeatsEarlier = [&](const Ranked& entry) {
            for (size_t earlier=0; earlier<i; ++earlier) {
                const auto& a=entry.candidate.rect;
                const auto& b=ranked[earlier].candidate.rect;
                const qint64 overlap=area(a.intersected(b));
                if (double(overlap)/(area(a)+area(b)-overlap)>=.7) return true;
            }
            return false;
        };
        if (!repeatsEarlier(ranked[i])) continue;
        auto next=ranked.begin()+i+1;
        while (next!=ranked.end() && next->tier==ranked[i].tier && repeatsEarlier(*next)) ++next;
        if (next!=ranked.end() && next->tier==ranked[i].tier)
            std::rotate(ranked.begin()+i,next,next+1);
    }
    for (size_t i=0; i<ranked.size(); ++i) candidates[i]=ranked[i].candidate;
}

void SmartSnapper::configure(const QString& mode,const QRect& desktop,quintptr excludedWindow) {
    m_mode=mode; m_desktop=desktop; m_excludedWindow=excludedWindow;
    ++m_generation;
    m_context = {};
    clear();
    m_query=std::make_shared<ControlQuery>();
}
