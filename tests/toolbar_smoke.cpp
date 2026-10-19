#include "core/toolbar_config.h"
#include "core/theme_manager.h"
#include "ui/toolbar/floating_toolbar.h"
#include "ui/settings/toolbar_settings_dialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFontDatabase>
#include <QListWidget>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTimer>
#include <QSignalSpy>
#include <algorithm>

namespace {
QString outputDirectory;
QString settingsDirectory;

bool sameEntries(const QList<ToolbarEntry>& left, const QList<ToolbarEntry>& right)
{
    if (left.size() != right.size()) return false;
    for (int i = 0; i < left.size(); ++i)
        if (left[i].action != right[i].action || left[i].visibility != right[i].visibility)
            return false;
    return true;
}

void useIsolatedSettings(const QString& path)
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path);
}

bool checkReloadedSettings()
{
    const auto entries = ToolbarConfig::instance().entries();
    if (entries.first().action != ToolAction::CharEdit) return false;
    const auto rect = std::find_if(entries.cbegin(), entries.cend(), [](const auto& entry) {
        return entry.action == ToolAction::Rect;
    });
    if (rect == entries.cend() || rect->visibility != ToolVisibility::Hidden) return false;
    FloatingToolbar toolbar;
    toolbar.setActiveTool(ToolAction::Rect);
    if (toolbar.currentStrokeWidth() != 7) return false;
    toolbar.setActiveTool(ToolAction::Arrow);
    if (toolbar.currentStrokeWidth() != 2) return false;
    toolbar.setActiveTool(ToolAction::Text);
    return toolbar.currentFontSize() == 31 && toolbar.currentStrokeWidth() == 3;
}
}

class ToolbarSmoke : public QObject
{
    Q_OBJECT
private slots:
    void defaultToolbarAndDisabledActions()
    {
        QVERIFY(sameEntries(ToolbarConfig::instance().entries(), ToolbarConfig::defaults()));
        ThemeManager::instance().setThemeMode(ThemeMode::Dark);
        QWidget canvas;
        canvas.resize(1280, 720);
        FloatingToolbar toolbar(&canvas);
        toolbar.updatePosition(QRect(100, 100, 900, 360), canvas.rect());
        canvas.show(); toolbar.show();
        QTest::qWait(30);
        QCOMPARE(toolbar.height(), 52);
        QVERIFY(toolbar.grab().save(outputDirectory + "/toolbar-default.png"));
        auto* undo = toolbar.findChild<QToolButton*>("tool_undo");
        auto* ocr = toolbar.findChild<QToolButton*>("tool_ocr");
        auto* copy = toolbar.findChild<QToolButton*>("tool_copy_text");
        QVERIFY(undo && ocr && copy);
        QVERIFY(!undo->isEnabled());
        toolbar.setUndoAvailable(true);
        QVERIFY(undo->isEnabled());
        toolbar.setOcrBusy(true);
        QVERIFY(!ocr->isEnabled()); QVERIFY(!copy->isEnabled());
        toolbar.setOcrBusy(false);
        QVERIFY(ocr->isEnabled()); QVERIFY(copy->isEnabled());
    }

    void independentPropertiesAndNarrowPlacement()
    {
        QWidget canvas;
        canvas.resize(1280, 720);
        FloatingToolbar toolbar(&canvas);
        toolbar.updatePosition(QRect(100, 100, 900, 360), canvas.rect());
        canvas.show(); toolbar.show();
        auto* width = toolbar.findChild<QSpinBox*>("strokeWidth");
        auto* fontSize = toolbar.findChild<QSpinBox*>("fontSize");
        QVERIFY(width && fontSize);
        toolbar.setActiveTool(ToolAction::Rect); width->setValue(7);
        toolbar.setActiveTool(ToolAction::Arrow); width->setValue(2);
        toolbar.setActiveTool(ToolAction::Rect); QCOMPARE(toolbar.currentStrokeWidth(), 7);
        toolbar.setActiveTool(ToolAction::Arrow); QCOMPARE(toolbar.currentStrokeWidth(), 2);
        toolbar.setActiveTool(ToolAction::Text);
        const int textStroke = toolbar.currentStrokeWidth();
        fontSize->setValue(31);
        QCOMPARE(toolbar.currentFontSize(), 31);
        QCOMPARE(toolbar.currentStrokeWidth(), textStroke);
        QTest::qWait(30);
        QVERIFY(toolbar.grab().save(outputDirectory + "/toolbar-text.png"));

        canvas.resize(420, 300);
        toolbar.updatePosition(QRect(250, 210, 162, 82), canvas.rect());
        QTest::qWait(30);
        const QRect safeArea = canvas.rect().adjusted(8, 8, -8, -8);
        QVERIFY2(safeArea.contains(toolbar.geometry()), qPrintable(QString("toolbar %1,%2 %3x%4")
            .arg(toolbar.x()).arg(toolbar.y()).arg(toolbar.width()).arg(toolbar.height())));
        QVERIFY(toolbar.y() < 210); // selection is at the bottom: toolbar must move above it.
        QVERIFY(toolbar.findChild<QToolButton*>("toolbarMore")->isVisible());
        int hiddenAlways = 0;
        for (const auto& entry : ToolbarConfig::defaults())
            if (entry.visibility == ToolVisibility::Always
                && !toolbar.findChild<QToolButton*>("tool_" + toolDefinition(entry.action).id)->isVisible())
                ++hiddenAlways;
        QVERIFY(hiddenAlways > 0);
        QVERIFY(toolbar.findChild<QToolButton*>("tool_confirm")->isVisible());
        QVERIFY(toolbar.findChild<QToolButton*>("tool_cancel")->isVisible());
        for (auto* control : toolbar.findChildren<QWidget*>()) {
            if (!control->isVisible()) continue;
            const QRect bounds(control->mapTo(&toolbar, QPoint()), control->size());
            QVERIFY2(toolbar.rect().contains(bounds), qPrintable(control->metaObject()->className()
                + QString(": ") + control->objectName()));
        }
        QVERIFY(canvas.grab().save(outputDirectory + "/toolbar-narrow.png"));

        canvas.resize(1280, 720);
        toolbar.updatePosition(QRect(100, 100, 900, 360), canvas.rect());
        auto* grip = toolbar.findChild<QToolButton*>("toolbarGrip");
        const QPoint beforeDrag = toolbar.pos();
        QTest::mousePress(grip, Qt::LeftButton, Qt::NoModifier, grip->rect().center());
        QTest::mouseMove(grip, grip->rect().center() + QPoint(30, 20));
        QTest::mouseRelease(grip, Qt::LeftButton);
        QVERIFY(toolbar.pos() != beforeDrag);
        const QPoint draggedPosition = toolbar.pos();
        toolbar.updatePosition(QRect(300, 30, 700, 300), canvas.rect());
        QCOMPARE(toolbar.pos(), draggedPosition);
    }

