#include "smart_snapper.h"
#include <QGuiApplication>
#include <QPainter>
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
#include <cstdio>

double iou(QRect a,QRect b) {
    const auto c=a.intersected(b);double inter=double(c.width())*c.height();
    return inter/(double(a.width())*a.height()+double(b.width())*b.height()-inter);
}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);int failures=0,checks=0;
    qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message){ std::fprintf(stdout,"%s\n",message.toUtf8().constData()); });
    auto check=[&](bool ok,const char* name) { ++checks;if(!ok)++failures;qInfo()<< (ok?"PASS":"FAIL")<<name; };
    SmartSnapper snap;snap.configure("Contour",{},0);
    QImage blank(1601,901,QImage::Format_RGB32);blank.fill(Qt::white);
    snap.processSnapshot(blank);check(snap.findCandidatesAt({800,450})==std::vector<QRect>{QRect(0,0,1601,901)},"blank has no artificial flood fragments");
    for(double dpr:{1.0,1.25,2.0}) {
        QImage image(qRound(1600*dpr),qRound(900*dpr),QImage::Format_RGB32);image.fill(Qt::white);
        QPainter p(&image);p.scale(dpr,dpr);p.setPen(QPen(Qt::black,1));p.setBrush(QColor(220,225,230));p.drawRect(QRect(100,100,1000,650));
        p.setBrush(QColor(50,120,190));p.drawRect(QRect(200,200,200,80));p.end();
        snap.processSnapshot(image,dpr);
        auto large=snap.findCandidatesAt({700,500});auto small=snap.findCandidatesAt({300,240});
        check(std::any_of(large.begin(),large.end(),[](auto r){return iou(r,{100,100,1000,650})>.95;}),"large region is complete across DPR");
        check(std::any_of(small.begin(),small.end(),[](auto r){return iou(r,{200,200,200,80})>.9;}),"nested one-pixel border across DPR");
        const auto selected=small.front();bool stable=true;
        for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) stable &= snap.findCandidatesAt({300+x,240+y}).front()==selected;
        check(stable,"49 nearby mouse positions stable");
    }
    snap.processSnapshot(QImage());check(snap.findCandidatesAt({300,240}).empty(),"invalid snapshot clears stale candidates");
    snap.processSnapshot(blank,0);check(!snap.findCandidatesAt({800,450}).empty(),"invalid DPR normalized");
    QImage uhd(3840,2160,QImage::Format_RGB32);uhd.fill(Qt::white);QPainter p(&uhd);p.setPen(Qt::black);p.drawRect(100,100,2200,1500);p.end();
    QElapsedTimer timer;timer.start();snap.processSnapshot(uhd,1);
    qInfo()<<"4K preprocess_ms"<<timer.nsecsElapsed()/1e6;
    auto boxes=snap.findCandidatesAt({1000,1000});check(std::any_of(boxes.begin(),boxes.end(),[](auto r){return iou(r,{100,100,2200,1500})>.95;}),"4K bounded analysis preserves geometry");
    for(double dpr:{1.0,1.25,2.0}) {
        QImage image(qRound(800*dpr),qRound(450*dpr),QImage::Format_RGB32);image.fill(QColor(28,28,28));
        QPainter paint(&image);paint.scale(dpr,dpr);paint.setPen(QPen(QColor(210,210,210),1));
        paint.setBrush(QColor(70,70,70));paint.drawRect(100,80,420,24);paint.drawRect(100,109,420,24);
        paint.setBrush(Qt::yellow);paint.drawRect(108,115,14,12);
        paint.setPen(Qt::white);paint.drawText(132,126,"Folder icon and label");paint.end();
        snap.processSnapshot(image,dpr);const auto rows=snap.findCandidatesAt({250,121});
        check(std::any_of(rows.begin(),rows.end(),[](auto r){return iou(r,{100,109,420,24})>.85;}),"adjacent outlined rows retain individual complete row across DPR");
    }
    QImage figure(800,500,QImage::Format_RGB32);figure.fill(Qt::white);
    QPainter figurePainter(&figure);figurePainter.setPen(QPen(Qt::black,2));
    figurePainter.setBrush(QColor(75,120,180));figurePainter.drawRect(100,100,100,220);
    figurePainter.setBrush(QColor(90,165,65));figurePainter.drawRect(204,100,190,220);
    // Dense image texture prevents the empty-surface pass from supplying the
    // subfigure by itself; the joined foreground needs its internal boundary.
    figurePainter.setPen(QPen(Qt::white,2));
    for(int y=106;y<316;y+=8) for(int x=106;x<390;x+=10) {
        if(x>192 && x<210) continue;
        figurePainter.drawLine(x,y,x+4,y+3);
    }
    figurePainter.setPen(QPen(Qt::black,2));
    figurePainter.drawLine(200,210,204,210);figurePainter.end();
    snap.processSnapshot(figure);const auto parts=snap.findCandidatesAt({150,210});
    check(std::any_of(parts.begin(),parts.end(),[](auto r){return iou(r,{100,100,100,220})>.9;}),"internal tall border recovers subfigure despite a connecting stroke");
    check(std::any_of(parts.begin(),parts.end(),[](auto r){return iou(r,{100,100,294,220})>.9;}),"subfigure partition preserves complete figure for navigation");
    QImage glyph(600,300,QImage::Format_RGB32);glyph.fill(Qt::white);
    QPainter glyphPainter(&glyph);glyphPainter.setPen(QPen(Qt::black,1));glyphPainter.drawRect(100,100,140,14);glyphPainter.end();
    snap.processSnapshot(glyph);const auto glyphs=snap.findCandidatesAt({150,107});
    check(std::any_of(glyphs.begin(),glyphs.end(),[](auto r){return r.height()<24 && iou(r,{100,100,140,14})>.8;}),"14px glyph retains original small surface candidate");
    check(glyphs.size()==2,"14px glyph does not gain a separate outlined-row candidate");
    qInfo()<<"checks"<<checks<<"failures"<<failures;return failures?1:0;
}
