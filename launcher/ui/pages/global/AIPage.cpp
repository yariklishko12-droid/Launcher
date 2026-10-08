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
    m_lister = new GeminiClient(this);

    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // --- AI service
    auto aiBox = new QGroupBox(tr("Нейросеть"), this);
    m_form = new QFormLayout(aiBox);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto intro = new QLabel(tr("Помощник чинит вылетающие сборки и подбирает моды. Выберите сервис ИИ и вставьте его ключ. "
                               "Ключи разных сервисов запоминаются отдельно, переключаться можно в любой момент."),
                            aiBox);
    intro->setWordWrap(true);
    m_form->addRow(intro);

    m_provider = new QComboBox(aiBox);
    for (const auto& p : GeminiClient::providers())
        m_provider->addItem(QStringLiteral("%1 — %2").arg(p.name, p.note), p.id);
    m_form->addRow(tr("Сервис:"), m_provider);

    m_providerHint = new QLabel(aiBox);
    m_providerHint->setWordWrap(true);
    m_providerHint->setOpenExternalLinks(true);
    m_form->addRow(m_providerHint);

    m_url = new QLineEdit(aiBox);
    m_url->setPlaceholderText("http://localhost:11434/v1");
    m_form->addRow(tr("Адрес сервера:"), m_url);

    m_key = new QLineEdit(aiBox);
    m_key->setEchoMode(QLineEdit::Password);
    m_showKey = new QPushButton(tr("Показать"), aiBox);
    m_showKey->setCheckable(true);
    connect(m_showKey, &QPushButton::toggled, this, [this](bool on) {
        m_key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        m_showKey->setText(on ? tr("Скрыть") : tr("Показать"));
    });
    auto keyRow = new QHBoxLayout();
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(m_showKey);
    m_form->addRow(tr("API-ключ:"), keyRow);

    m_model = new QComboBox(aiBox);
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    m_refreshModels = new QPushButton(tr("Обновить список"), aiBox);
    m_refreshModels->setToolTip(tr("Загрузить список моделей, доступных по этому ключу"));
    connect(m_refreshModels, &QPushButton::clicked, this, [this]() { refreshModels(false); });
    auto modelRow = new QHBoxLayout();
    modelRow->addWidget(m_model, 1);
    modelRow->addWidget(m_refreshModels);
    m_form->addRow(tr("Модель:"), modelRow);

    m_testButton = new QPushButton(tr("Проверить"), aiBox);
    m_testResult = new QLabel(aiBox);
    m_testResult->setWordWrap(true);
    auto testRow = new QHBoxLayout();
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_testResult, 1);
    m_form->addRow(testRow);
    connect(m_testButton, &QPushButton::clicked, this, &AIPage::testKey);
    connect(m_client, &GeminiClient::finished, this, [this](const QString&) {
        m_testButton->setEnabled(true);
        m_testResult->setText(tr("<span style='color:#3ecf8e'>✔ Работает: %1</span>").arg(GeminiClient::displayName().toHtmlEscaped()));
        // the client may have picked / replaced the model: show it
        const auto model = GeminiClient::model(m_shownProvider);
        if (!model.isEmpty() && selectedModel() != model) {
            int index = m_model->findData(model);
            if (index < 0) {
                m_model->addItem(model, model);
                index = m_model->count() - 1;
            }
            m_model->setCurrentIndex(index);
        }
    });
    connect(m_client, &GeminiClient::retrying, this, [this](const QString& msg) { m_testResult->setText(msg.toHtmlEscaped()); });
    connect(m_client, &GeminiClient::failed, this, [this](const QString& error) {
        m_testButton->setEnabled(true);
        m_testResult->setText(QStringLiteral("<span style='color:#ff6b6b'>✖ %1</span>").arg(error.toHtmlEscaped()));
    });

    m_offerOnCrash = new QCheckBox(tr("Предлагать помощь ИИ, если игра вылетела"), aiBox);
    m_form->addRow(m_offerOnCrash);

    auto privacy = new QLabel(tr("<small>При анализе в выбранный сервис ИИ отправляются: версия игры, список модов и последние логи. "
                                 "Токены входа и пароли из логов вырезаются. Ключи хранятся в файле настроек лаунчера. "
                                 "С Ollama и LM Studio всё остаётся на вашем компьютере.</small>"),
                              aiBox);
    privacy->setWordWrap(true);
    m_form->addRow(privacy);
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
    connect(m_provider, &QComboBox::currentIndexChanged, this, &AIPage::onProviderChanged);
}

