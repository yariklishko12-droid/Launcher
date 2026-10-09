// SPDX-License-Identifier: GPL-3.0-only
// AI assistant: resolves Modrinth projects for an instance and installs them with required dependencies.
#pragma once

#include <QJsonArray>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <functional>

struct ModCandidate {
    QString slug;    // slug or project id suggested by the AI
    QString name;    // human name (also used as a search query fallback)
    QString reason;  // why the AI suggested it

    bool resolved = false;
    QString error;
    QString projectId;
    QString title;
    QString versionNumber;
    QString fileName;
    QString fileUrl;
    QString sha1;
    QJsonArray dependencies;
    bool isDependency = false;
    QString requiredBy;  // for dependencies found inside jars: which mods need it
};

/** A mod that is known (from Modrinth search) to have a build for the instance's Minecraft version + loader. */
struct CatalogEntry {
    QString slug;
    QString title;
    QString description;
    QStringList categories;
    int downloads = 0;
    bool thematic = false;  // found by a theme search (set by the caller)
};

class ModrinthInstaller : public QObject {
    Q_OBJECT
   public:
    ModrinthInstaller(QString mcVersion, QString loader, QString modsDir, QObject* parent = nullptr);
    ~ModrinthInstaller() override;

    /** Looks every candidate up on Modrinth for this Minecraft version + loader. Emits resolved(). */
    void resolve(QList<ModCandidate> candidates);
    /** Loads the most popular mods that support this Minecraft version + loader (libraries excluded). Emits catalogReady(). */
    void fetchCatalog(int maxEntries);
    /** Searches Modrinth by English phrases and categories (only mods for this version + loader). Emits catalogReady(). */
    void fetchThemeCatalog(const QStringList& queries, const QStringList& categories, int pagesPerSearch);
    /** Downloads candidates and their required dependencies, skipping already installed projects.
     *  Afterwards reads the jars and fetches dependencies that are declared only inside the mods.
     *  Pass an empty list to only repair missing dependencies. Emits installFinished(). */
    void install(QList<ModCandidate> candidates);
    void abort();
    bool busy() const { return m_busy; }

   signals:
    void progress(const QString& message);
    void resolved(const QList<ModCandidate>& candidates);
    void catalogReady(const QList<CatalogEntry>& entries);
    void installFinished(const QStringList& installed, const QStringList& skipped, const QStringList& errors);

   private:
    using ReplyHandler = std::function<void(QNetworkReply*)>;
    void request(const QUrl& url, ReplyHandler handler, const QByteArray& postJson = {});
    void resolveOne(ModCandidate candidate, std::function<void(ModCandidate)> done);
    void searchFallback(ModCandidate candidate, std::function<void(ModCandidate)> done);
    void resolveNext();
    struct CatalogJob {
        QUrl url;
        int group;  // pages of one search; a short page ends its group
    };
    QUrl searchUrl(const QString& query, const QString& category, const QString& index, int offset) const;
    void startCatalog(QList<CatalogJob> jobs);
    void runCatalogJob();
    void scanInstalled(std::function<void()> done);
    void installNext();
    void download(ModCandidate candidate);
    void checkJarDependencies();
    void finishInstall();
    QStringList loaderList() const;

    QString m_mcVersion;
    QString m_loader;
    QString m_modsDir;
    bool m_busy = false;
    bool m_aborted = false;
    QPointer<QNetworkReply> m_reply;

    QList<ModCandidate> m_queue;
    QList<ModCandidate> m_results;
    int m_resolveIndex = 0;
    int m_resolveActive = 0;
    QList<CatalogEntry> m_catalog;
    QSet<QString> m_catalogSlugs;
    QList<CatalogJob> m_catalogJobs;
    QSet<int> m_finishedGroups;
    int m_catalogJobsTotal = 0;
    QSet<QString> m_installedProjects;
    QSet<QString> m_attemptedDeps;
    int m_depRounds = 0;
    QStringList m_installed, m_skipped, m_errors;
};
