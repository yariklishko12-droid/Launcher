// SPDX-License-Identifier: GPL-3.0-only
// AI assistant: thin client for the Google Gemini generateContent API.
#pragma once

#include <QJsonObject>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>

class GeminiClient : public QObject {
    Q_OBJECT
   public:
    explicit GeminiClient(QObject* parent = nullptr);
    ~GeminiClient() override;

    static bool isConfigured();
    static QString currentModel();

    /** Sends a request. Emits finished() with the model text or failed() with a readable error. */
    void generate(const QString& systemPrompt, const QString& userPrompt, bool expectJson = true);
    void abort();
    bool busy() const { return !m_reply.isNull(); }

    /** Extracts the first JSON object from a model answer (tolerates ```json fences). */
    static QJsonObject parseJsonObject(const QString& text, QString* error = nullptr);

    /** Removes tokens / session ids that must never leave the user's machine. */
    static QString redact(QString text);

   signals:
    void finished(const QString& text);
    void failed(const QString& error);

   private:
    QPointer<QNetworkReply> m_reply;
};
