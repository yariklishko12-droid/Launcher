// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthInstaller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QRegularExpression>
#include <QUrlQuery>

#include "Application.h"
#include "DependencyScanner.h"

namespace {
const QString API = QStringLiteral("https://api.modrinth.com/v2");

QString toJsonList(const QStringList& list)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact));
}

QString sha1Of(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}
}  // namespace

ModrinthInstaller::ModrinthInstaller(QString mcVersion, QString loader, QString modsDir, QObject* parent)
    : QObject(parent), m_mcVersion(std::move(mcVersion)), m_loader(std::move(loader)), m_modsDir(std::move(modsDir))
{}

ModrinthInstaller::~ModrinthInstaller()
{
    abort();
}

void ModrinthInstaller::abort()
{
    m_aborted = true;
    m_busy = false;
    if (m_reply) {
        auto reply = m_reply;
        m_reply = nullptr;
        reply->abort();
    }
}

QStringList ModrinthInstaller::loaderList() const
{
    if (m_loader == "quilt")
        return { "quilt", "fabric" };
    return { m_loader };
}

void ModrinthInstaller::request(const QUrl& url, ReplyHandler handler, const QByteArray& postJson)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    req.setTransferTimeout(60 * 1000);
    QNetworkReply* reply;
    if (postJson.isEmpty()) {
        reply = APPLICATION->network()->get(req);
    } else {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = APPLICATION->network()->post(req, postJson);
    }
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler]() {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (m_aborted || reply->error() == QNetworkReply::OperationCanceledError)
            return;
        handler(reply);
    });
}

// ---------------------------------------------------------------- resolving

void ModrinthInstaller::resolveOne(ModCandidate candidate, std::function<void(ModCandidate)> done)
{
    const QString id = candidate.slug.trimmed().toLower();
    if (id.isEmpty()) {
        searchFallback(candidate, done);
        return;
    }
    QUrl url(API + "/project/" + QString::fromUtf8(QUrl::toPercentEncoding(id)) + "/version");
    QUrlQuery query;
    query.addQueryItem("loaders", toJsonList(loaderList()));
    query.addQueryItem("game_versions", toJsonList({ m_mcVersion }));
    url.setQuery(query);

    request(url, [this, candidate, done](QNetworkReply* reply) mutable {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto versions = QJsonDocument::fromJson(reply->readAll()).array();
        if (status == 404 || (reply->error() == QNetworkReply::NoError && versions.isEmpty())) {
            // The slug may be hallucinated/outdated: try one search by name (only when the project does not exist,
            // otherwise a search would silently pick a different mod that happens to support this version).
            if (status == 404 && !candidate.name.isEmpty()) {
                searchFallback(candidate, done);
                return;
            }
            candidate.error = status == 404 ? tr("не найден на Modrinth") : tr("нет версии для %1 (%2)").arg(m_mcVersion, m_loader);
            done(candidate);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            candidate.error = reply->errorString();
            done(candidate);
            return;
        }

        QJsonObject chosen = versions.first().toObject();
        for (const auto& v : versions) {
            if (v.toObject().value("version_type").toString() == "release") {
                chosen = v.toObject();
                break;
            }
        }
        const auto files = chosen.value("files").toArray();
        if (files.isEmpty()) {
            candidate.error = tr("у версии нет файлов");
            done(candidate);
            return;
        }
        QJsonObject file = files.first().toObject();
        for (const auto& f : files) {
            if (f.toObject().value("primary").toBool()) {
                file = f.toObject();
                break;
            }
        }
        candidate.resolved = true;
        candidate.error.clear();
        candidate.projectId = chosen.value("project_id").toString();
        candidate.versionNumber = chosen.value("version_number").toString();
        candidate.fileName = file.value("filename").toString();
        candidate.fileUrl = file.value("url").toString();
        candidate.sha1 = file.value("hashes").toObject().value("sha1").toString();
        candidate.dependencies = chosen.value("dependencies").toArray();
        if (candidate.isDependency)
            candidate.title = chosen.value("name").toString();
        else if (candidate.title.isEmpty())
            candidate.title = candidate.name.isEmpty() ? candidate.slug : candidate.name;
        done(candidate);
    });
}

