// SPDX-License-Identifier: GPL-3.0-only
#include "GeminiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

#include "Application.h"
#include "settings/SettingsObject.h"

namespace {
using Kind = AIProviderInfo::Kind;

QJsonObject providerData()
{
    return QJsonDocument::fromJson(APPLICATION->settings()->get("AIProviderData").toString().toUtf8()).object();
}

QString trimUrl(QString url)
{
    url = url.trimmed();
    while (url.endsWith('/'))
        url.chop(1);
    return url;
}

QString errorMessage(const QJsonObject& root, QNetworkReply* reply)
{
    QString message;
    const auto error = root.value("error");
    if (error.isObject())
        message = error.toObject().value("message").toString();
    else if (error.isString())
        message = error.toString();
    if (message.isEmpty())
        message = root.value("message").toString();
    if (message.isEmpty())
        message = root.value("detail").toString();
    if (message.isEmpty())
        message = reply->errorString();
    return message;
}

bool containsAny(const QString& text, std::initializer_list<const char*> words)
{
    for (const auto* w : words) {
        if (text.contains(QLatin1String(w), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool isNonChatModel(const QString& id)
{
    return containsAny(id, { "embed", "whisper", "tts", "dall-e", "moderation", "transcribe", "realtime", "audio", "image", "guard",
                             "rerank", "-live", "robotics", "computer-use", "aqa", "learnlm", "veo", "imagen", "lyria", "sora",
                             "search-preview", "codex" });
}
}  // namespace

// ---------------------------------------------------------------- providers

const QList<AIProviderInfo>& GeminiClient::providers()
{
    static const QList<AIProviderInfo> list = {
        { "gemini", "Google Gemini", QObject::tr("бесплатно"), Kind::Gemini, "https://generativelanguage.googleapis.com/v1beta", true,
          false, "https://aistudio.google.com/apikey", "AIza…", 400 },
        { "openrouter", "OpenRouter", QObject::tr("сотни моделей, есть бесплатные"), Kind::OpenAI, "https://openrouter.ai/api/v1", true,
          false, "https://openrouter.ai/settings/keys", "sk-or-…", 250 },
        { "groq", "Groq", QObject::tr("бесплатно, очень быстро"), Kind::OpenAI, "https://api.groq.com/openai/v1", true, false,
          "https://console.groq.com/keys", "gsk_…", 100 },
        { "deepseek", "DeepSeek", QObject::tr("платно, очень дёшево"), Kind::OpenAI, "https://api.deepseek.com/v1", true, false,
          "https://platform.deepseek.com/api_keys", "sk-…", 300 },
        { "openai", "OpenAI (ChatGPT)", QObject::tr("платно"), Kind::OpenAI, "https://api.openai.com/v1", true, false,
          "https://platform.openai.com/api-keys", "sk-…", 300 },
        { "claude", "Claude (Anthropic)", QObject::tr("платно"), Kind::Claude, "https://api.anthropic.com/v1", true, false,
          "https://console.anthropic.com/settings/keys", "sk-ant-…", 300 },
        { "ollama", "Ollama", QObject::tr("на своём компьютере"), Kind::OpenAI, "http://localhost:11434/v1", false, true,
          "https://ollama.com/download", QObject::tr("не нужен"), 60 },
        { "lmstudio", "LM Studio", QObject::tr("на своём компьютере"), Kind::OpenAI, "http://localhost:1234/v1", false, true,
          "https://lmstudio.ai", QObject::tr("не нужен"), 60 },
        { "custom", QObject::tr("Другой сервис"), QObject::tr("любой OpenAI-совместимый"), Kind::OpenAI, "", false, true, "",
          QObject::tr("если нужен"), 150 },
    };
    return list;
}

QString GeminiClient::currentProviderId()
{
    const auto id = APPLICATION->settings()->get("AIProvider").toString();
    for (const auto& p : providers()) {
        if (p.id == id)
            return id;
    }
    return QStringLiteral("gemini");
}

AIProviderInfo GeminiClient::provider(const QString& id)
{
    const auto wanted = id.isEmpty() ? currentProviderId() : id;
    for (const auto& p : providers()) {
        if (p.id == wanted)
            return p;
    }
    return providers().first();
}

QString GeminiClient::apiKey(const QString& id)
{
    const auto p = provider(id);
    if (p.id == "gemini")
        return APPLICATION->settings()->get("GeminiApiKey").toString().trimmed();
    return providerData().value(p.id).toObject().value("key").toString().trimmed();
}

QString GeminiClient::model(const QString& id)
{
    const auto p = provider(id);
    if (p.id == "gemini") {
        const auto m = APPLICATION->settings()->get("GeminiModel").toString().trimmed();
        return m.isEmpty() ? QStringLiteral("gemini-2.5-flash") : m;
    }
    return providerData().value(p.id).toObject().value("model").toString().trimmed();
}

QString GeminiClient::baseUrl(const QString& id)
{
    const auto p = provider(id);
    const auto url = providerData().value(p.id).toObject().value("url").toString();
    return trimUrl(p.editableUrl && !url.trimmed().isEmpty() ? url : p.baseUrl);
}

void GeminiClient::saveProvider(const QString& id, const QString& key, const QString& model, const QString& url)
{
    auto s = APPLICATION->settings();
    if (id == "gemini") {
        s->set("GeminiApiKey", key.trimmed());
        s->set("GeminiModel", model.trimmed());
        return;
    }
    auto data = providerData();
    data[id] = QJsonObject{ { "key", key.trimmed() }, { "model", model.trimmed() }, { "url", trimUrl(url) } };
    s->set("AIProviderData", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
}

void GeminiClient::setCurrentProvider(const QString& id)
{
    APPLICATION->settings()->set("AIProvider", id);
}

bool GeminiClient::isConfigured()
{
    const auto p = provider();
    return p.needsKey ? !apiKey().isEmpty() : !baseUrl().isEmpty();
}

QString GeminiClient::currentModel()
{
    const auto m = model();
    return m.isEmpty() ? tr("модель выберется автоматически") : m;
}

QString GeminiClient::displayName()
{
    return QStringLiteral("%1 · %2").arg(provider().name, currentModel());
}

// ---------------------------------------------------------------- requests

GeminiClient::GeminiClient(QObject* parent) : QObject(parent) {}

GeminiClient::~GeminiClient()
{
    abort();
}

void GeminiClient::abort()
{
    ++m_generation;
    if (m_reply) {
        auto reply = m_reply;
        m_reply = nullptr;
        reply->abort();
    }
    if (m_listReply) {
        auto reply = m_listReply;
        m_listReply = nullptr;
        reply->abort();
    }
}

void GeminiClient::fail(const QString& error)
{
    emit failed(error);
}

void GeminiClient::generate(const QString& systemPrompt, const QString& userPrompt, bool expectJson)
{
    abort();
    m_provider = provider();
    m_key = apiKey();
    m_url = baseUrl();
    m_model = model();
    m_systemPrompt = systemPrompt;
    m_userPrompt = userPrompt;
    m_expectJson = expectJson;
    m_attempt = 0;
    m_replacements = 0;
    m_badModels.clear();
    m_tooLarge = false;
    m_noTemperature = false;
    m_claudeMaxTokens = 16000;

    if (m_provider.needsKey && m_key.isEmpty()) {
        fail(tr("Не указан API-ключ для %1. Откройте «Настройки → ИИ-помощник» и вставьте ключ.").arg(m_provider.name));
        return;
    }
    if (m_url.isEmpty()) {
        fail(tr("Не указан адрес сервера для %1. Откройте «Настройки → ИИ-помощник».").arg(m_provider.name));
        return;
    }
    if (m_model.isEmpty()) {
        // No model chosen yet: take the best one from the service's own list and remember it.
        const auto generation = m_generation;
        emit retrying(tr("Подбираю модель %1…").arg(m_provider.name));
        listModels(m_provider.id, m_key, m_url, [this, generation](QList<AIModelInfo> models, QString error) {
            if (generation != m_generation)
                return;
            const auto picked = pickModel(m_provider.id, models);
            if (picked.isEmpty()) {
                fail(error.isEmpty() ? tr("У %1 нет доступных моделей. Выберите модель в «Настройки → ИИ-помощник».").arg(m_provider.name)
                                     : error);
                return;
            }
            m_model = picked;
            saveProvider(m_provider.id, m_key, picked, m_url);
            send();
        });
        return;
    }
    send();
}

void GeminiClient::replaceModel(const QString& reason, const QString& failMessage, bool permanent)
{
    m_badModels.insert(m_model);
    ++m_replacements;
    emit retrying(tr("%1 Подбираю другую модель…").arg(reason));
    const auto generation = m_generation;
    listModels(m_provider.id, m_key, m_url, [this, generation, failMessage, permanent](QList<AIModelInfo> models, QString) {
        if (generation != m_generation)
            return;
        const auto next = pickModel(m_provider.id, models, m_badModels);
        if (next.isEmpty()) {
            fail(failMessage);
            return;
        }
        if (permanent)
            saveProvider(m_provider.id, m_key, next, m_url);
        emit retrying(tr("Переключаюсь на модель %1…").arg(next));
        m_model = next;
        m_attempt = 0;
        send();
    });
}

void GeminiClient::send()
{
    QNetworkRequest request;
    QJsonObject body;
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(300 * 1000);

    switch (m_provider.kind) {
        case Kind::Gemini: {
            const QString model = QString::fromUtf8(QUrl::toPercentEncoding(m_model));
            request.setUrl(QUrl(QStringLiteral("%1/models/%2:generateContent").arg(m_url, model)));
            request.setRawHeader("x-goog-api-key", m_key.toUtf8());
            QJsonObject generationConfig{ { "temperature", 0.2 } };
            if (m_expectJson)
                generationConfig["responseMimeType"] = QStringLiteral("application/json");
            body = {
                { "systemInstruction", QJsonObject{ { "parts", QJsonArray{ QJsonObject{ { "text", m_systemPrompt } } } } } },
                { "contents",
                  QJsonArray{ QJsonObject{ { "role", "user" }, { "parts", QJsonArray{ QJsonObject{ { "text", m_userPrompt } } } } } } },
                { "generationConfig", generationConfig },
            };
            break;
        }
        case Kind::Claude: {
            request.setUrl(QUrl(m_url + "/messages"));
            request.setRawHeader("x-api-key", m_key.toUtf8());
            request.setRawHeader("anthropic-version", "2023-06-01");
            body = {
                { "model", m_model },
                { "max_tokens", m_claudeMaxTokens },
                { "system", m_systemPrompt },
                { "messages", QJsonArray{ QJsonObject{ { "role", "user" }, { "content", m_userPrompt } } } },
            };
            if (!m_noTemperature)
                body["temperature"] = 0.2;
            break;
        }
        case Kind::OpenAI: {
            request.setUrl(QUrl(m_url + "/chat/completions"));
            if (!m_key.isEmpty())
                request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
            if (m_provider.id == "openrouter") {
                request.setRawHeader("HTTP-Referer", "https://github.com/yariklishko12-droid/Launcher");
                request.setRawHeader("X-Title", "PineconeMC Launcher");
            }
            body = {
                { "model", m_model },
                { "messages", QJsonArray{ QJsonObject{ { "role", "system" }, { "content", m_systemPrompt } },
                                          QJsonObject{ { "role", "user" }, { "content", m_userPrompt } } } },
            };
            if (!m_noTemperature)
                body["temperature"] = 0.2;
            // Only services that reliably support JSON mode get it; the prompts ask for JSON anyway.
            if (m_expectJson && (m_provider.id == "openai" || m_provider.id == "deepseek"))
                body["response_format"] = QJsonObject{ { "type", "json_object" } };
            break;
        }
    }

    auto reply = APPLICATION->network()->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const bool current = (m_reply == reply);
        if (current)
            m_reply = nullptr;
        if (!current || reply->error() == QNetworkReply::OperationCanceledError)
            return;

        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto root = QJsonDocument::fromJson(reply->readAll()).object();
        const auto name = m_provider.name;

        if (reply->error() != QNetworkReply::NoError || root.contains("error") || status >= 400) {
            const QString message = errorMessage(root, reply);
            const auto netError = reply->error();

            // ---- classify the failure
            if (status == 0) {
                const bool temporaryNet = netError == QNetworkReply::TimeoutError || netError == QNetworkReply::RemoteHostClosedError ||
                                          netError == QNetworkReply::TemporaryNetworkFailureError;
                if (!temporaryNet) {
                    if (m_provider.editableUrl)
                        fail(tr("Не удалось подключиться к %1 по адресу %2. Запустите программу и проверьте адрес в настройках.")
                                 .arg(name, m_url));
                    else
                        fail(tr("Нет связи с %1: %2. Проверьте интернет.").arg(name, message));
                    return;
                }
            }

            // Claude: retry with a smaller output limit / without temperature for models that reject them.
            if (status == 400 && message.contains("max_tokens") && m_claudeMaxTokens > 4096) {
                m_claudeMaxTokens = m_claudeMaxTokens > 8192 ? 8192 : 4096;
                send();
                return;
            }
            if (status == 400 && message.contains("temperature", Qt::CaseInsensitive) && !m_noTemperature) {
                m_noTemperature = true;
                send();
                return;
            }

            // Retry hint from the server (Retry-After header or Gemini RetryInfo); Gemini also tells which quota ran out.
            double retryAfter = reply->rawHeader("Retry-After").toDouble();
            bool daily = false;
            for (const auto& d : root.value("error").toObject().value("details").toArray()) {
                const auto detail = d.toObject();
                if (detail.value("@type").toString().endsWith("RetryInfo")) {
                    auto delay = detail.value("retryDelay").toString();
                    delay.chop(1);
                    retryAfter = delay.toDouble();
                }
                for (const auto& v : detail.value("violations").toArray()) {
                    if (v.toObject().value("quotaId").toString().contains("PerDay"))
                        daily = true;
                }
            }

            const bool auth = status == 401 ||
                              containsAny(message, { "api key not valid", "invalid api key", "incorrect api key", "invalid x-api-key",
                                                     "invalid_api_key", "authentication", "unauthorized" });
            const bool credits = status == 402 || containsAny(message, { "credit balance", "insufficient credits", "insufficient_quota",
                                                                         "insufficient balance", "requires more credits" }) ||
                                 // Gemini mentions "billing" in every quota error, for the others it means money
                                 (m_provider.kind != Kind::Gemini && message.contains("billing", Qt::CaseInsensitive)) ||
                                 (m_provider.id == "openai" && message.contains("exceeded your current quota"));
            const bool tooLarge = status == 413 || containsAny(message, { "too large", "context length", "context_length", "maximum context",
                                                                          "prompt is too long", "reduce the length", "too many tokens",
                                                                          "context window" });
            const bool modelGone = status == 404 ||
                                   (message.contains("model", Qt::CaseInsensitive) &&
                                    containsAny(message, { "no longer available", "not found", "does not exist", "decommissioned",
                                                           "deprecated", "not supported", "no endpoints found", "not available" }));
            if (status == 429 && !credits && !tooLarge) {
                daily = daily || retryAfter > 120 ||
                        containsAny(message, { "per day", "daily", "(tpd)", "(rpd)", "free-models-per-day", "requests per day" });
            }
            const bool temporary = !daily && !credits && !tooLarge &&
                                   (status == 429 || status == 500 || status == 502 || status == 503 || status == 504 ||
                                    status == 529 || containsAny(message, { "overloaded", "high demand", "try again later" }) ||
                                    netError == QNetworkReply::TimeoutError || netError == QNetworkReply::RemoteHostClosedError ||
                                    netError == QNetworkReply::TemporaryNetworkFailureError);

            // ---- react
            if (auth) {
                fail(tr("Ключ %1 не подходит (%2). Проверьте его в «Настройки → ИИ-помощник».").arg(name, message));
            } else if (credits) {
                fail(tr("У %1 закончились деньги или бесплатные кредиты на счёте (%2). Пополните счёт или выберите другой сервис.")
                         .arg(name, message));
            } else if (tooLarge) {
                m_tooLarge = true;
                fail(tr("Запрос слишком большой для модели %1 (%2).").arg(m_model, message));
            } else if (daily) {
                const auto text = tr("Закончился лимит запросов к %1 (модель %2) на сегодня. Он обновится в течение суток; "
                                     "можно выбрать другую модель или другой сервис в «Настройки → ИИ-помощник».")
                                      .arg(name, m_model);
                if (m_replacements < 2)
                    replaceModel(tr("Лимит модели %1 на сегодня исчерпан.").arg(m_model), text, false);
                else
                    fail(text);
            } else if (modelGone && !temporary) {
                const auto text = tr("Модель %1 недоступна у %2 (%3). Выберите другую в «Настройки → ИИ-помощник».").arg(m_model, name, message);
                if (m_replacements < 2)
                    replaceModel(tr("Модель %1 больше недоступна.").arg(m_model), text, true);
                else
                    fail(text);
            } else if (temporary) {
                if (m_attempt < 3) {
                    ++m_attempt;
                    static const int schedule[] = { 5, 15, 30 };
                    const int delay = retryAfter > 0 ? std::clamp(int(retryAfter + 1), 2, 60) : schedule[m_attempt - 1];
                    emit retrying(tr("%1 перегружен, повтор %2 из 3 через %3 с…").arg(name).arg(m_attempt).arg(delay));
                    const auto generation = m_generation;
                    QTimer::singleShot(delay * 1000, this, [this, generation]() {
                        if (generation == m_generation)
                            send();
                    });
                } else if (m_replacements < 2) {
                    replaceModel(tr("Модель %1 перегружена.").arg(m_model), tr("%1 перегружен, попробуйте позже: %2").arg(name, message), false);
                } else {
                    fail(tr("%1 перегружен, попробуйте позже: %2").arg(name, message));
                }
            } else {
                fail(tr("Ошибка %1: %2").arg(name, message));
            }
            return;
        }

        // ---- success: extract the text
        QString text;
        switch (m_provider.kind) {
            case Kind::Gemini: {
                const auto candidates = root.value("candidates").toArray();
                if (candidates.isEmpty()) {
                    const auto reason = root.value("promptFeedback").toObject().value("blockReason").toString();
                    fail(reason.isEmpty() ? tr("%1 не вернул ответ.").arg(name) : tr("%1 отклонил запрос (%2).").arg(name, reason));
                    return;
                }
                const auto parts = candidates.first().toObject().value("content").toObject().value("parts").toArray();
                for (const auto& part : parts) {
                    const auto obj = part.toObject();
                    if (!obj.value("thought").toBool())
                        text += obj.value("text").toString();
                }
                break;
            }
            case Kind::Claude: {
                for (const auto& block : root.value("content").toArray()) {
                    const auto obj = block.toObject();
                    if (obj.value("type").toString() == "text")
                        text += obj.value("text").toString();
                }
                break;
            }
            case Kind::OpenAI: {
                const auto choices = root.value("choices").toArray();
                const auto content = choices.isEmpty() ? QJsonValue() : choices.first().toObject().value("message").toObject().value("content");
                if (content.isString()) {
                    text = content.toString();
                } else {
                    for (const auto& part : content.toArray())
                        text += part.toObject().value("text").toString();
                }
                break;
            }
        }
        if (text.trimmed().isEmpty()) {
            fail(tr("%1 вернул пустой ответ. Попробуйте ещё раз.").arg(name));
            return;
        }
        emit finished(text);
    });
}

// ---------------------------------------------------------------- models

void GeminiClient::listModels(const QString& providerId,
                              const QString& key,
                              const QString& url,
                              std::function<void(QList<AIModelInfo>, QString)> done)
{
    const auto p = provider(providerId);
    const auto base = trimUrl(url.isEmpty() ? p.baseUrl : url);
    QNetworkRequest request;
    request.setTransferTimeout(30 * 1000);
    switch (p.kind) {
        case Kind::Gemini:
            request.setUrl(QUrl(base + "/models?pageSize=1000"));
            request.setRawHeader("x-goog-api-key", key.trimmed().toUtf8());
            break;
        case Kind::Claude:
            request.setUrl(QUrl(base + "/models?limit=100"));
            request.setRawHeader("x-api-key", key.trimmed().toUtf8());
            request.setRawHeader("anthropic-version", "2023-06-01");
            break;
        case Kind::OpenAI:
            request.setUrl(QUrl(base + "/models"));
            if (!key.trimmed().isEmpty())
                request.setRawHeader("Authorization", "Bearer " + key.trimmed().toUtf8());
            break;
    }
    if (m_listReply)
        m_listReply->abort();
    auto reply = APPLICATION->network()->get(request);
    m_listReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, p, done]() {
        reply->deleteLater();
        if (m_listReply == reply)
            m_listReply = nullptr;
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        const auto root = QJsonDocument::fromJson(reply->readAll()).object();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            if (status == 0 && p.editableUrl)
                done({}, tr("Не удалось подключиться к %1. Запустите программу и проверьте адрес.").arg(p.name));
            else
                done({}, tr("%1: %2").arg(p.name, errorMessage(root, reply)));
            return;
        }

        QList<AIModelInfo> models;
        if (p.kind == Kind::Gemini) {
            for (const auto& value : root.value("models").toArray()) {
                const auto obj = value.toObject();
                auto id = obj.value("name").toString();
                id.remove(QRegularExpression("^models/"));
                bool chat = false;
                for (const auto& m : obj.value("supportedGenerationMethods").toArray())
                    chat = chat || m.toString() == "generateContent";
                if (!chat || !id.startsWith("gemini") || isNonChatModel(id) || id.contains("tts"))
                    continue;
                models << AIModelInfo{ id, QStringLiteral("%1 — %2").arg(id, obj.value("displayName").toString()), false };
            }
        } else {
            for (const auto& value : root.value("data").toArray()) {
                const auto obj = value.toObject();
                const auto id = obj.value("id").toString();
                if (id.isEmpty() || isNonChatModel(id))
                    continue;
                AIModelInfo info{ id, id, false };
                if (p.id == "openrouter") {
                    const auto pricing = obj.value("pricing").toObject();
                    info.free = id.endsWith(":free") ||
                                (pricing.value("prompt").toString() == "0" && pricing.value("completion").toString() == "0");
                    info.label = info.free ? tr("%1 (бесплатно)").arg(id) : id;
                } else if (p.kind == Kind::Claude && obj.contains("display_name")) {
                    info.label = QStringLiteral("%1 — %2").arg(id, obj.value("display_name").toString());
                }
                models << info;
            }
            if (p.id == "openrouter")
                std::stable_sort(models.begin(), models.end(), [](const AIModelInfo& a, const AIModelInfo& b) { return a.free && !b.free; });
        }
        if (models.isEmpty())
            done({}, tr("У %1 не найдено подходящих моделей.").arg(p.name));
        else
            done(models, QString());
    });
}

