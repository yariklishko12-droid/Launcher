// SPDX-License-Identifier: GPL-3.0-only
#include "FpsBoost.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include "Application.h"
#include "BuildConfig.h"
#include "net/ChecksumValidator.h"

namespace FpsBoost {
namespace {
QUrl endpoint(const QString& suffix)
{
    return QUrl(BuildConfig.MODRINTH_PROD_URL + suffix);
}
bool safeId(const QString& id)
{
    static const QRegularExpression valid("^[A-Za-z0-9_-]+$");
    return valid.match(id).hasMatch();
}
}
ResolveTask::ResolveTask(QString minecraft, QString loader, QString loaderVersion)
    : m_minecraft(std::move(minecraft)), m_loader(std::move(loader)), m_loaderVersion(std::move(loaderVersion))
{
    setAbortable(true);
}
ResolveTask::~ResolveTask()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_download) {
        disconnect(m_download.get(), nullptr, this, nullptr);
        m_download->abort();
    }
}
bool ResolveTask::abort()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_download) {
        disconnect(m_download.get(), nullptr, this, nullptr);
        m_download->abort();
    }
    return Task::abort();
}
void ResolveTask::executeTask()
{
    m_plan.storage = std::make_shared<QTemporaryDir>();
    if (!m_plan.storage->isValid() || m_minecraft.isEmpty() || m_loaderVersion.isEmpty() ||
        !QStringList{"fabric", "forge", "neoforge", "quilt"}.contains(m_loader)) {
        emitFailed(tr("No supported mod loader is selected."));
        return;
    }
    // Mutually exclusive renderer alternatives. Never install Sodium and Embeddium together.
    m_roots = {"sodium", "embeddium", "lithium", "ferrite-core", "modernfix", "immediatelyfast", "entityculling"};
    nextRoot();
}
void ResolveTask::json(const QUrl& url, std::function<void(QJsonDocument)> done)
{
    if (!isRunning()) return;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    auto reply = APPLICATION->network()->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)] {
        const auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        const auto message = reply->errorString();
        auto bytes = reply->readAll();
        reply->deleteLater();
        m_reply = nullptr;
        if (!isRunning()) return;
        if (httpStatus == 404) {
            done(QJsonDocument(QJsonArray()));
            return;
        }
        if (error != QNetworkReply::NoError) {
            emitFailed(tr("Could not check FPS Boost mods. Check your internet connection and try again.\n%1").arg(message));
            return;
        }
        QJsonParseError parseError;
        auto document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || document.isNull()) {
            emitFailed(tr("Modrinth returned an invalid response. Try again later."));
            return;
        }
        done(document);
    });
}
void ResolveTask::nextRoot()
{
    if (!isRunning()) return;
    if (m_root >= m_roots.size()) {
        m_plan.files = m_selected.values();
        emitSucceeded(); // An empty plan means no verified compatible bundle, not a network error.
        return;
    }
    const auto root = m_roots[m_root++];
    if (root == "embeddium" && m_renderer) {
        nextRoot();
        return;
    }
    setStatus(tr("Checking FPS Boost: %1").arg(root));
    const auto checkpoint = m_selected;
    resolve(root, {}, [this, checkpoint, root](bool ok) {
        if (!ok) m_selected = checkpoint;
        else if (root == "sodium" || root == "embeddium") m_renderer = true;
        nextRoot();
    }, 0);
}
void ResolveTask::resolve(const QString& project, const QString& pinned, Done done, int depth)
{
    if (!isRunning()) return;
    if (depth > 16 || m_selected.size() >= 64 || (project.isEmpty() ? pinned.isEmpty() : !safeId(project)) || (!pinned.isEmpty() && !safeId(pinned))) {
        done(false);
        return;
    }
    if (m_selected.contains(project)) {
        done(pinned.isEmpty() || m_selected.value(project).version == pinned);
        return;
    }
    if (!pinned.isEmpty()) {
        json(endpoint("/version/" + pinned), [this, project, done, depth](QJsonDocument doc) {
            auto obj = doc.object();
            if (!project.isEmpty() && !obj.isEmpty() && obj.value("project_id").toString() != project) {
                done(false);
                return;
            }
            tryVersion(obj.isEmpty() ? QJsonArray() : QJsonArray{obj}, 0, done, depth);
        });
        return;
    }
    auto url = endpoint("/project/" + project + "/version");
    QUrlQuery query;
    query.addQueryItem("game_versions", QString::fromUtf8(QJsonDocument(QJsonArray{m_minecraft}).toJson(QJsonDocument::Compact)));
    query.addQueryItem("loaders", QString::fromUtf8(QJsonDocument(QJsonArray{m_loader}).toJson(QJsonDocument::Compact)));
    query.addQueryItem("include_changelog", "false");
    url.setQuery(query);
    json(url, [this, done, depth](QJsonDocument doc) {
        auto array = doc.array();
        QList<QJsonObject> sorted;
        for (const auto& entry : array) sorted.append(entry.toObject());
        std::sort(sorted.begin(), sorted.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a.value("date_published").toString() > b.value("date_published").toString();
        });
        array = {};
        for (const auto& entry : sorted) array.append(entry);
        tryVersion(array, 0, done, depth);
    });
}
void ResolveTask::tryVersion(QJsonArray versions, int index, Done done, int depth)
{
    if (!isRunning()) return;
    if (index >= versions.size() || index >= 32) {
        done(false);
        return;
    }
    const auto v = versions[index].toObject();
    const auto project = v.value("project_id").toString();
    const auto id = v.value("id").toString();
    auto next = [this, versions, index, done, depth] { tryVersion(versions, index + 1, done, depth); };
    if (!safeId(project) || !safeId(id) || !v.value("game_versions").toArray().contains(m_minecraft) ||
        !v.value("loaders").toArray().contains(m_loader) || v.value("status").toString("listed") != "listed") {
        next();
        return;
    }
    if (m_selected.contains(project)) {
        if (m_selected.value(project).version == id) done(true);
        else next();
        return;
    }
    // A curated root can publish beta releases, but not alpha builds.
    if (v.value("version_type").toString() == "alpha") {
        next();
        return;
    }
    QJsonObject primary;
    for (const auto& entry : v.value("files").toArray()) {
        const auto f = entry.toObject();
        if (!f.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive)) continue;
        if (primary.isEmpty() || f.value("primary").toBool()) primary = f;
        if (f.value("primary").toBool()) break;
    }
    const auto name = primary.value("filename").toString();
    const auto hash = primary.value("hashes").toObject().value("sha512").toString();
    const QUrl url(primary.value("url").toString());
    static const QRegularExpression digest("^[0-9a-fA-F]{128}$");
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(':') ||
        !digest.match(hash).hasMatch() || url.scheme() != "https" || url.host() != "cdn.modrinth.com") {
        next();
        return;
    }
    File file;
    file.path = m_plan.storage->filePath(id + ".jar");
    file.filename = name;
    file.sha512 = hash;
    file.project = project;
    file.version = id;
    file.label = v.value("name").toString(name);
    file.dependencies = v.value("dependencies").toArray();
    auto download = Net::Download::makeFile(url, file.path);
    download->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha512, hash));
    m_download = makeShared<NetJob>(tr("Checking FPS Boost mod"), APPLICATION->network());
    m_download->setAskRetry(false);
    m_download->addNetAction(download);
    connect(m_download.get(), &Task::failed, this, [this](const QString& reason) {
        if (isRunning()) emitFailed(tr("Could not download or verify an FPS Boost mod:\n%1").arg(reason));
    });
    connect(m_download.get(), &Task::succeeded, this, [this, file, next, done, depth]() mutable {
        if (!isRunning()) return;
        if (!readManifest(file.path, m_loader, file.manifest)) {
            next();
            return;
        }
        Manifest platform;
        for (const auto& req : file.manifest.requirements) {
            if (QStringList{"minecraft", "fabricloader", "quilt_loader", "forge", "neoforge"}.contains(req.id))
                platform.requirements.append(req);
        }
        if (!compatible({platform}, m_minecraft, m_loader, m_loaderVersion)) {
            next();
            return;
        }
        const auto checkpoint = m_selected;
        m_selected.insert(file.project, file); // Also breaks required-dependency cycles.
        dependencies(file, 0, [this, checkpoint, next, done, depth](bool ok) {
            // Only the top-level call has a complete graph; inner dependencies may refer to siblings.
            if (ok && (depth != 0 || graphCompatible())) done(true);
            else {
                m_selected = checkpoint;
                next();
            }
        }, depth);
    });
    m_download->start();
}
void ResolveTask::dependencies(const File& file, int index, Done done, int depth)
{
    if (!isRunning()) return;
    if (index >= file.dependencies.size()) {
        done(true);
        return;
    }
    const auto dep = file.dependencies[index].toObject();
    const auto kind = dep.value("dependency_type").toString();
    const auto project = dep.value("project_id").toString();
    const auto pinned = dep.value("version_id").toString();
    if (kind != "required") {
        // Reject any declared incompatible project/version already in the resolved graph.
        bool conflict = false;
        if (kind == "incompatible") {
            for (const auto& installed : m_selected) {
                if ((project.isEmpty() || installed.project == project) && (pinned.isEmpty() || installed.version == pinned))
                    conflict = true;
            }
        }
        if (conflict) done(false);
        else dependencies(file, index + 1, done, depth);
        return;
    }
    if (project.isEmpty() && pinned.isEmpty()) { done(false); return; }
    resolve(project, pinned, [this, file, index, done, depth](bool ok) {
        if (!ok) done(false);
        else dependencies(file, index + 1, done, depth);
    }, depth + 1);
}
bool ResolveTask::graphCompatible() const
{
    QList<Manifest> manifests;
    QSet<QString> names;
    for (const auto& file : m_selected) {
        if (names.contains(file.filename)) return false;
        names.insert(file.filename);
        manifests.append(file.manifest);
        for (const auto& value : file.dependencies) {
            const auto dep = value.toObject();
            if (dep.value("dependency_type").toString() != "incompatible") continue;
            for (const auto& other : m_selected) {
                const auto project = dep.value("project_id").toString();
                const auto version = dep.value("version_id").toString();
                if ((project.isEmpty() || project == other.project) && (version.isEmpty() || version == other.version)) return false;
            }
        }
    }
    return compatible(manifests, m_minecraft, m_loader, m_loaderVersion);
}
}  // namespace FpsBoost