    void moreMenuGridKeyboardAndDisabledItems()
    {
        QWidget canvas;
        canvas.resize(1280, 720);
        FloatingToolbar toolbar(&canvas);
        toolbar.updatePosition(QRect(100, 100, 900, 360), canvas.rect());
        canvas.show(); toolbar.show();
        QSignalSpy actions(&toolbar, &FloatingToolbar::actionTriggered);
        bool navigationChecked = false;
        QTimer::singleShot(30, &toolbar, [&] {
            auto* menu = toolbar.findChild<QMenu*>("toolbarMoreMenu");
            QVERIFY(menu);
            const auto closePopup = qScopeGuard([menu] { menu->close(); });
            auto* first = menu->findChild<QToolButton*>("more_ocr");
            auto* right = menu->findChild<QToolButton*>("more_screen_translate");
            auto* middleLeft = menu->findChild<QToolButton*>("more_formula_ocr");
            auto* middleRight = menu->findChild<QToolButton*>("more_markdown");
            auto* last = menu->findChild<QToolButton*>("more_char_edit");
            auto* settings = menu->findChild<QToolButton*>("more_configure");
            QVERIFY(first && right && middleLeft && middleRight && last && settings);
            first->setFocus();
            QTest::keyClick(first, Qt::Key_Up);
            QCOMPARE(QApplication::focusWidget(), first); // top boundary stays put.
            QTest::keyClick(first, Qt::Key_Right);
            QCOMPARE(QApplication::focusWidget(), right);
            QTest::keyClick(right, Qt::Key_Down);
            QCOMPARE(QApplication::focusWidget(), middleRight);
            QTest::keyClick(middleRight, Qt::Key_Down);
            QCOMPARE(QApplication::focusWidget(), last); // odd final row has no right cell.
            QTest::keyClick(last, Qt::Key_Down);
            QCOMPARE(QApplication::focusWidget(), settings); // spanning footer is reachable from left.
            QTest::keyClick(settings, Qt::Key_Down);
            QCOMPARE(QApplication::focusWidget(), settings); // no modulo wrap to first row.
            QTest::keyClick(settings, Qt::Key_Up);
            QCOMPARE(QApplication::focusWidget(), last);
            QTest::keyClick(last, Qt::Key_Right);
            QCOMPARE(QApplication::focusWidget(), last);
            middleLeft->setEnabled(false);
            first->setFocus();
            QTest::keyClick(first, Qt::Key_Down);
            QCOMPARE(QApplication::focusWidget(), middleRight); // skips a disabled cell.
            middleRight->setEnabled(false);
            first->setFocus();
            QTest::keyClick(first, Qt::Key_Down);
            QCOMPARE(QApplication::focusWidget(), last); // skips a fully disabled row.
            QTest::keyClick(last, Qt::Key_Escape);
            QVERIFY(!menu->isVisible());
            navigationChecked = true;
        });
        toolbar.findChild<QToolButton*>("toolbarMore")->click();
        QVERIFY(navigationChecked);
        QCOMPARE(actions.count(), 0); // moving and cancelling must not activate a tool.

        bool activationChecked = false;
        QTimer::singleShot(30, &toolbar, [&] {
            auto* menu = toolbar.findChild<QMenu*>("toolbarMoreMenu");
            QVERIFY(menu);
            const auto closePopup = qScopeGuard([menu] { menu->close(); });
            auto* first = menu->findChild<QToolButton*>("more_ocr");
            auto* right = menu->findChild<QToolButton*>("more_screen_translate");
            QVERIFY(first && right);
            first->setFocus();
            QTest::keyClick(first, Qt::Key_Right);
            QCOMPARE(QApplication::focusWidget(), right);
            QTest::keyClick(right, Qt::Key_Return);
            QVERIFY(!menu->isVisible());
            activationChecked = true;
        });
        toolbar.findChild<QToolButton*>("toolbarMore")->click();
        QVERIFY(activationChecked);
        QCOMPARE(actions.count(), 1);
        QCOMPARE(actions.at(0).at(0).value<ToolAction>(), ToolAction::ScreenTranslate);
    }