void AIPage::loadSettings()
{
    auto s = APPLICATION->settings();
    m_edits = {};
    for (const auto& p : GeminiClient::providers()) {
        m_edits[p.id] = QJsonObject{ { "key", GeminiClient::apiKey(p.id) },
                                     { "model", p.id == "gemini" || !GeminiClient::model(p.id).isEmpty() ? GeminiClient::model(p.id) : QString() },
                                     { "url", GeminiClient::baseUrl(p.id) } };
    }
    const auto current = GeminiClient::currentProviderId();
    m_provider->blockSignals(true);
    m_provider->setCurrentIndex(std::max(0, m_provider->findData(current)));
    m_provider->blockSignals(false);
    m_shownProvider.clear();
    showProvider(current);
    m_offerOnCrash->setChecked(s->get("AIOfferOnCrash").toBool());
    m_animations->setChecked(s->get("UIAnimations").toBool());
}

QString AIPage::selectedModel() const
{
    const auto text = m_model->currentText().trimmed();
    const int index = m_model->findText(text);
    if (index >= 0 && !m_model->itemData(index).toString().isEmpty())
        return m_model->itemData(index).toString();
    return text.section(' ', 0, 0).trimmed();  // typed by hand ("id — description" -> id)
}

void AIPage::storeEdits()
{
    if (m_shownProvider.isEmpty())
        return;
    m_edits[m_shownProvider] = QJsonObject{ { "key", m_key->text().trimmed() }, { "model", selectedModel() }, { "url", m_url->text().trimmed() } };
}

void AIPage::showProvider(const QString& id)
{
    const auto p = GeminiClient::provider(id);
    m_shownProvider = p.id;
    const auto data = m_edits.value(p.id).toObject();

    QString hint;
    if (p.id == "ollama" || p.id == "lmstudio")
        hint = tr("Нейросеть работает прямо на вашем компьютере: бесплатно и без лимитов, но нужен мощный ПК. "
                  "<a href=\"%1\">Скачать %2</a>, скачать в ней модель и запустить. Ключ не нужен.")
                   .arg(p.keyUrl, p.name);
    else if (p.id == "custom")
        hint = tr("Любой сервис с OpenAI-совместимым API: укажите адрес (обычно заканчивается на /v1), ключ и модель.");
    else
        hint = tr("<a href=\"%1\">Получить ключ %2</a> · %3").arg(p.keyUrl, p.name, p.note);
    m_providerHint->setText(hint);

    m_form->setRowVisible(m_url, p.editableUrl);
    m_url->setText(data.value("url").toString(p.baseUrl));
    m_key->setPlaceholderText(p.keyPlaceholder);
    m_key->setText(data.value("key").toString());

    m_model->clear();
    const auto model = data.value("model").toString();
    if (!model.isEmpty())
        m_model->addItem(model, model);
    m_model->lineEdit()->setPlaceholderText(tr("выберется автоматически"));
    m_model->setCurrentText(model);
    m_testResult->clear();

    if (!p.needsKey || !m_key->text().isEmpty())
        refreshModels(true);
}

void AIPage::onProviderChanged()
{
    storeEdits();
    showProvider(m_provider->currentData().toString());
}

void AIPage::refreshModels(bool quiet)
{
    const auto p = GeminiClient::provider(m_shownProvider);
    const auto key = m_key->text().trimmed();
    if (p.needsKey && key.isEmpty()) {
        if (!quiet)
            m_testResult->setText(tr("Сначала вставьте ключ."));
        return;
    }
    m_refreshModels->setEnabled(false);
    if (!quiet)
        m_testResult->setText(tr("Загружаю список моделей…"));
    const auto providerId = p.id;
    m_lister->listModels(providerId, key, m_url->text().trimmed(), [this, providerId, quiet](QList<AIModelInfo> models, QString error) {
        m_refreshModels->setEnabled(true);
        if (providerId != m_shownProvider)
            return;
        if (models.isEmpty()) {
            if (!quiet)
                m_testResult->setText(QStringLiteral("<span style='color:#ff6b6b'>✖ %1</span>").arg(error.toHtmlEscaped()));
            return;
        }
        auto current = selectedModel();
        bool known = false;
        for (const auto& m : models)
            known = known || m.id == current;
        if (current.isEmpty() || !known)
            current = known ? current : (current.isEmpty() ? GeminiClient::pickModel(providerId, models) : current);
        m_model->clear();
        for (const auto& m : models)
            m_model->addItem(m.label, m.id);
        int index = m_model->findData(current);
        if (index < 0 && !current.isEmpty()) {
            m_model->insertItem(0, tr("%1 (нет в списке)").arg(current), current);
            index = 0;
        }
        m_model->setCurrentIndex(std::max(0, index));
        if (!quiet)
            m_testResult->setText(tr("Доступно моделей: %1").arg(models.size()));
    });
}

bool AIPage::apply()
{
    storeEdits();
    for (const auto& p : GeminiClient::providers()) {
        const auto data = m_edits.value(p.id).toObject();
        GeminiClient::saveProvider(p.id, data.value("key").toString(), data.value("model").toString(), data.value("url").toString());
    }
    GeminiClient::setCurrentProvider(m_shownProvider);
    auto s = APPLICATION->settings();
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