void ModrinthInstaller::searchFallback(ModCandidate candidate, std::function<void(ModCandidate)> done)
{
    const QString text = candidate.name.isEmpty() ? candidate.slug : candidate.name;
    QUrl url(API + "/search");
    QUrlQuery query;
    query.addQueryItem("query", text);
    query.addQueryItem("limit", "1");
    QJsonArray loaders;
    for (const auto& l : loaderList())
        loaders.append("categories:" + l);
    const QJsonArray facets{ QJsonArray{ "project_type:mod" }, QJsonArray{ "versions:" + m_mcVersion }, loaders };
    query.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
    url.setQuery(query);

    request(url, [this, candidate, done](QNetworkReply* reply) mutable {
        const auto hits = QJsonDocument::fromJson(reply->readAll()).object().value("hits").toArray();
        if (reply->error() != QNetworkReply::NoError || hits.isEmpty()) {
            candidate.error = tr("не найден для %1 (%2)").arg(m_mcVersion, m_loader);
            done(candidate);
            return;
        }
        const auto hit = hits.first().toObject();
        if (candidate.isDependency) {
            const auto norm = [](QString s) { return s.toLower().remove(QRegularExpression("[^a-z0-9]")); };
            const auto want = norm(candidate.name), slug = norm(hit.value("slug").toString()), title = norm(hit.value("title").toString());
            if (want.isEmpty() || !(slug == want || slug.contains(want) || want.contains(slug) || title.contains(want))) {
                candidate.error = tr("не найден на Modrinth (возможно, есть только на CurseForge)");
                done(candidate);
                return;
            }
        }
        candidate.slug = hit.value("slug").toString();
        candidate.title = hit.value("title").toString();
        candidate.name.clear();  // prevents another fallback round
        resolveOne(candidate, done);
    });
}

void ModrinthInstaller::resolve(QList<ModCandidate> candidates)
{
    m_aborted = false;
    m_busy = true;
    m_results = std::move(candidates);
    m_resolveIndex = 0;
    m_resolveActive = 0;
    if (m_results.isEmpty()) {
        m_busy = false;
        emit resolved(m_results);
        return;
    }
    // A few lookups in parallel: a 100-mod pack would otherwise take minutes (Modrinth allows 300 requests/min).
    for (int i = 0; i < 6; ++i)
        resolveNext();
}

void ModrinthInstaller::resolveNext()
{
    if (m_aborted)
        return;
    if (m_resolveIndex >= m_results.size()) {
        if (m_resolveActive == 0 && m_busy) {
            m_busy = false;
            emit resolved(m_results);
        }
        return;
    }
    const int index = m_resolveIndex++;
    ++m_resolveActive;
    const auto candidate = m_results[index];
    emit progress(tr("Проверяю на Modrinth (%1 из %2): %3")
                      .arg(index + 1)
                      .arg(m_results.size())
                      .arg(candidate.name.isEmpty() ? candidate.slug : candidate.name));
    resolveOne(candidate, [this, index](ModCandidate result) {
        if (index < m_results.size())
            m_results[index] = result;
        --m_resolveActive;
        resolveNext();
    });
}

QUrl ModrinthInstaller::searchUrl(const QString& query, const QString& category, const QString& index, int offset) const
{
    QUrl url(API + "/search");
    QUrlQuery q;
    if (!query.isEmpty())
        q.addQueryItem("query", query);
    q.addQueryItem("index", index);
    q.addQueryItem("limit", "100");
    q.addQueryItem("offset", QString::number(offset));
    QJsonArray loaders;
    for (const auto& l : loaderList())
        loaders.append("categories:" + l);
    QJsonArray facets{ QJsonArray{ "project_type:mod" }, QJsonArray{ "versions:" + m_mcVersion }, loaders };
    if (!category.isEmpty())
        facets.append(QJsonArray{ "categories:" + category });
    q.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
    url.setQuery(q);
    return url;
}

void ModrinthInstaller::fetchCatalog(int maxEntries)
{
    QList<CatalogJob> jobs;
    for (int offset = 0; offset < std::max(100, maxEntries); offset += 100)
        jobs << CatalogJob{ searchUrl({}, {}, "downloads", offset), 0 };
    startCatalog(jobs);
}