    void configurationCancelDefaultAcceptAndReload()
    {
        const auto defaults = ToolbarConfig::defaults();
        {
            ToolbarSettingsDialog dialog;
            dialog.show(); QTest::qWait(30);
            QVERIFY(dialog.grab().save(outputDirectory + "/toolbar-settings.png"));
            auto* list = dialog.findChild<QListWidget*>();
            QVERIFY(list);
            auto* visibility = list->itemWidget(list->item(0))->findChild<QComboBox*>();
            QVERIFY(visibility);
            visibility->setCurrentIndex(visibility->findData(static_cast<int>(ToolVisibility::Hidden)));
            dialog.reject();
            QVERIFY(sameEntries(ToolbarConfig::instance().entries(), defaults));
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
            QVERIFY(!settings.contains("Toolbar/entries/size"));
        }
        {
            ToolbarSettingsDialog dialog;
            auto* list = dialog.findChild<QListWidget*>();
            // InternalMove uses the item model move path; preserve its row widget and combo.
            auto* firstItem = list->item(0);
            auto* firstWidget = list->itemWidget(firstItem);
            auto* firstCombo = firstWidget->findChild<QComboBox*>();
            QVERIFY(list->model()->moveRow(QModelIndex(), 0, QModelIndex(), 3));
            QCOMPARE(list->item(2), firstItem);
            QCOMPARE(list->itemWidget(firstItem), firstWidget);
            QCOMPARE(firstWidget->findChild<QComboBox*>(), firstCombo);
            QVERIFY(list->model()->moveRow(QModelIndex(), list->count() - 1, QModelIndex(), 0));
            for (int i = 0; i < list->count(); ++i) {
                auto* item = list->item(i);
                if (item->data(Qt::UserRole).toInt() != static_cast<int>(ToolAction::Rect)) continue;
                auto* visibility = list->itemWidget(item)->findChild<QComboBox*>();
                visibility->setCurrentIndex(visibility->findData(static_cast<int>(ToolVisibility::Hidden)));
            }
            dialog.accept();
        }
        const auto custom = ToolbarConfig::instance().entries();
        QCOMPARE(custom.first().action, ToolAction::CharEdit);
        {
            ToolbarSettingsDialog dialog;
            auto* reset = [&]() -> QPushButton* {
                for (auto* button : dialog.findChildren<QPushButton*>())
                    if (button->text() == QString::fromUtf8("恢复默认")) return button;
                return nullptr;
            }();
            QVERIFY(reset);
            reset->click();
            auto* list = dialog.findChild<QListWidget*>();
            QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), static_cast<int>(ToolAction::Rect));
            QVERIFY(sameEntries(ToolbarConfig::instance().entries(), custom));
            dialog.reject();
            QVERIFY(sameEntries(ToolbarConfig::instance().entries(), custom));
        }
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot").sync();
        QProcess reload;
        reload.start(QCoreApplication::applicationFilePath(), {"--verify-persistence", settingsDirectory});
        QVERIFY(reload.waitForFinished(15000));
        QVERIFY2(reload.exitStatus() == QProcess::NormalExit && reload.exitCode() == 0,
                 reload.readAllStandardError().constData());
    }
};

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    // The offscreen Windows plugin has no system font database of its own.
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyhbd.ttc");
    application.setFont(QFont("Microsoft YaHei", 10));
    const QStringList args = application.arguments();
    if (args.size() == 3 && args.at(1) == "--verify-persistence") {
        useIsolatedSettings(args.at(2));
        return checkReloadedSettings() ? 0 : 1;
    }
    QTemporaryDir isolatedSettings;
    if (!isolatedSettings.isValid()) return 2;
    settingsDirectory = isolatedSettings.path();
    useIsolatedSettings(settingsDirectory); // before any application singleton is constructed.
    outputDirectory = args.size() == 3 && args.at(1) == "--output" ? args.at(2)
        : QDir::currentPath() + "/screenshots";
    if (!QDir().mkpath(outputDirectory)) return 2;
    ToolbarSmoke smoke;
    return QTest::qExec(&smoke, QStringList{args.first(), "-o", outputDirectory + "/results.txt,txt"});
}

#include "toolbar_smoke.moc"
