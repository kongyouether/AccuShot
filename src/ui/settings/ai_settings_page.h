#pragma once
#include "../../core/ai/ai_service.h"
#include "../../core/translation/translation_plugin_manager.h"
#include "../../core/formula/formula_ocr_manager.h"
#include "../themed_combo_box.h"
#include "settings_switch.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPointer>
#include <QUuid>
#include <QPainter>
#include <QMenu>
#include <QFrame>

// Draft editor: tests use draft requests; only Settings/Save persists configuration.
class AiSettingsPage : public QWidget {
public:
    explicit AiSettingsPage(QWidget* parent=nullptr) : QWidget(parent), profiles(AiService::profiles()) {
        profiles.removeIf([](const AiProfile& p){return p.protocol=="formula";});
        setObjectName("aiServices");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(24);
        auto* description=new QLabel("这里只管理可共用的在线账号。请到文字识别、翻译、公式识别或 Markdown 页选择方案、模型和指令。密钥保存在 Windows 凭据管理器。",this);
        description->setWordWrap(true);description->setObjectName("rowDesc");layout->addWidget(description);
        auto* row=new QHBoxLayout;selector=new ThemedComboBox(this);selector->setObjectName("aiProfile");row->addWidget(selector,1);
        auto* add=new QPushButton("添加账号",this);auto* remove=new QPushButton("删除",this);row->addWidget(add);row->addWidget(remove);layout->addLayout(row);
        auto* block=new QFrame(this);block->setObjectName("settingsCard");block->setAttribute(Qt::WA_StyledBackground);
        auto* form=new QFormLayout(block);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setContentsMargins(20,18,20,18);form->setHorizontalSpacing(24);form->setVerticalSpacing(18);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        name=new QLineEdit(this);name->setObjectName("aiName");form->addRow("账号名称",name);
        protocol=new ThemedComboBox(this);protocol->addItem("OpenAI 兼容","openai");protocol->addItem("Claude Messages","claude");protocol->addItem("Gemini","gemini");protocol->addItem("Mathpix","mathpix");form->addRow("接口协议",protocol);
        endpoint=new QLineEdit(this);endpoint->setObjectName("aiEndpoint");form->addRow("服务地址",endpoint);
        key=new QLineEdit(this);key->setObjectName("aiKey");key->setEchoMode(QLineEdit::Password);form->addRow("API Key",key);
        headers=new QLineEdit(this);headers->setPlaceholderText("可选 JSON，例如 Mathpix 的 app_id");form->addRow("附加请求头",headers);
        vision=new SettingsSwitch("允许调用图片模型",this);
        vision->setToolTip("具体模型在文字识别、翻译、公式识别或 Markdown 页选择。");form->addRow(vision);
        layout->addWidget(block);
        for(const auto& p:profiles){selector->addItem(p.name,p.id);rawHeaders[p.id]=p.headers.isEmpty()?QString():QString::fromUtf8(QJsonDocument(p.headers).toJson(QJsonDocument::Compact));}
        connect(selector,&QComboBox::currentIndexChanged,this,[this](int index){stash();current=index;load();});
        auto* presetMenu=new QMenu(add);add->setMenu(presetMenu);
        auto choices=AiService::presets();AiProfile custom;custom.name="自定义服务";choices.prepend(custom);
        for(const auto& preset:choices)if(preset.id!="qwen-markdown")presetMenu->addAction(preset.name,this,[this,preset]{stash();auto p=preset;p.id=QUuid::createUuid().toString(QUuid::WithoutBraces);profiles.append(p);selector->addItem(p.name,p.id);selector->setCurrentIndex(profiles.size()-1);});
        connect(remove,&QPushButton::clicked,this,[this]{if(current<0)return;int index=current;current=-1;profiles.removeAt(index);selector->removeItem(index);current=selector->currentIndex();load();});
        current=selector->currentIndex();load();
        layout->addStretch();
    }
    void selectAccount(const QString& id){const int i=selector->findData(id);if(i>=0)selector->setCurrentIndex(i);}
    QList<AiProfile> draftProfiles(){stash();return profiles;}
    bool save(QString* error) {
        stash();
        for(const auto& p:profiles) {
            if(!rawHeaders.value(p.id).trimmed().isEmpty()&&!QJsonDocument::fromJson(rawHeaders.value(p.id).toUtf8()).isObject()){*error=p.name+"：附加请求头必须为 JSON 对象。";return false;}
            if(p.name.trimmed().isEmpty()){*error="服务名称不能为空。";return false;}
            if(!p.headers.isEmpty())for(auto it=p.headers.begin();it!=p.headers.end();++it)
                if(!it.value().isString()||it.key().contains('\n')||it.key().contains('\r')||it.value().toString().contains('\n')||it.value().toString().contains('\r')){*error="附加请求头必须是字符串，且不能包含换行。";return false;}
        }
        if(!headers->text().trimmed().isEmpty()&&!QJsonDocument::fromJson(headers->text().toUtf8()).isObject()){*error="附加请求头必须为 JSON 对象。";return false;}
        return AiService::saveProfiles(profiles,error);
    }
private:
    void stash(){if(current<0||current>=profiles.size())return;auto& p=profiles[current];p.name=name->text().trimmed();p.protocol=protocol->currentData().toString();p.endpoint=endpoint->text().trimmed();p.key=key->text().trimmed();rawHeaders[p.id]=headers->text();p.headers=QJsonDocument::fromJson(headers->text().toUtf8()).object();p.vision=vision->isChecked();selector->setItemText(current,p.name);}
    void load(){const auto p=current>=0?profiles[current]:AiProfile{};name->setText(p.name);protocol->setCurrentIndex(protocol->findData(p.protocol));endpoint->setText(p.endpoint);key->setText(p.key);headers->setText(rawHeaders.value(p.id));vision->setChecked(p.vision);}
    QMap<QString,QString> rawHeaders;
    QList<AiProfile> profiles;int current=-1;
    QComboBox *selector,*protocol;QLineEdit *name,*endpoint,*key,*headers;QCheckBox* vision;
};
