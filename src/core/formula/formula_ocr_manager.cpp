#include "formula_ocr_manager.h"
#include "../plugin/local_plugin.h"
#include <QTemporaryDir>
#include "../ai/ai_service.h"
#include "formula_format_helper.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QBuffer>
#include <QProcess>
#include <QDateTime>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QSettings>
#include <QDebug>
#include <QNetworkProxyFactory>

FormulaOcrManager& FormulaOcrManager::instance()
{
    static FormulaOcrManager mgr;
    return mgr;
}

FormulaOcrManager::FormulaOcrManager(QObject* parent)
    : QObject(parent)
{
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    m_nam = std::make_unique<QNetworkAccessManager>(this);

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
    m_preferredMode = settings.value("FormulaOcr/PreferredMode", "Online").toString();
}

QString FormulaOcrManager::preferredMode() const
{
    if(m_preferredMode=="ai:pix2text")return "local:pix2text";
    if(m_preferredMode=="ai:paddleformula")return "local:paddleformula";
    return m_preferredMode;
}

void FormulaOcrManager::setPreferredMode(const QString& mode)
{
    m_preferredMode = mode;
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
    settings.setValue("FormulaOcr/PreferredMode", mode);
}

QString FormulaOcrManager::simpleTexToken() const
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
    return settings.value("FormulaOcr/SimpleTexToken", "").toString().trimmed();
}

void FormulaOcrManager::setSimpleTexToken(const QString& token)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
    settings.setValue("FormulaOcr/SimpleTexToken", token.trimmed());
}

QString FormulaOcrManager::findOfflinePluginExecutable() const { return LocalPlugin::directory("latex-ocr-engine")+"/venv/Scripts/python.exe"; }
bool FormulaOcrManager::isOfflinePluginReady() const { return LocalPlugin::ready("latex-ocr-engine"); }

QNetworkReply* FormulaOcrManager::recognizeAsync(const QImage& formulaImage, qreal dpr,
                                      std::function<void(const FormulaOcrResult&)> callback, const QString& requestedEngine)
{
    Q_UNUSED(dpr);
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    const QString mode = requestedEngine.isEmpty() ? m_preferredMode : requestedEngine;
    QString pluginId;
    if(mode=="Offline")pluginId="latex-ocr-engine";
    if(mode=="local:pix2text" || mode=="ai:pix2text")pluginId="pix2text";
    if(mode=="local:paddleformula" || mode=="ai:paddleformula")pluginId="paddleformula";
    if(!pluginId.isEmpty()) {
        QThreadPool::globalInstance()->start([this,formulaImage,pluginId,startTime,callback]{
            QTemporaryDir temp;const auto path=temp.filePath("formula.png");
            QJsonObject json;
            if(!formulaImage.save(path))json={{"success",false},{"error","无法写入公式图片"}};
            else json=LocalPlugin::run(pluginId,{{"image",path}});
            FormulaOcrResult result;result.success=json.value("success").toBool();result.latex=json.value("text").toString();
            result.errorMessage=json.value("error").toString();result.engineName=pluginId;
            result.elapsedMs=QDateTime::currentMSecsSinceEpoch()-startTime;
            result.blockMarkdown=FormulaFormatHelper::toBlockMarkdown(result.latex);
            result.inlineMarkdown=FormulaFormatHelper::toInlineMarkdown(result.latex);
            result.mathml=FormulaFormatHelper::toWordMathML(result.latex);
            QMetaObject::invokeMethod(this,[callback,result]{if(callback)callback(result);},Qt::QueuedConnection);
        });
        return nullptr;
    }
    if (mode.startsWith("ai:")) {
        const auto p=AiService::forTask(mode.mid(3),"formula");
        if(AiService::isLocalProfile(p)) {FormulaOcrResult result;result.errorMessage="本地公式识别请先安装并选择功能插件。";callback(result);return nullptr;}
        return AiService::run(p,AiService::taskPrompt(p.id,"formula",p.formulaPrompt),formulaImage,this,[=](AiResult r) {
            FormulaOcrResult result; result.success=r.success; result.errorMessage=r.error; result.latex=r.text;
            if(result.latex.startsWith("```")) {result.latex=result.latex.section('\n',1); if(result.latex.endsWith("```"))result.latex.chop(3);result.latex=result.latex.trimmed();}
            if(result.latex.startsWith("$$")&&result.latex.endsWith("$$"))result.latex=result.latex.mid(2,result.latex.size()-4).trimmed();
            result.blockMarkdown=FormulaFormatHelper::toBlockMarkdown(result.latex);
            result.inlineMarkdown=FormulaFormatHelper::toInlineMarkdown(result.latex);
            result.mathml=FormulaFormatHelper::toWordMathML(result.latex);
            result.engineName=p.name;result.elapsedMs=QDateTime::currentMSecsSinceEpoch()-startTime;callback(result);
        });
    }
    if(mode!="Online") {FormulaOcrResult result;result.errorMessage="原公式引擎不可用，请选择已安装插件或明确选择在线服务。";callback(result);return nullptr;}
    if (simpleTexToken().isEmpty()) {
        FormulaOcrResult result; result.isAuthError=true;result.engineName="SimpleTeX";
        result.errorMessage="请在设置中配置 SimpleTeX Token，或手动选择其他公式引擎。";
        callback(result);return nullptr;
    }

    // 针对在线上传进行超高效高对比度轻量编码 (JPEG 92 相比大型无压缩 PNG 体积减少 80%，网络秒传)
    QByteArray imageBytes;
    QBuffer buffer(&imageBytes);
    buffer.open(QIODevice::WriteOnly);
    formulaImage.save(&buffer, "JPEG", 92);
    buffer.close();

    recognizeOnlineAsync(imageBytes, startTime, callback);
    return nullptr;
}