QString GeminiClient::pickModel(const QString& providerId, const QList<AIModelInfo>& models, const QSet<QString>& exclude)
{
    static const QRegularExpression version(QStringLiteral(R"((?:gemini|gpt|claude-\w+|llama)-(\d+(?:\.\d+)?))"));
    static const QRegularExpression dated(QStringLiteral(R"(\d{4}-?\d{2}-?\d{2})"));
    auto score = [&](const AIModelInfo& m) {
        const auto id = m.id.toLower();
        double s = 0;
        const auto v = version.match(id);
        const double ver = v.hasMatch() ? v.captured(1).toDouble() : 0;
        if (providerId == "gemini") {
            s += id.contains("flash") ? 100 : 0;
            s -= id.contains("lite") ? 40 : 0;
            s -= id.contains("pro") ? 20 : 0;  // tiny free limits
            s -= (id.contains("preview") || id.contains("exp")) ? 30 : 0;
            s -= id.contains("gemma") ? 200 : 0;
            s += ver * 20;
        } else if (providerId == "openrouter") {
            s += m.free ? 200 : 0;
            s += (id.contains("deepseek") || id.contains("gemini")) ? 30 : 0;
            s += (id.contains("llama-3.3-70b") || id.contains("qwen3") || id.contains("gpt-oss")) ? 20 : 0;
        } else if (providerId == "groq") {
            s += id.contains("llama-3.3-70b") ? 100 : 0;
            s += id.contains("gpt-oss-120b") ? 90 : 0;
            s += id.contains("qwen") ? 50 : 0;
            s += id.contains("llama") ? 30 : 0;
        } else if (providerId == "deepseek") {
            s += id == "deepseek-chat" ? 100 : 0;
            s += id.contains("reasoner") ? 50 : 0;
        } else if (providerId == "openai") {
            s += id.startsWith("gpt-") ? 50 : 0;
            s += id.contains("mini") ? 50 : 0;
            s -= id.contains("nano") ? 20 : 0;
            s -= dated.match(id).hasMatch() ? 5 : 0;
            s += ver * 10;
        } else if (providerId == "claude") {
            s += id.contains("sonnet") ? 100 : 0;
            s += id.contains("haiku") ? 60 : 0;
            s += id.contains("opus") ? 40 : 0;
        }
        return s;
    };
    QString best;
    double bestScore = -1e9;
    for (const auto& m : models) {
        if (exclude.contains(m.id))
            continue;
        const double s = score(m);
        if (best.isEmpty() || s > bestScore) {
            best = m.id;
            bestScore = s;
        }
    }
    return best;
}

