// SPDX-License-Identifier: GPL-3.0-only
// AI assistant: finds missing mod dependencies by reading metadata inside the mod jars
// (fabric.mod.json, quilt.mod.json, mods.toml, neoforge.mods.toml), including jar-in-jar libraries.
#pragma once

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

namespace DependencyScanner {

struct Result {
    QSet<QString> provided;                       // canonical mod ids present in enabled jars
    QMap<QString, QStringList> missing;           // canonical dependency id -> display names of mods that need it
    QMap<QString, QString> disabledProviders;     // canonical id -> file name of a *disabled* jar that provides it
};

/** Blocking: reads every jar in the folder. Run it off the UI thread. */
Result scan(const QString& modsDir);

/** Maps Fabric API sub-modules ("fabric-networking-api-v1", "fabric") to "fabric-api", lowercases ids. */
QString canonicalId(const QString& id);

/** Best guess of the Modrinth project slug for a mod id. */
QString modrinthSlugFor(const QString& id);

}  // namespace DependencyScanner
