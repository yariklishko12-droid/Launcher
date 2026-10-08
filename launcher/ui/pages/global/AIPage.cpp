// SPDX-License-Identifier: GPL-3.0-only
#include "AIPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Application.h"
#include "ai/GeminiClient.h"
#include "settings/SettingsObject.h"

AIPage::AIPage(QWidget* parent) : QWidget(parent)
{
    m_client = new GeminiClient(this);

    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // --- Gemini
    auto aiBox = new QGroupBox(tr("Нейросеть Gemini"), this);
    auto form = new QFormLayout(aiBox);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto intro = new QLabel(tr("Помощник чинит вылетающие сборки и подбирает моды. Нужен бесплатный ключ: "
                               "<a href=\"https://aistudio.google.com/apikey\">получить в Google AI Studio</a>."),
                            aiBox);
    intro->setWordWrap(true);
    intro->setOpenExternalLinks(true);
    form->addRow(intro);

    m_key = new QLineEdit(aiBox);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText("AIza…");
    m_showKey = new QPushButton(tr("Показать"), aiBox);
    m_showKey->setCheckable(true);
    connect(m_showKey, &QPushButton::toggled, this, [this](bool on) {
        m_key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        m_showKey->setText(on ? tr("Скрыть") : tr("Показать"));
    });
    auto keyRow = new QHBoxLayout();
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(m_showKey);
    form->addRow(tr("API-ключ:"), keyRow);

    m_model = new QComboBox(aiBox);
    m_model->setEditable(true);
    m_model->addItem(tr("gemini-2.5-flash — быстрый, бесплатный лимит больше"), "gemini-2.5-flash");
    m_model->addItem(tr("gemini-2.5-pro — умнее, но медленнее"), "gemini-2.5-pro");
    m_model->addItem(tr("gemini-2.5-flash-lite — самый быстрый"), "gemini-2.5-flash-lite");
    form->addRow(tr("Модель:"), m_model);

    m_testButton = new QPushButton(tr("Проверить ключ"), aiBox);
    m_testResult = new QLabel(aiBox);
    m_testResult->setWordWrap(true);
    auto testRow = new QHBoxLayout();
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_testResult, 1);
    form->addRow(testRow);
    connect(m_testButton, &QPushButton::clicked, this, &AIPage::testKey);
    connect(m_client, &GeminiClient::finished, this, [this](const QString&) {
        m_testButton->setEnabled(true);
        m_testResult->setText(tr("<span style='color:#3ecf8e'>✔ Ключ работает</span>"));
    });
    connect(m_client, &GeminiClient::failed, this, [this](const QString& error) {
        m_testButton->setEnabled(true);
        m_testResult->setText(QStringLiteral("<span style='color:#ff6b6b'>✖ %1</span>").arg(error.toHtmlEscaped()));
    });

    m_offerOnCrash = new QCheckBox(tr("Предлагать помощь ИИ, если игра вылетела"), aiBox);
    form->addRow(m_offerOnCrash);

    auto privacy = new QLabel(tr("<small>При анализе в Google отправляются: версия игры, список модов и последние логи. "
                                 "Токены входа и пароли из логов вырезаются. Ключ хранится в файле настроек лаунчера.</small>"),
                              aiBox);
    privacy->setWordWrap(true);
    form->addRow(privacy);
    root->addWidget(aiBox);

    // --- Interface
    auto uiBox = new QGroupBox(tr("Интерфейс"), this);
    auto uiLayout = new QVBoxLayout(uiBox);
    m_animations = new QCheckBox(tr("Плавные анимации (появление окон и переходы между страницами)"), uiBox);
    uiLayout->addWidget(m_animations);
    auto themeHint = new QLabel(tr("<small>Тему «Pixel Dark» можно выбрать в разделе «Оформление».</small>"), uiBox);
    uiLayout->addWidget(themeHint);
    root->addWidget(uiBox);

    root->addStretch(1);
    loadSettings();
}

void AIPage::loadSettings()
{
    auto s = APPLICATION->settings();
    m_key->setText(s->get("GeminiApiKey").toString());
    const auto model = GeminiClient::currentModel();
    const int index = m_model->findData(model);
    if (index >= 0)
        m_model->setCurrentIndex(index);
    else
        m_model->setEditText(model);
    m_offerOnCrash->setChecked(s->get("AIOfferOnCrash").toBool());
    m_animations->setChecked(s->get("UIAnimations").toBool());
}

bool AIPage::apply()
{
    auto s = APPLICATION->settings();
    s->set("GeminiApiKey", m_key->text().trimmed());
    QString model = m_model->currentData().toString();
    if (model.isEmpty() || m_model->currentText() != m_model->itemText(m_model->currentIndex()))
        model = m_model->currentText().section(' ', 0, 0).trimmed();
    s->set("GeminiModel", model);
    s->set("AIOfferOnCrash", m_offerOnCrash->isChecked());
    s->set("UIAnimations", m_animations->isChecked());
    return true;
}

void AIPage::testKey()
{
    apply();
    m_testButton->setEnabled(false);
    m_testResult->setText(tr("Проверяю…"));
    m_client->generate(QStringLiteral("You are a connectivity test."), QStringLiteral("Reply with the single word OK."), false);
}
