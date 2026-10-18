// Re-run production CV on historical PNGs, fuse with contemporaneous native geometry.
// No live native/UIA queries. GT is copied for reporting and never used by detection/ranking.
#include "smart_snapper.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <chrono>
#include <stdexcept>

struct SmartSnapperTestAccess {
    static std::vector<SmartSnapper::Candidate> fuse(const SmartSnapper& cv,
            std::vector<SmartSnapper::Candidate> native, const QPoint& point,
            const SmartSnapper::CandidateContext& context) {
        return SmartSnapper::fuseCandidates(std::move(native), cv.m_boundingBoxes, point, context);
    }
    static const std::vector<QRect>& regions(const SmartSnapper& cv) { return cv.m_boundingBoxes; }
};
namespace {
QByteArray bytes(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error(("Cannot read " + path).toStdString());
    return f.readAll();
}
QString sha(const QString& path) { return QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex(); }
QRect rect(const QJsonArray& r) { return {r[0].toInt(),r[1].toInt(),r[2].toInt(),r[3].toInt()}; }
QJsonArray box(const QRect& r) { return {r.x(),r.y(),r.width(),r.height()}; }
SmartSnapper::Candidate candidate(const QJsonObject& o) {
    // Historical payload has no UIA controlType; live captures may supply it.
    return {rect(o["rect"].toArray()),o["sources"].toInt() & 3,o["depth"].toInt(-1),
        quintptr(o["owner_window"].toString().toULongLong()),o["generation"].toString().toULongLong(),
        o["request_serial"].toString().toULongLong(),o["control_type"].toInt(0)};
}
QJsonArray details(const std::vector<SmartSnapper::Candidate>& values) {
    QJsonArray result;
    for (const auto& c:values) result.append(QJsonObject{{"rect",box(c.rect)},{"sources",c.sources},
        {"depth",c.depth},{"owner_window",QString::number(c.ownerWindow)},
        {"generation",QString::number(c.generation)},{"request_serial",QString::number(c.requestSerial)},
        {"control_type",c.controlType}});
    return result;
}
QJsonArray boxes(const std::vector<SmartSnapper::Candidate>& values) {
    QJsonArray result; for(const auto& c:values) result.append(box(c.rect)); return result;
}
void write(const QString& path,const QJsonObject& value) {
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) throw std::runtime_error(("Refusing overwrite / cannot create "+path).toStdString());
    f.write(QJsonDocument(value).toJson(QJsonDocument::Indented));
}
template<class Function> double timeMs(Function function) {
    const auto start=std::chrono::steady_clock::now(); function();
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        const auto args=app.arguments();
        auto option=[&](const QString& key,const QString& fallback) {
            const auto i=args.indexOf(key);return i>=0 && i+1<args.size()?args[i+1]:fallback;
        };
        const QString source=QDir(option("--source",QString(REPLAY_ROOT)+"/docs/evidence/hybrid-snap-2026-10-18")).absolutePath();
        const QString baseline=QDir(option("--baseline",QString(REPLAY_ROOT)+"/docs/evidence/hybrid-ranking-2026-10-18/v2")).absolutePath();
        const QString output=QDir(option("--output",QString(REPLAY_ROOT)+"/docs/evidence/snap-refinement-2026-10-18/history-v1")).absolutePath();
        const int repeats=option("--repeat","1000").toInt();
        const QString nativeMode=option("--native-mode","historical");
        const auto names=option("--scenes","codex-v2,explorer,web,paper").split(',');
        if(repeats<1 || output==source || output==baseline) throw std::runtime_error("Invalid output / repeat");
        QDir().mkpath(output);
        const QString cpp=SNAP_CPP_PATH, header=QString(REPLAY_ROOT)+"/src/core/smart_snapper.h";
        if(sha(cpp)!=SNAP_CPP_SHA || sha(header)!=SNAP_H_SHA) throw std::runtime_error("Source changed; reconfigure/rebuild");
        QJsonObject manifest{{"schema",1},{"kind",nativeMode=="historical"?
            "historical-image-production-CV-plus-contemporaneous-native-replay":"same-capture-typed-native-production-CV-replay"},
            {"run_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"smart_snapper_cpp_sha256",SNAP_CPP_SHA},{"smart_snapper_h_sha256",SNAP_H_SHA},
            {"executable_sha256",sha(app.applicationFilePath())},{"replay_source_sha256",sha(QString(REPLAY_ROOT)+"/tests/contour/refinement_replay.cpp")},
            {"qt_version",qVersion()},{"query_repeats",repeats},
            {"native_mode",nativeMode},{"smart_snapper_cpp_path",cpp},
            {"timing_scope","steady_clock; preprocess processSnapshot only, query CV findCandidatesAt only, fusion production fuseCandidates including rank; no current UIA, image decoding, GUI/input/paint"},
            {"native_limit",nativeMode=="historical"?
                "Native geometry recovered from old Hybrid final_details, sources masked with 3. Old fusion/dedup may already have selected geometry; unavailable historical controlType stays unknown.":
                "Native geometry and control_type recovered from same-capture Element final_details; owner Window supplemented only when absent. Both versions use identical payload; neither replay queries current UIA."}};
        QJsonArray inputs;
        for(const QString& name:names) {
            const auto old=QJsonDocument::fromJson(bytes(source+"/"+name+".json")).object();
            const auto v2=nativeMode=="historical"?QJsonDocument::fromJson(bytes(baseline+"/"+name+"-replay.json")).object():QJsonObject();
            const QString png=source+"/"+old["snapshot"].toString();
            const QImage image(png);
            if(image.isNull()) throw std::runtime_error("PNG read failed");
            SmartSnapper cv;cv.configure("Contour",rect(old["desktop"].toArray()),0);
            const double dpr=old["dpr"].toDouble();
            QJsonArray preprocessing;
            for(int n=0;n<21;++n) preprocessing.append(timeMs([&]{cv.processSnapshot(image,dpr);}));
            QJsonArray points;
            for(const auto& value:old["points"].toArray()) {
                const auto p=value.toObject(),modes=p["modes"].toObject(),hybrid=modes["Hybrid"].toObject();
                const auto xy=p["point"].toArray();const QPoint at(xy[0].toInt(),xy[1].toInt());
                std::vector<SmartSnapper::Candidate> native;
                const auto nativePayload=nativeMode=="historical"?hybrid["final_details"].toArray():modes["Element"].toObject()["final_details"].toArray();
                for(const auto& c:nativePayload) {
                    auto recovered=candidate(c.toObject()); if(recovered.sources) native.push_back(recovered);
                }
                SmartSnapper::CandidateContext context;
                context.ownerWindow=hybrid["owner_window"].toString().toULongLong();context.snapshotReady=true;
                if(!native.empty()) context.generation=native.front().generation;
                for(const auto& value:modes["Window"].toObject()["final_details"].toArray()) {
                    const auto c=candidate(value.toObject());
                    if(c.ownerWindow==context.ownerWindow && (c.sources & SmartSnapper::SourceWindow)) {context.windowRect=c.rect;break;}
                }
                if(context.windowRect.isEmpty()) throw std::runtime_error("Missing contemporaneous owner window");
                if(nativeMode!="historical") {
                    bool haveWindow=false;
                    for(const auto& c:native) if((c.sources & SmartSnapper::SourceWindow) && c.ownerWindow==context.ownerWindow) haveWindow=true;
                    if(!haveWindow) for(const auto& value:modes["Window"].toObject()["final_details"].toArray()) {
                        const auto c=candidate(value.toObject());
                        if(c.ownerWindow==context.ownerWindow && (c.sources & SmartSnapper::SourceWindow)) {native.push_back(c);break;}
                    }
                }
                const auto fused=SmartSnapperTestAccess::fuse(cv,native,at,context);
                QJsonArray queryTimes,fusionTimes;
                for(int n=0;n<repeats;++n) {
                    queryTimes.append(timeMs([&]{cv.findCandidatesAt(at);}));
                    fusionTimes.append(timeMs([&]{SmartSnapperTestAccess::fuse(cv,native,at,context);}));
                }
                QJsonObject baselinePoint;
                for(const auto& item:v2["points"].toArray()) if(item.toObject()["id"]==p["id"]) baselinePoint=item.toObject();
                if(nativeMode=="historical" && baselinePoint.isEmpty()) throw std::runtime_error("Missing v2 point");
                points.append(QJsonObject{{"id",p["id"]},{"name",p["name"]},{"point",p["point"]},{"target",p["target"]},
                    {"owner_window_rect",box(context.windowRect)},{"historical_native_details",details(native)},
                    {"before_details",baselinePoint["after_details"]},{"before_boxes",baselinePoint["after_boxes"]},
                    {"after_details",details(fused)},{"after_boxes",boxes(fused)},
                    {"query_ms",queryTimes},{"fusion_rank_ms",fusionTimes}});
            }
            QJsonArray regions;for(const auto& r:SmartSnapperTestAccess::regions(cv)) regions.append(box(r));
            QJsonObject result{{"schema",1},{"scene",name},{"snapshot",old["snapshot"]},{"dpr",old["dpr"]},
                {"desktop",old["desktop"]},{"source_capture_utc",old["capture_utc"]},
                {"source_json_sha256",sha(source+"/"+name+".json")},{"source_snapshot_sha256",sha(png)},
                {"v2_replay_sha256",nativeMode=="historical"?sha(baseline+"/"+name+"-replay.json"):QString()},
                {"native_mode",nativeMode},{"preprocess_ms",preprocessing},
                {"all_cv_regions_including_root",regions},{"points",points}};
            write(output+"/"+name+"-replay.json",result);
            inputs.append(QJsonObject{{"scene",name},{"source_json_sha256",result["source_json_sha256"]},
                {"source_snapshot_sha256",result["source_snapshot_sha256"]},{"v2_replay_sha256",result["v2_replay_sha256"]}});
        }
        if(sha(cpp)!=SNAP_CPP_SHA || sha(header)!=SNAP_H_SHA) throw std::runtime_error("Source changed during replay");
        manifest["inputs"]=inputs;write(output+"/replay-manifest.json",manifest);
        QTextStream(stdout)<<"Historical replay complete: "<<output<<Qt::endl;return 0;
    } catch(const std::exception& error) {QTextStream(stderr)<<error.what()<<Qt::endl;return 2;}
}
