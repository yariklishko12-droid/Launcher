// SPDX-License-Identifier: GPL-3.0-only
// Settings page for the AI assistant (choice of AI service, key, model) and interface animations.
#pragma once

#include <QJsonObject>
#include <QWidget>

#include "ui/pages/BasePage.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class GeminiClient;

class AIPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit AIPage(QWidget* parent = nullptr);
    ~AIPage() override = default;

    QString displayName() const override { return tr("ИИ-помощник"); }
    QIcon icon() const override
    {
        auto icon = QIcon::fromTheme("bug");
        return icon.isNull() ? QIcon::fromTheme("help") : icon;
    }
    QString id() const override { return "ai"; }
    QString helpPage() const override { return QString(); }
    bool apply() override;
    void retranslate() override {}

   private:
    void loadSettings();
    void testKey();
    void refreshModels(bool quiet);
    void onProviderChanged();
    void storeEdits();  // widgets -> m_edits[m_shownProvider]
    void showProvider(const QString& id);
    QString selectedModel() const;

    QFormLayout* m_form;
    QComboBox* m_provider;
    QLabel* m_providerHint;
    QLineEdit* m_url;
    QLineEdit* m_key;
    QPushButton* m_showKey;
    QComboBox* m_model;
    QPushButton* m_refreshModels;
    QCheckBox* m_offerOnCrash;
    QCheckBox* m_animations;
    QPushButton* m_testButton;
    QLabel* m_testResult;
    GeminiClient* m_client;
    GeminiClient* m_lister;  // separate, so a test request does not cancel loading the model list

    QString m_shownProvider;
    QJsonObject m_edits;  // unsaved values per provider: { key, model, url }
};
