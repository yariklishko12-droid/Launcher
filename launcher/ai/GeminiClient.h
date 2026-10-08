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
    /** Gemini is overloaded and the request will be repeated automatically. */
    void retrying(const QString& message);

   private:
    void send();

    QPointer<QNetworkReply> m_reply;
    QString m_systemPrompt, m_userPrompt, m_model;
    bool m_expectJson = true;
    int m_attempt = 0;
    quint64 m_generation = 0;  // invalidates pending retry timers after abort()/new request
};