void ModrinthInstaller::fetchThemeCatalog(const QStringList& queries, const QStringList& categories, int pagesPerSearch)
{
    QList<CatalogJob> jobs;
    int group = 0;
    // pages are interleaved (page 1 of every search, then page 2 ...) so the best matches of every search come first
    for (int page = 0; page < std::max(1, pagesPerSearch); ++page) {
        group = 0;
        for (const auto& query : queries)
            jobs << CatalogJob{ searchUrl(query, {}, "relevance", page * 100), group++ };
        for (const auto& category : categories)
            jobs << CatalogJob{ searchUrl({}, category, "downloads", page * 100), group++ };
    }
    startCatalog(jobs);
}

void ModrinthInstaller::startCatalog(QList<CatalogJob> jobs)
{
    m_aborted = false;
    m_busy = true;
    m_catalog.clear();
    m_catalogSlugs.clear();
    m_finishedGroups.clear();
    m_catalogJobs = std::move(jobs);
    m_catalogJobsTotal = m_catalogJobs.size();
    runCatalogJob();
}

void ModrinthInstaller::runCatalogJob()
{
    while (!m_catalogJobs.isEmpty() && m_finishedGroups.contains(m_catalogJobs.first().group))
        m_catalogJobs.removeFirst();
    if (m_catalogJobs.isEmpty()) {
        m_busy = false;
        emit catalogReady(m_catalog);  // an empty catalogue is not fatal: the AI then picks from memory
        return;
    }
    const auto job = m_catalogJobs.takeFirst();
    emit progress(tr("Ищу моды на Modrinth для Minecraft %1 (%2)… найдено %3")
                      .arg(m_mcVersion, m_loader)
                      .arg(m_catalog.size()));
    request(job.url, [this, job](QNetworkReply* reply) {
        const auto root = QJsonDocument::fromJson(reply->readAll()).object();
        const auto hits = root.value("hits").toArray();
        if (reply->error() == QNetworkReply::NoError) {
            static const QSet<QString> skipCategories = { "fabric", "forge", "neoforge", "quilt", "liteloader", "modloader", "rift" };
            for (const auto& value : hits) {
                const auto hit = value.toObject();
                QStringList categories;
                bool library = false;
                for (const auto& c : hit.value("categories").toArray()) {
                    const auto name = c.toString();
                    if (name == "library")
                        library = true;
                    else if (!skipCategories.contains(name))
                        categories << name;
                }
                if (library && categories.isEmpty())
                    continue;  // pure libraries are installed automatically as dependencies
                CatalogEntry entry;
                entry.slug = hit.value("slug").toString();
                entry.title = hit.value("title").toString();
                entry.description = hit.value("description").toString().simplified().left(90);
                entry.categories = categories;
                entry.downloads = hit.value("downloads").toInt();
                if (!entry.slug.isEmpty() && !m_catalogSlugs.contains(entry.slug)) {
                    m_catalogSlugs.insert(entry.slug);
                    m_catalog << entry;
                }
            }
        }
        if (reply->error() != QNetworkReply::NoError || hits.size() < 100)
            m_finishedGroups.insert(job.group);  // no more pages for this search
        runCatalogJob();
    });
}

// ---------------------------------------------------------------- installing