// ---------------------------------------------------------------- helpers

QJsonObject GeminiClient::parseJsonObject(const QString& rawText, QString* error)
{
    static const QRegularExpression think(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
    QString text = rawText;
    text.remove(think);
    const auto start = text.indexOf('{');
    const auto end = text.lastIndexOf('}');
    if (start < 0 || end <= start) {
        if (error)
            *error = tr("В ответе ИИ нет JSON.");
        return {};
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(text.mid(start, end - start + 1).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = tr("Не удалось разобрать ответ ИИ: %1").arg(parseError.errorString());
        return {};
    }
    return doc.object();
}

QString GeminiClient::redact(QString text)
{
    static const QRegularExpression accessToken(QStringLiteral(R"((--accessToken|--session|--uuid|--xuid|--clientId)\s+\S+)"));
    static const QRegularExpression keyValue(QStringLiteral(R"((?i)\b(access_?token|session_?id|auth_?token|refresh_?token|password)\b\s*[=:]\s*\S+)"));
    static const QRegularExpression jwt(QStringLiteral(R"(eyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,})"));
    text.replace(accessToken, QStringLiteral("\\1 <скрыто>"));
    text.replace(keyValue, QStringLiteral("\\1=<скрыто>"));
    text.replace(jwt, QStringLiteral("<скрыто>"));
    return text;
}
