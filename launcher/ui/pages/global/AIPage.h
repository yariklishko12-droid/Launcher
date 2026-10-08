// SPDX-License-Identifier: GPL-3.0-only
// Settings page for the AI assistant (Gemini) and interface animations.
#pragma once

#include <QWidget>

#include "ui/pages/BasePage.h"

class QCheckBox;
class QComboBox;
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

    QLineEdit* m_key;
    QPushButton* m_showKey;
    QComboBox* m_model;
    QCheckBox* m_offerOnCrash;
    QCheckBox* m_animations;
    QPushButton* m_testButton;
    QLabel* m_testResult;
    GeminiClient* m_client;
};
