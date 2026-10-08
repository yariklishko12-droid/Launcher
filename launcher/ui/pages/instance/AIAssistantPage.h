// SPDX-License-Identifier: GPL-3.0-only
// AI assistant page: diagnoses crashes and builds mod sets with Gemini + Modrinth.
#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include "ai/ModrinthInstaller.h"
#include "ui/pages/BasePage.h"

class MinecraftInstance;
class GeminiClient;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTextBrowser;
class QFrame;

class AIAssistantPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit AIAssistantPage(MinecraftInstance* instance, QWidget* parent = nullptr);
    ~AIAssistantPage() override;

    QString displayName() const override { return tr("ИИ-помощник"); }
    QIcon icon() const override
    {
        auto icon = QIcon::fromTheme("bug");
        return icon.isNull() ? QIcon::fromTheme("help") : icon;
    }
    QString id() const override { return "ai-assistant"; }
    QString helpPage() const override { return QString(); }
    void retranslate() override {}

    /** Called after a crash: analyses immediately if the page is alive, otherwise on next open. */
    static void requestAutoAnalyze(const QString& instanceId);

   protected:
    void openedImpl() override;

   private:
    void buildUi();
    void refreshHeader();
    void setBusy(bool busy, const QString& status = QString());

    // repair
    void analyze();
    void onDiagnosis(const QString& text);
    void applySelectedFixes();
    bool setModEnabled(const QString& fileName, bool enabled);

    // build
    void suggestMods();
    void onSuggestions(const QString& text);
    void onResolved(const QList<ModCandidate>& candidates);
    void installSelected();
    void startInstall(QList<ModCandidate> candidates, const QString& title);
    void onInstallFinished(const QStringList& installed, const QStringList& skipped, const QStringList& errors);

    // context
    QString mcVersion() const;
    QString loader() const;
    QString modListText() const;
    QString collectLogs() const;

    enum class Mode { Idle, Diagnose, Suggest };
    Mode m_mode = Mode::Idle;
    void onAiFinished(const QString& text);
    void onAiFailed(const QString& error);
    void recreateInstaller();

    MinecraftInstance* m_instance;
    GeminiClient* m_ai;
    ModrinthInstaller* m_installer = nullptr;
    QList<ModCandidate> m_resolved;
    QList<QJsonObject> m_pendingInstallActions;

    QLabel* m_header;
    QFrame* m_keyBanner;
    QTabWidget* m_tabs;
    QPushButton* m_analyzeButton;
    QTextBrowser* m_diagnosis;
    QListWidget* m_fixList;
    QPushButton* m_applyButton;
    QPlainTextEdit* m_request;
    QPushButton* m_suggestButton;
    QTextBrowser* m_summary;
    QListWidget* m_modList;
    QPushButton* m_installButton;
    QLabel* m_status;
    QProgressBar* m_progress;
    QPushButton* m_cancelButton;

    static QList<QPointer<AIAssistantPage>> s_pages;
    static QSet<QString> s_pendingAutoAnalyze;
};
