// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QTemporaryDir>
#include <functional>
#include <memory>

#include "net/NetJob.h"
#include "tasks/Task.h"

namespace FpsBoost {
// Unknown constraint syntax deliberately fails closed.
bool matchesVersion(const QString& version, const QString& requirement, bool maven = false);

struct Requirement {
    QString id;
    QStringList alternatives;
    bool incompatible = false;
    bool maven = false;
};
struct Manifest {
    QMap<QString, QString> provides;
    QList<Requirement> requirements;
};
bool readManifest(const QString& path, const QString& loader, Manifest& result, int depth = 0);
bool compatible(const QList<Manifest>& manifests, const QString& minecraft, const QString& loader, const QString& loaderVersion);
QString apiLoader(const QString& component);

struct File {
    QString path;
    QString filename;
    QString sha512;
    QString project;
    QString version;
    QString label;
    QJsonArray dependencies;
    Manifest manifest;
};
struct Plan {
    std::shared_ptr<QTemporaryDir> storage;
    QList<File> files;
};

// Resolves exact published versions and required dependencies, downloads to an isolated
// temporary directory, then checks actual JAR loader constraints before offering the toggle.
class ResolveTask final : public Task {
    Q_OBJECT
public:
    ResolveTask(QString minecraft, QString loader, QString loaderVersion);
    ~ResolveTask() override;
    Plan plan() const { return m_plan; }
    bool abort() override;
protected:
    void executeTask() override;
private:
    using Done = std::function<void(bool)>;
    void nextRoot();
    void resolve(const QString& project, const QString& pinned, Done done, int depth);
    void tryVersion(QJsonArray versions, int index, Done done, int depth);
    void dependencies(const File& file, int index, Done done, int depth);
    void json(const QUrl& url, std::function<void(QJsonDocument)> done);
    bool graphCompatible() const;
    QString m_minecraft, m_loader, m_loaderVersion;
    QStringList m_roots;
    int m_root = 0;
    bool m_renderer = false;
    Plan m_plan;
    QMap<QString, File> m_selected;
    QPointer<QNetworkReply> m_reply;
    NetJob::Ptr m_download;
};
}  // namespace FpsBoost
