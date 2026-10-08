// SPDX-License-Identifier: GPL-3.0-only
#include "GeminiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

#include "Application.h"
#include "settings/SettingsObject.h"

namespace {
QString apiKey()
{
    return APPLICATION->settings()->get("GeminiApiKey").toString().trimmed();
}
}  // namespace

GeminiClient::GeminiClient(QObject* parent) : QObject(parent) {}

GeminiClient::~GeminiClient()
{
    abort();
}

bool GeminiClient::isConfigured()
{
    return !apiKey().isEmpty();
}

QString GeminiClient::currentModel()
{
    auto model = APPLICATION->settings()->get("GeminiModel").toString().trimmed();
    return model.isEmpty() ? QStringLiteral("gemini-2.5-flash") : model;
}

void GeminiClient::abort()
{
    if (m_reply) {
        auto reply = m_reply;
        m_reply = nullptr;
        reply->abort();
    }
}

void GeminiClient::generate(const QString& systemPrompt, const QString& userPrompt, bool expectJson)
{
    abort();
    if (!isConfigured()) {
        emit failed(tr("Не указан API-ключ Gemini. Откройте «Настройки → ИИ-помощник» и вставьте ключ."));
        return;
    }

    const QString model = QString::fromUtf8(QUrl::toPercentEncoding(currentModel()));
    QNetworkRequest request(QUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent").arg(model)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-goog-api-key", apiKey().toUtf8());
    request.setTransferTimeout(180 * 1000);

    QJsonObject generationConfig{ { "temperature", 0.2 } };
    if (expectJson)
        generationConfig["responseMimeType"] = QStringLiteral("application/json");

    QJsonObject body{
        { "systemInstruction", QJsonObject{ { "parts", QJsonArray{ QJsonObject{ { "text", systemPrompt } } } } } },
        { "contents", QJsonArray{ QJsonObject{ { "role", "user" }, { "parts", QJsonArray{ QJsonObject{ { "text", userPrompt } } } } } } },
        { "generationConfig", generationConfig },
    };

    auto reply = APPLICATION->network()->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const bool current = (m_reply == reply);
        if (current)
            m_reply = nullptr;
        if (!current || reply->error() == QNetworkReply::OperationCanceledError)
            return;

        const auto root = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || root.contains("error")) {
            QString message = root.value("error").toObject().value("message").toString();
            if (message.isEmpty())
                message = reply->errorString();
            emit failed(tr("Ошибка Gemini: %1").arg(message));
            return;
        }

        const auto candidates = root.value("candidates").toArray();
        if (candidates.isEmpty()) {
            const auto reason = root.value("promptFeedback").toObject().value("blockReason").toString();
            emit failed(reason.isEmpty() ? tr("Gemini не вернул ответ.") : tr("Gemini отклонил запрос (%1).").arg(reason));
            return;
        }

        QString text;
        const auto parts = candidates.first().toObject().value("content").toObject().value("parts").toArray();
        for (const auto& part : parts) {
            const auto obj = part.toObject();
            if (obj.value("thought").toBool())
                continue;
            text += obj.value("text").toString();
        }
        if (text.trimmed().isEmpty()) {
            emit failed(tr("Gemini вернул пустой ответ. Попробуйте ещё раз."));
            return;
        }
        emit finished(text);
    });
}

QJsonObject GeminiClient::parseJsonObject(const QString& text, QString* error)
{
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
