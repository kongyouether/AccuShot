#include "smart_snapper.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <algorithm>

QJsonArray boxJson(const QRect& r) { return {r.x(),r.y(),r.width(),r.height()}; }
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    if(argc!=3) return 2;
    QFile manifest(argv[1]); if(!manifest.open(QIODevice::ReadOnly)) return 3;
    const auto cases=QJsonDocument::fromJson(manifest.readAll()).array();
    QJsonArray output;
    for(const auto& entry:cases) {
        const auto c=entry.toObject();
        const QString path=QFileInfo(manifest).absolutePath()+"/inputs/"+c["image"].toString();
        QImage img(path); if(img.isNull()) return 4;
        const double dpr=c["dpr"].toDouble(1);
        SmartSnapper snap; snap.configure("Contour",{},0);
        QJsonArray times; QElapsedTimer timer;
        for(int n=0;n<21;++n){timer.start();snap.processSnapshot(img,dpr);times.append(timer.nsecsElapsed()/1e6);}
        QJsonArray targets;
        for(const auto& v:c["targets"].toArray()) {
            auto t=v.toObject(); auto b=t["rect"].toArray();
            QRect r(b[0].toInt(),b[1].toInt(),b[2].toInt(),b[3].toInt());
            QJsonArray queries;
            for(const QPointF fraction:{QPointF(.5,.5),QPointF(.25,.25),QPointF(.75,.25),QPointF(.25,.75),QPointF(.75,.75)}) {
                QPoint p(qRound((r.x()+r.width()*fraction.x())/dpr),qRound((r.y()+r.height()*fraction.y())/dpr));
                QJsonArray boxes; const auto candidates=snap.findCandidatesAt(p);
                for(const auto& candidate:candidates) boxes.append(boxJson(candidate));
                timer.start();for(int k=0;k<1000;++k) snap.findCandidatesAt(p);
                queries.append(QJsonObject{{"point",QJsonArray{p.x(),p.y()}},{"boxes",boxes},{"query_us",timer.nsecsElapsed()/1e6}});
            }
            t["queries"]=queries;targets.append(t);
        }
        output.append(QJsonObject{{"image",c["image"]},{"dpr",dpr},{"width",img.width()},{"height",img.height()},{"preprocess_ms",times},{"targets",targets}});
    }
    QFile file(argv[2]);if(!file.open(QIODevice::WriteOnly))return 5;
    file.write(QJsonDocument(output).toJson());return 0;
}