void ModrinthInstaller::scanInstalled(std::function<void()> done)
{
    m_installedProjects.clear();
    QJsonArray hashes;
    QDirIterator it(m_modsDir, { "*.jar", "*.zip", "*.jar.disabled", "*.zip.disabled" }, QDir::Files);
    while (it.hasNext()) {
        const auto hash = sha1Of(it.next());
        if (!hash.isEmpty())
            hashes.append(hash);
    }
    if (hashes.isEmpty()) {
        done();
        return;
    }
    emit progress(tr("Определяю уже установленные моды…"));
    const QJsonObject body{ { "hashes", hashes }, { "algorithm", "sha1" } };
    request(
        QUrl(API + "/version_files"),
        [this, done](QNetworkReply* reply) {
            const auto map = QJsonDocument::fromJson(reply->readAll()).object();
            for (const auto& version : map)
                m_installedProjects.insert(version.toObject().value("project_id").toString());
            done();
        },
        QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void ModrinthInstaller::install(QList<ModCandidate> candidates)
{
    m_aborted = false;
    m_busy = true;
    m_installed.clear();
    m_skipped.clear();
    m_errors.clear();
    m_attemptedDeps.clear();
    m_depRounds = 0;
    m_queue = std::move(candidates);
    QDir().mkpath(m_modsDir);
    scanInstalled([this]() { installNext(); });
}

void ModrinthInstaller::finishInstall()
{
    m_busy = false;
    emit installFinished(m_installed, m_skipped, m_errors);
}

void ModrinthInstaller::installNext()
{
    if (m_queue.isEmpty()) {
        if (m_depRounds < 4) {
            ++m_depRounds;
            checkJarDependencies();
        } else {
            finishInstall();
        }
        return;
    }
    auto candidate = m_queue.takeFirst();
    if (!candidate.resolved) {
        resolveOne(candidate, [this](ModCandidate result) {
            if (!result.resolved) {
                const auto who = result.name.isEmpty() ? result.slug : result.name;
                m_errors << (result.requiredBy.isEmpty() ? QStringLiteral("%1: %2").arg(who, result.error)
                                                         : tr("%1 (нужен для: %2): %3").arg(who, result.requiredBy, result.error));
                installNext();
                return;
            }
            m_queue.prepend(result);
            installNext();
        });
        return;
    }
    if (m_installedProjects.contains(candidate.projectId)) {
        if (!candidate.isDependency)
            m_skipped << candidate.title;
        installNext();
        return;
    }
    download(candidate);
}

void ModrinthInstaller::download(ModCandidate candidate)
{
    emit progress(tr("Скачиваю %1 %2").arg(candidate.title, candidate.versionNumber));
    request(QUrl(candidate.fileUrl), [this, candidate](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) {
            m_errors << QStringLiteral("%1: %2").arg(candidate.title, reply->errorString());
            installNext();
            return;
        }
        const auto data = reply->readAll();
        if (!candidate.sha1.isEmpty() &&
            QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()) != candidate.sha1) {
            m_errors << tr("%1: контрольная сумма не совпала, файл не сохранён").arg(candidate.title);
            installNext();
            return;
        }
        QSaveFile out(QDir(m_modsDir).filePath(QFileInfo(candidate.fileName).fileName()));
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
            m_errors << tr("%1: не удалось записать файл").arg(candidate.title);
            installNext();
            return;
        }
        m_installedProjects.insert(candidate.projectId);
        m_installed << (candidate.isDependency ? tr("%1 (зависимость)").arg(candidate.title) : candidate.title);

        for (const auto& dep : candidate.dependencies) {
            const auto obj = dep.toObject();
            const auto projectId = obj.value("project_id").toString();
            if (obj.value("dependency_type").toString() != "required" || projectId.isEmpty() || m_installedProjects.contains(projectId))
                continue;
            ModCandidate depCandidate;
            depCandidate.slug = projectId;
            depCandidate.title = projectId;
            depCandidate.isDependency = true;
            m_queue.prepend(depCandidate);
        }
        installNext();
    });
}

// ---------------------------------------------------------------- dependencies declared inside jars

void ModrinthInstaller::checkJarDependencies()
{
    emit progress(tr("Проверяю зависимости внутри модов…"));
    auto watcher = new QFutureWatcher<DependencyScanner::Result>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
        watcher->deleteLater();
        if (m_aborted)
            return;
        const auto result = watcher->result();
        bool changed = false;
        const QDir dir(m_modsDir);
        for (auto it = result.missing.cbegin(); it != result.missing.cend(); ++it) {
            const auto& id = it.key();
            if (m_attemptedDeps.contains(id))
                continue;
            m_attemptedDeps.insert(id);
            const auto requiredBy = it.value().join(", ");

            if (result.disabledProviders.contains(id)) {
                // The dependency is already there, just disabled: turn it back on instead of downloading a copy.
                const auto file = result.disabledProviders.value(id);
                if (QFile::rename(dir.filePath(file), dir.filePath(file.chopped(9)))) {
                    m_installed << tr("%1 — включён обратно (нужен для: %2)").arg(file.chopped(9), requiredBy);
                    changed = true;
                }
                continue;
            }
            ModCandidate dep;
            dep.slug = DependencyScanner::modrinthSlugFor(id);
            dep.name = id;
            dep.title = id;
            dep.isDependency = true;
            dep.requiredBy = requiredBy;
            m_queue << dep;
            changed = true;
        }
        if (!changed) {
            finishInstall();
            return;
        }
        installNext();
    });
    const QString modsDir = m_modsDir;
    watcher->setFuture(QtConcurrent::run([modsDir]() { return DependencyScanner::scan(modsDir); }));
}
