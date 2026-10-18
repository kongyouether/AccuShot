#include "core/ai/ai_service.h"
#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QTextStream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;
 QFile file(QString::fromLocal8Bit(argv[1]));if(!file.open(QIODevice::ReadOnly))return 3;
 const auto match=QRegularExpression("sk-[A-Za-z0-9_-]+").match(QString::fromUtf8(file.readAll()));if(!match.hasMatch())return 4;
 auto profiles=AiService::profiles();AiProfile selected;for(auto& p:profiles)if(p.id=="deepseek"){p.key=match.captured();p.model="deepseek-flash";selected=p;}
 if(selected.id.isEmpty())return 5;
 AiService::run(selected,"Translate into Chinese. Return only the translation: A clear interface helps people work efficiently.",{},&app,[&](AiResult r){
   if(!r.success){QTextStream(stdout)<<r.error<<Qt::endl;app.exit(6);return;}
   QString error;if(!AiService::saveProfiles(profiles,&error)){QTextStream(stdout)<<error<<Qt::endl;app.exit(7);return;}
   QSettings cfg(QSettings::NativeFormat,QSettings::UserScope,"Evan","Evan");cfg.setValue("Translation/Engine","ai:deepseek");cfg.setValue("Snapping/Mode","Window");cfg.setValue("Snapping/EnableSmartSnapping",true);
   QTextStream(stdout)<<"DeepSeek live translation PASS\n"<<r.text<<"\nCredential stored in Windows Credential Manager; default translation=DeepSeek; snap=Window.\n";
   app.exit(0);
 });QTimer::singleShot(100000,&app,[&]{app.exit(8);});return app.exec();
}
