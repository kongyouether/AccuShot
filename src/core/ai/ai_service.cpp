#include "ai_service.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QMap>
#include <QHostAddress>
#include <windows.h>
#include <wincred.h>
namespace {
QSettings settings() { return QSettings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot"); }
QMap<QString,QString> testSecrets;
QString imageData(const QImage& image) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer,"PNG");
    return QString::fromLatin1(bytes.toBase64());
}
}
QString AiService::secret(const QString& id) {
    if (QSettings::defaultFormat() != QSettings::NativeFormat) return testSecrets.value(id);
    const auto target = ("AccuShot/AI/" + id).toStdWString(); PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) return {};
    const QString result = QString::fromUtf8(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
    CredFree(cred); return result;
}
bool AiService::isLocalProfile(const AiProfile& profile) {
    const auto host=QUrl(profile.endpoint).host().toLower();
    return profile.protocol=="formula" || host=="localhost" || host=="0.0.0.0" || QHostAddress(host).isLoopback();
}
bool AiService::saveSecret(const QString& id, const QString& key) {
    if (QSettings::defaultFormat() != QSettings::NativeFormat) { testSecrets[id] = key; return true; }
    auto target = ("AccuShot/AI/" + id).toStdWString();
    if (key.isEmpty()) { CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0); return true; }
    const auto bytes = key.toUtf8(); CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC; cred.TargetName = target.data(); cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.CredentialBlobSize = DWORD(bytes.size()); cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(bytes.constData()));
    return CredWriteW(&cred, 0) != FALSE;
}
QList<AiProfile> AiService::presets() {
    QList<AiProfile> list;
    const auto add = [&](QString id, QString name, QString protocol, QString endpoint, QString model, bool vision) {
        AiProfile p; p.id=id; p.name=name; p.protocol=protocol; p.endpoint=endpoint; p.model=model; p.vision=vision; list.append(p);
    };
    add("deepseek","DeepSeek","openai","https://api.deepseek.com","deepseek-flash",true);
    add("openai","OpenAI","openai","https://api.openai.com/v1","",true);
    add("qwen","通义千问","openai","https://dashscope.aliyuncs.com/compatible-mode/v1","",false);
    add("qwen-markdown","通义千问 · Markdown","openai","https://dashscope.aliyuncs.com/compatible-mode/v1","qwen3-vl-plus",true);
    add("gemini","Gemini","gemini","https://generativelanguage.googleapis.com/v1beta","",true);
    add("claude","Claude","claude","https://api.anthropic.com/v1","",true);
    add("mathpix","Mathpix","mathpix","https://api.mathpix.com/v3/text","",true);
    return list;
}
QList<AiProfile> AiService::profiles() {
    const QString json=settings().value("AI/Profiles").toString();
    if (json.isEmpty()) {auto list=presets();for(auto& p:list)p.key=secret(p.id);return list;}
    QList<AiProfile> result;
    for (const auto& v : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const auto o=v.toObject(); AiProfile p;
        p.id=o["id"].toString(); p.name=o["name"].toString(); p.protocol=o["protocol"].toString();
        p.endpoint=o["endpoint"].toString(); p.model=o["model"].toString(); p.vision=o["vision"].toBool();
        p.headers=QJsonDocument::fromJson(secret(p.id+"/headers").toUtf8()).object();
        p.translationPrompt=o["translationPrompt"].toString(p.translationPrompt);
        p.formulaPrompt=o["formulaPrompt"].toString(p.formulaPrompt);
        p.key=secret(p.id); result.append(p);
    }
    return result;
}
AiProfile AiService::profile(const QString& id) { for(auto p:profiles()) if(p.id==id) {p.key=secret(id);return p;} return {}; }
AiProfile AiService::forTask(const QString& id, const QString& task) {
    auto p=profile(id);
    p.model=settings().value("AI/TaskModels/"+task+"/"+id,p.model).toString();
    return p;
}
QString AiService::taskPrompt(const QString& id,const QString& task,const QString& fallback) {
    return settings().value("AI/TaskPrompts/"+task+"/"+id,fallback).toString();
}
bool AiService::saveProfiles(const QList<AiProfile>& profiles, QString* error) {
    const auto previous=AiService::profiles();
    QJsonArray array;
    for (const auto& p:profiles) {
        if(!saveSecret(p.id,p.key) || !saveSecret(p.id+"/headers",QString::fromUtf8(QJsonDocument(p.headers).toJson(QJsonDocument::Compact)))) { if(error)*error="无法写入 Windows 凭据，请检查当前用户权限。"; return false; }
        array.append(QJsonObject{{"id",p.id},{"name",p.name},{"protocol",p.protocol},{"endpoint",p.endpoint},{"model",p.model},
            {"vision",p.vision},{"translationPrompt",p.translationPrompt},{"formulaPrompt",p.formulaPrompt}});
    }
    auto cfg=settings(); cfg.setValue("AI/Profiles",QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact))); cfg.sync();
    if(cfg.status()!=QSettings::NoError) {if(error)*error="服务配置保存失败。";return false;}
    for(const auto& old:previous){bool retained=false;for(const auto& p:profiles)if(p.id==old.id)retained=true;if(!retained){saveSecret(old.id,{});saveSecret(old.id+"/headers",{});}}
    return true;
}
QNetworkRequest AiService::request(const AiProfile& p) {
    QString url=p.endpoint.trimmed(); while(url.endsWith('/'))url.chop(1);
    if(p.protocol=="openai" && !url.endsWith("/chat/completions")) url+="/chat/completions";
    if(p.protocol=="claude" && !url.endsWith("/messages")) url+="/messages";
    if(p.protocol=="gemini") url+="/models/"+QString::fromLatin1(QUrl::toPercentEncoding(p.model))+":generateContent";
    QNetworkRequest req{QUrl(url)}; req.setTransferTimeout(90000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    if(p.protocol=="claude") { req.setRawHeader("x-api-key",p.key.toUtf8());req.setRawHeader("anthropic-version","2023-06-01"); }
    else if(p.protocol=="gemini")req.setRawHeader("x-goog-api-key",p.key.toUtf8());
    else if(p.protocol=="mathpix")req.setRawHeader("app_key",p.key.toUtf8());
    else if(!p.key.isEmpty())req.setRawHeader("Authorization","Bearer "+p.key.toUtf8());
    for(auto it=p.headers.begin();it!=p.headers.end();++it)req.setRawHeader(it.key().toUtf8(),it.value().toString().toUtf8());
    return req;
}
QJsonObject AiService::payload(const AiProfile& p,const QString& prompt,const QImage& image) {
    const QString data=image.isNull()?QString():imageData(image);
    if(p.protocol=="mathpix")return {{"src","data:image/png;base64,"+data},{"formats",QJsonArray{"text"}},{"math_inline_delimiters",QJsonArray{"$","$"}}};
    if(p.protocol=="formula")return {{"image",data},{"prompt",prompt}};
    if(p.protocol=="gemini") {
        QJsonArray parts{QJsonObject{{"text",prompt}}};
        if(!data.isEmpty())parts.append(QJsonObject{{"inline_data",QJsonObject{{"mime_type","image/png"},{"data",data}}}});
        return {{"contents",QJsonArray{QJsonObject{{"role","user"},{"parts",parts}}}}};
    }
    QJsonArray content;
    content.append(QJsonObject{{"type","text"},{"text",prompt}});
    if(!data.isEmpty()) {
        if(p.protocol=="claude")content.append(QJsonObject{{"type","image"},{"source",QJsonObject{{"type","base64"},{"media_type","image/png"},{"data",data}}}});
        else content.append(QJsonObject{{"type","image_url"},{"image_url",QJsonObject{{"url","data:image/png;base64,"+data}}}});
    }
    QJsonObject body{{"model",p.model},{"messages",QJsonArray{QJsonObject{{"role","user"},{"content",content}}}}};
    if(p.protocol=="claude")body["max_tokens"]=4096;
    return body;
}
AiResult AiService::parse(const QString& protocol,const QByteArray& bytes) {
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(bytes,&error); AiResult r;
    if(error.error!=QJsonParseError::NoError || !doc.isObject()) {r.error="服务返回的不是有效 JSON。";return r;}
    const auto o=doc.object();
    if(protocol=="openai")r.truncated=o["choices"].toArray().at(0).toObject()["finish_reason"].toString()=="length";
    else if(protocol=="claude")r.truncated=o["stop_reason"].toString()=="max_tokens";
    else if(protocol=="gemini")r.truncated=o["candidates"].toArray().at(0).toObject()["finishReason"].toString()=="MAX_TOKENS";
    if(protocol=="openai")r.text=o["choices"].toArray().at(0).toObject()["message"].toObject()["content"].toString();
    else if(protocol=="mathpix")r.text=o["text"].toString();
    else if(protocol=="formula")r.text=o["latex"].toString();
    else {
        const auto parts=protocol=="claude"?o["content"].toArray():o["candidates"].toArray().at(0).toObject()["content"].toObject()["parts"].toArray();
        for(const auto& part:parts)r.text+=part.toObject()["text"].toString();
    }
    r.text=r.text.trimmed(); r.success=!r.text.isEmpty();
    if(!r.success)r.error="服务未返回有效结果，请检查模型、接口协议与响应格式。";
    return r;
}
QNetworkReply* AiService::run(const AiProfile& p,const QString& prompt,const QImage& image,QObject* context,std::function<void(AiResult)> callback) {
    if(p.protocol=="formula") {callback({false,{},"本地公式服务已移至设置 → 插件与模型，请安装并选择对应插件。"});return nullptr;}
    const QUrl url(p.endpoint);
    if(!url.isValid() || url.host().isEmpty() || (url.scheme()!="https" && !(url.scheme()=="http" && (url.host()=="localhost" || url.host()=="127.0.0.1" || url.host()=="::1")))) {
        callback({false,{},"请使用 HTTPS 服务地址；本机服务允许 HTTP。"});return nullptr;
    }
    if(!image.isNull()&&!p.vision) {callback({false,{},"此配置未启用图片输入，请选择视觉模型。"});return nullptr;}
    if(p.model.trimmed().isEmpty()&&p.protocol!="mathpix"&&p.protocol!="formula") {callback({false,{},"请填写模型名称。"});return nullptr;}
    auto* manager=new QNetworkAccessManager(context);
    auto* reply=manager->post(request(p),QJsonDocument(payload(p,prompt,image)).toJson(QJsonDocument::Compact));
    QObject::connect(reply,&QNetworkReply::finished,context,[reply,manager,p,callback] {
        AiResult result;
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(reply->error()!=QNetworkReply::NoError || status<200 || status>=300) {
            if(reply->error()==QNetworkReply::OperationCanceledError)result.error="请求已取消或超时。";
            else result.error=QString("请求失败（HTTP %1）：%2").arg(status).arg(status==401||status==403?"请检查密钥和访问权限":status==429?"服务限流或额度不足":"请检查服务地址、模型和网络");
        } else result=parse(p.protocol,reply->readAll());
        reply->deleteLater();manager->deleteLater();callback(result);
    });
    return reply;
}
