// Paired offline replay of captured candidates; no screenshot, UIA query, or GT-based ranking.
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
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace {
QByteArray bytes(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error(("Cannot read " + path).toStdString());
    return f.readAll();
}
QString sha(const QString& path) {
    return QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex();
}
QRect rect(const QJsonArray& r) { return {r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt()}; }
SmartSnapper::Candidate candidate(const QJsonObject& o) {
    return {rect(o["rect"].toArray()), o["sources"].toInt(), o["depth"].toInt(-1),
        quintptr(o["owner_window"].toString().toULongLong()),
        o["generation"].toString().toULongLong(), o["request_serial"].toString().toULongLong()};
}
bool equal(const SmartSnapper::Candidate& a, const SmartSnapper::Candidate& b) {
    return a.rect == b.rect && a.sources == b.sources && a.depth == b.depth &&
        a.ownerWindow == b.ownerWindow && a.generation == b.generation && a.requestSerial == b.requestSerial;
}
QJsonArray mapping(const std::vector<SmartSnapper::Candidate>& before,
                   const std::vector<SmartSnapper::Candidate>& after) {
    if (before.size() != after.size()) throw std::runtime_error("Ranker changed candidate count");
    std::vector<bool> used(before.size());
    QJsonArray result;
    for (const auto& c : after) {
        size_t i = 0;
        while (i < before.size() && (used[i] || !equal(before[i], c))) ++i;
        if (i == before.size()) throw std::runtime_error("Ranker changed candidate metadata or geometry");
        used[i] = true;
        result.append(int(i));
    }
    return result;
}
void writeNew(const QString& path, const QJsonObject& o) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw std::runtime_error(("Refusing overwrite / cannot create " + path).toStdString());
    const auto data = QJsonDocument(o).toJson(QJsonDocument::Indented);
    if (f.write(data) != data.size()) throw std::runtime_error("Incomplete JSON write");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        auto option = [&](const QString& key, const QString& fallback) {
            const auto i = args.indexOf(key);
            return i >= 0 && i + 1 < args.size() ? args[i+1] : fallback;
        };
        const QString input = QDir(option("--input", QString(REPLAY_ROOT) + "/docs/evidence/hybrid-snap-2026-10-18")).absolutePath();
        const QString output = QDir(option("--output", QString(REPLAY_ROOT) + "/docs/evidence/hybrid-ranking-2026-10-18")).absolutePath();
        bool ok = false;
        const int repeats = option("--repeat", "1000").toInt(&ok);
        if (!ok || repeats < 1) throw std::runtime_error("--repeat must be positive");
        if (input == output) throw std::runtime_error("Output must differ from captured evidence");
        if (!QDir().mkpath(output)) throw std::runtime_error("Cannot create output directory");
        const QString cppPath = QString(REPLAY_ROOT) + "/src/core/smart_snapper.cpp";
        const QString hPath = QString(REPLAY_ROOT) + "/src/core/smart_snapper.h";
        if (sha(cppPath) != RANK_CPP_SHA || sha(hPath) != RANK_H_SHA)
            throw std::runtime_error("Source differs from CMake-recorded hashes; reconfigure and rebuild replay");
        QJsonObject manifest{{"schema",1}, {"kind","paired-offline-candidate-ranking-replay"},
            {"run_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"source_head",SOURCE_HEAD}, {"smart_snapper_cpp_sha256",RANK_CPP_SHA},
            {"smart_snapper_h_sha256",RANK_H_SHA}, {"executable_sha256",sha(app.applicationFilePath())},
            {"replay_source_sha256",sha(QString(REPLAY_ROOT)+"/tests/contour/ranking_replay.cpp")},
            {"qt_version",qVersion()}, {"warm_repeats_per_point",repeats},
            {"timing_scope","steady_clock rankCandidates only; input vector copied before timer; excludes snapshot, CV, UIA, input, paint and GUI"}};
        QJsonArray files;
        bool firstProcessCall = true;
        for (const QString name : {"codex-v2", "explorer", "web", "paper"}) {
            const QString path = input + "/" + name + ".json";
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(bytes(path), &error);
            if (error.error != QJsonParseError::NoError) throw std::runtime_error("Invalid source JSON");
            const auto source = document.object();
            QJsonArray points;
            for (const auto& value : source["points"].toArray()) {
                const auto point = value.toObject();
                const auto modes = point["modes"].toObject();
                const auto hybrid = modes["Hybrid"].toObject();
                const auto original = hybrid["final_details"].toArray();
                const auto originalBoxes = hybrid["final_boxes"].toArray();
                if (original.isEmpty() || original.size() != originalBoxes.size())
                    throw std::runtime_error("Missing / inconsistent Hybrid candidates");
                std::vector<SmartSnapper::Candidate> before;
                for (int i=0; i<original.size(); ++i) {
                    if (original[i].toObject()["rect"] != originalBoxes[i])
                        throw std::runtime_error("final_details / final_boxes mismatch");
                    before.push_back(candidate(original[i].toObject()));
                }
                // Recover the same owner window rectangle from this point's contemporaneous Window mode.
                QRect ownerRect;
                for (const auto& v : modes["Window"].toObject()["final_details"].toArray()) {
                    const auto c = candidate(v.toObject());
                    if (c.ownerWindow == quintptr(hybrid["owner_window"].toString().toULongLong()) &&
                        (c.sources & SmartSnapper::SourceWindow)) { ownerRect = c.rect; break; }
                }
                if (ownerRect.isEmpty()) throw std::runtime_error("Cannot identify contemporaneous owner window");
                auto after = before;
                auto start = std::chrono::steady_clock::now();
                SmartSnapper::rankCandidates(after, ownerRect);
                const auto stop = std::chrono::steady_clock::now();
                const double firstMs = std::chrono::duration<double, std::milli>(stop-start).count();
                const auto order = mapping(before, after);
                QJsonArray details, boxes, warm;
                for (const auto& i : order) {
                    details.append(original[i.toInt()]);
                    boxes.append(originalBoxes[i.toInt()]);
                }
                for (int run=0; run<repeats; ++run) {
                    auto sample = before;
                    start = std::chrono::steady_clock::now();
                    SmartSnapper::rankCandidates(sample, ownerRect);
                    const auto end = std::chrono::steady_clock::now();
                    warm.append(std::chrono::duration<double, std::milli>(end-start).count());
                    if (!std::equal(sample.begin(), sample.end(), after.begin(), after.end(), equal))
                        throw std::runtime_error("Ranker is nondeterministic");
                }
                QJsonObject result{{"id",point["id"]},{"name",point["name"]},{"point",point["point"]},
                    {"target",point["target"]},{"owner_window_rect",QJsonArray{ownerRect.x(),ownerRect.y(),ownerRect.width(),ownerRect.height()}},
                    {"before_details",original},{"before_boxes",originalBoxes},
                    {"after_details",details},{"after_boxes",boxes},{"after_original_indices",order},
                    {"first_call_ms",firstMs},{"first_process_call",firstProcessCall},{"warm_ms",warm}};
                points.append(result);
                firstProcessCall = false;
            }
            const QJsonObject result{{"schema",1},{"scene",name},{"snapshot",source["snapshot"]},
                {"dpr",source["dpr"]},{"desktop",source["desktop"]},{"image_pixels",source["image_pixels"]},
                {"source_capture_utc",source["capture_utc"]},{"source_json_sha256",sha(path)},
                {"source_snapshot_sha256",sha(input+"/"+source["snapshot"].toString())},{"points",points}};
            writeNew(output+"/"+name+"-replay.json",result);
            files.append(QJsonObject{{"scene",name},{"json_sha256",sha(path)},
                {"snapshot_sha256",result["source_snapshot_sha256"]},{"points",points.size()}});
        }
        manifest["inputs"] = files;
        if (sha(cppPath) != RANK_CPP_SHA || sha(hPath) != RANK_H_SHA)
            throw std::runtime_error("Source changed during replay; discard this incomplete run");
        writeNew(output+"/replay-manifest.json",manifest);
        QTextStream(stdout) << "Replayed four captured scenes without recollection: " << output << Qt::endl;
        return 0;
    } catch (const std::exception& e) {
        QTextStream(stderr) << e.what() << Qt::endl;
        return 2;
    }
}
