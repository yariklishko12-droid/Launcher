// SPDX-License-Identifier: GPL-3.0-only
// AI assistant: client for several AI services (Google Gemini, Anthropic Claude and every OpenAI-compatible API:
// OpenRouter, Groq, DeepSeek, OpenAI, Ollama, LM Studio, custom). The class keeps its historical name.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <functional>

struct AIProviderInfo {
    enum class Kind { Gemini, OpenAI, Claude };
    QString id;
    QString name;        // "Groq"
    QString note;        // "бесплатно, очень быстро"
    Kind kind = Kind::OpenAI;
    QString baseUrl;     // default API address
    bool needsKey = true;
    bool editableUrl = false;
    QString keyUrl;      // where to get a key
    QString keyPlaceholder;
    int catalogSize = 300;  // how many catalogue mods fit comfortably into one request
};

struct AIModelInfo {
    QString id;
    QString label;
    bool free = false;
};

class GeminiClient : public QObject {
    Q_OBJECT
   public:
    explicit GeminiClient(QObject* parent = nullptr);
    ~GeminiClient() override;

    // ---- providers and their settings
    static const QList<AIProviderInfo>& providers();
    /** Provider by id; empty id = the provider selected in the settings. */
    static AIProviderInfo provider(const QString& id = QString());
    static QString currentProviderId();
    static QString apiKey(const QString& id = QString());
    static QString model(const QString& id = QString());
    static QString baseUrl(const QString& id = QString());
    static void saveProvider(const QString& id, const QString& key, const QString& model, const QString& url);
    static void setCurrentProvider(const QString& id);

    static bool isConfigured();
    static QString currentModel();
    /** "Groq · llama-3.3-70b" for headers and messages. */
    static QString displayName();

    /** Sends a request to the selected provider. Emits finished() with the model text or failed() with a readable error. */
    void generate(const QString& systemPrompt, const QString& userPrompt, bool expectJson = true);
    void abort();
    bool busy() const { return !m_reply.isNull(); }
    /** True if the last failure was "request too large / context exceeded": the caller may retry with less data. */
    bool lastErrorTooLarge() const { return m_tooLarge; }

    /** Loads the models available for the given provider/key/address (does not depend on saved settings). */
    void listModels(const QString& providerId,
                    const QString& key,
                    const QString& url,
                    std::function<void(QList<AIModelInfo> models, QString error)> done);
    /** Picks a sensible default model from a list (optionally avoiding one). */
    static QString pickModel(const QString& providerId, const QList<AIModelInfo>& models, const QSet<QString>& exclude = {});

    /** Extracts the first JSON object from a model answer (tolerates ```json fences and <think> blocks). */
    static QJsonObject parseJsonObject(const QString& text, QString* error = nullptr);

    /** Removes tokens / session ids that must never leave the user's machine. */
    static QString redact(QString text);

   signals:
    void finished(const QString& text);
    void failed(const QString& error);
    /** The service is overloaded / the model changed and the request will be repeated automatically. */
    void retrying(const QString& message);

   private:
    void send();
    void fail(const QString& error);
    void replaceModel(const QString& reason, const QString& failMessage, bool permanent);

    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_listReply;
    AIProviderInfo m_provider;
    QString m_key, m_url, m_model;
    QString m_systemPrompt, m_userPrompt;
    bool m_expectJson = true;
    int m_attempt = 0;
    int m_replacements = 0;
    QSet<QString> m_badModels;
    bool m_noTemperature = false;
    bool m_tooLarge = false;
    int m_claudeMaxTokens = 16000;
    quint64 m_generation = 0;  // invalidates pending retry timers after abort()/new request
};
