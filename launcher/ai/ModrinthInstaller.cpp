// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthInstaller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>

#include "Application.h"

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
            // The slug may be hallucinated/outdated or has no build for this version: try one search by name.
            if (!candidate.name.isEmpty()) {
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
    m_queue = std::move(candidates);
    m_results.clear();
    resolveNext();
}

void ModrinthInstaller::resolveNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        emit resolved(m_results);
        return;
    }
    auto candidate = m_queue.takeFirst();
    emit progress(tr("Проверяю на Modrinth: %1").arg(candidate.name.isEmpty() ? candidate.slug : candidate.name));
    resolveOne(candidate, [this](ModCandidate result) {
        m_results.append(result);
        resolveNext();
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
    m_queue = std::move(candidates);
    QDir().mkpath(m_modsDir);
    scanInstalled([this]() { installNext(); });
}

void ModrinthInstaller::installNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        emit installFinished(m_installed, m_skipped, m_errors);
        return;
    }
    auto candidate = m_queue.takeFirst();
    if (!candidate.resolved) {
        resolveOne(candidate, [this](ModCandidate result) {
            if (!result.resolved) {
                m_errors << QStringLiteral("%1: %2").arg(result.name.isEmpty() ? result.slug : result.name, result.error);
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