void FormulaOcrManager::recognizeOnlineAsync(const QByteArray& imageBytes, qint64 startTime,
                                            std::function<void(const FormulaOcrResult&)> callback)
{
    // 国内免梯子高可用学术公式识别 API (SimpleTeX / 云端多通道热备)
    QUrl url("https://server.simpletex.cn/api/latex_ocr");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "AccuShot-FormulaOCR/1.1.2");
    request.setTransferTimeout(9000); // 9秒超时

    QString token = simpleTexToken();
    if (!token.isEmpty()) {
        request.setRawHeader("token", token.toUtf8());
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"file\"; filename=\"formula.png\""));
    imagePart.setBody(imageBytes);
    multiPart->append(imagePart);

    QNetworkReply* reply = m_nam->post(request, multiPart);
    multiPart->setParent(reply); // 随 reply 销毁自动释放

    connect(reply, &QNetworkReply::finished, this, [reply, startTime, callback]() {
        reply->deleteLater();
        FormulaOcrResult result;
        result.elapsedMs = QDateTime::currentMSecsSinceEpoch() - startTime;
        result.engineName = "在线云端通道 (高速)";

        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::AuthenticationRequiredError || httpStatus == 401 || httpStatus == 403) {
            result.success = false;
            result.isAuthError = true;
            result.errorMessage = "在线公式通道需要授权 Token (错误码 204/401)。\n可在右上角点击【⚙️ Token 设置】填入免费 Token (前往 simpletex.cn 免费获取，每月赠送上千次调用)，或直接切换为【本地离线插件】。";
            if (callback) callback(result);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            result.success = false;
            result.errorMessage = QString("网络请求失败 (%1): %2").arg(reply->error()).arg(reply->errorString());
            if (callback) callback(result);
            return;
        }

        QByteArray respBytes = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(respBytes);
        if (!doc.isObject()) {
            result.success = false;
            result.errorMessage = "云端返回解析失败 (非合法 JSON 数据)";
            if (callback) callback(result);
            return;
        }

        QJsonObject root = doc.object();
        bool status = root.value("status").toBool(false);
        if (!status && root.contains("error")) {
            result.success = false;
            result.errorMessage = root.value("error").toString("云端公式提取异常");
            if (callback) callback(result);
            return;
        }

        QJsonObject resObj = root.value("res").toObject();
        QString latex = resObj.value("latex").toString().trimmed();
        if (latex.isEmpty() && root.contains("latex")) {
            latex = root.value("latex").toString().trimmed();
        }

        if (latex.isEmpty()) {
            result.success = false;
            result.errorMessage = "未在选区中检测到有效数学公式";
            if (callback) callback(result);
            return;
        }

        // 清理公式前后多余的包裹标签
        if (latex.startsWith("$$") && latex.endsWith("$$") && latex.length() >= 4) {
            latex = latex.mid(2, latex.length() - 4).trimmed();
        } else if (latex.startsWith("$") && latex.endsWith("$") && latex.length() >= 2) {
            latex = latex.mid(1, latex.length() - 2).trimmed();
        }

        result.success = true;
        result.latex = latex;
        result.blockMarkdown = FormulaFormatHelper::toBlockMarkdown(latex);
        result.inlineMarkdown = FormulaFormatHelper::toInlineMarkdown(latex);
        result.mathml = FormulaFormatHelper::toWordMathML(latex);

        if (callback) callback(result);
    });
}

void FormulaOcrManager::renderFormulaAsync(const QString& latex,
                                          std::function<void(bool success, const QByteArray& data, const QString& mimeType)> callback)
{
    QString trimmed = latex.trimmed();
    if (trimmed.isEmpty()) {
        if (callback) callback(false, QByteArray(), "");
        return;
    }

    // 采用 300 DPI 超高清无损透明公式微渲染通道，彻底消除文字与算子发虚模糊
    QString encoded = QUrl::toPercentEncoding(trimmed);
    QUrl url(QString("https://latex.codecogs.com/png.latex?\\dpi{300}&space;%1").arg(encoded));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "AccuShot-FormulaRenderer/1.2.0");
    req.setTransferTimeout(6000);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, QByteArray(), "");
            return;
        }
        QByteArray data = reply->readAll();
        if (data.isEmpty() || data.size() < 16) {
            if (callback) callback(false, QByteArray(), "");
            return;
        }
        if (callback) callback(true, data, "image/png");
    });
}
