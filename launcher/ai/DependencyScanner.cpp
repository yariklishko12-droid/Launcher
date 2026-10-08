// SPDX-License-Identifier: GPL-3.0-only
#include "DependencyScanner.h"

#include <toml++/toml.h>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>

#include "archive/ArchiveReader.h"

namespace DependencyScanner {

namespace {

const QSet<QString>& builtins()
{
    static const QSet<QString> ids = { "",          "minecraft",   "java",  "fabricloader", "fabric-loader", "quilt_loader",
                                       "forge",     "neoforge",    "fml",   "javafml",      "lowcodefml",    "mixinextras",
                                       "mixin",     "quilt_base",  "mcp",   "minecraftforge" };
    return ids;
}

struct JarInfo {
    QString name;
    QStringList ids;
    QStringList deps;
};

void readFabric(const QByteArray& data, JarInfo& info)
{
    const auto obj = QJsonDocument::fromJson(data).object();
    if (obj.isEmpty())
        return;
    info.ids << obj.value("id").toString();
    for (const auto& p : obj.value("provides").toArray())
        info.ids << p.toString();
    if (info.name.isEmpty())
        info.name = obj.value("name").toString();
    const auto depends = obj.value("depends").toObject();
    for (auto it = depends.begin(); it != depends.end(); ++it)
        info.deps << it.key();
}

void readQuilt(const QByteArray& data, JarInfo& info)
{
    const auto loader = QJsonDocument::fromJson(data).object().value("quilt_loader").toObject();
    if (loader.isEmpty())
        return;
    info.ids << loader.value("id").toString();
    for (const auto& p : loader.value("provides").toArray())
        info.ids << (p.isObject() ? p.toObject().value("id").toString() : p.toString());
    if (info.name.isEmpty())
        info.name = loader.value("metadata").toObject().value("name").toString();
    for (const auto& d : loader.value("depends").toArray()) {
        if (d.isString()) {
            info.deps << d.toString();
        } else if (d.isObject() && !d.toObject().value("optional").toBool()) {
            info.deps << d.toObject().value("id").toString();
        }
    }
}

void readForgeToml(const QByteArray& data, JarInfo& info)
{
    toml::table table;
#if TOML_EXCEPTIONS
    try {
        table = toml::parse(data.toStdString());
    } catch (...) {
        return;
    }
#else
    auto result = toml::parse(data.toStdString());
    if (!result)
        return;
    table = result.table();
#endif
    if (auto mods = table["mods"].as_array()) {
        for (auto& entry : *mods) {
            if (auto mod = entry.as_table()) {
                if (auto id = (*mod)["modId"].as_string())
                    info.ids << QString::fromStdString(id->get());
                if (auto name = (*mod)["displayName"].as_string(); name && info.name.isEmpty())
                    info.name = QString::fromStdString(name->get());
            }
        }
    }
    auto parseList = [&info](toml::array* list) {
        if (!list)
            return;
        for (auto& entry : *list) {
            auto dep = entry.as_table();
            if (!dep)
                continue;
            auto id = (*dep)["modId"].as_string();
            if (!id)
                continue;
            bool required = false;
            if (auto mandatory = (*dep)["mandatory"].as_boolean())
                required = mandatory->get();
            if (auto type = (*dep)["type"].as_string())
                required = type->get() == "required" || type->get() == "REQUIRED";
            if (auto side = (*dep)["side"].as_string(); side && side->get() == "SERVER")
                required = false;
            if (required)
                info.deps << QString::fromStdString(id->get());
        }
    };
    if (auto deps = table["dependencies"].as_table()) {
        for (auto& [key, value] : *deps)
            parseList(value.as_array());
    } else if (auto depsArray = table["dependencies"].as_array()) {
        parseList(depsArray);
    }
}

JarInfo readJar(const QString& path, bool nested = false)
{
    JarInfo info;
    QList<QByteArray> nestedJars;
    MMCZip::ArchiveReader zip(path);
    zip.parse([&](MMCZip::ArchiveReader::File* file, bool&) {
        const auto name = file->filename();
        if (name == "fabric.mod.json") {
            readFabric(file->readAll(), info);
        } else if (name == "quilt.mod.json") {
            readQuilt(file->readAll(), info);
        } else if (name == "META-INF/mods.toml" || name == "META-INF/neoforge.mods.toml") {
            readForgeToml(file->readAll(), info);
        } else if (!nested && name.endsWith(".jar") && (name.startsWith("META-INF/jars/") || name.startsWith("META-INF/jarjar/"))) {
            nestedJars << file->readAll();
        } else {
            file->skip();
        }
        return true;
    });
    // Jar-in-jar libraries provide their ids too (e.g. Fabric API modules, bundled Cloth Config).
    for (const auto& bytes : nestedJars) {
        QTemporaryFile tmp(QDir::temp().filePath("ai-nested-XXXXXX.jar"));
        if (!tmp.open())
            continue;
        tmp.write(bytes);
        tmp.flush();
        info.ids << readJar(tmp.fileName(), true).ids;
    }
    return info;
}

}  // namespace

QString canonicalId(const QString& raw)
{
    const QString id = raw.trimmed().toLower();
    if (id == "fabric" || id == "fabric-api")
        return QStringLiteral("fabric-api");
    if (id.startsWith("fabric-") && !id.startsWith("fabric-language-") && id != "fabric-loader" && id != "fabric-permissions-api-v0")
        return QStringLiteral("fabric-api");  // Fabric API sub-module
    return id;
}

QString modrinthSlugFor(const QString& rawId)
{
    static const QHash<QString, QString> known = {
        { "fabric-api", "fabric-api" },
        { "architectury", "architectury-api" },
        { "cloth-config", "cloth-config" },
        { "cloth_config", "cloth-config" },
        { "cloth-config2", "cloth-config" },
        { "owo", "owo-lib" },
        { "yet_another_config_lib_v3", "yacl" },
        { "yet-another-config-lib", "yacl" },
        { "yacl", "yacl" },
        { "geckolib", "geckolib" },
        { "geckolib3", "geckolib" },
        { "fabric-language-kotlin", "fabric-language-kotlin" },
        { "kotlinforforge", "kotlin-for-forge" },
        { "balm", "balm" },
        { "puzzleslib", "puzzles-lib" },
        { "forgeconfigapiport", "forge-config-api-port" },
        { "terrablender", "terrablender" },
        { "curios", "curios" },
        { "trinkets", "trinkets" },
        { "cardinal-components", "cardinal-components-api" },
        { "resourcefullib", "resourceful-lib" },
        { "resourcefulconfig", "resourceful-config" },
        { "modmenu", "modmenu" },
        { "collective", "collective" },
        { "bookshelf", "bookshelf-lib" },
        { "moonlight", "moonlight" },
        { "creativecore", "creativecore" },
        { "lithostitched", "lithostitched" },
        { "cristellib", "cristel-lib" },
        { "midnightlib", "midnightlib" },
        { "placeholder-api", "placeholder-api" },
        { "playeranimator", "playeranimator" },
        { "player-animator", "playeranimator" },
        { "fzzy_config", "fzzy-config" },
        { "athena", "athena-ctm" },
        { "supermartijn642corelib", "supermartijn642s-core-lib" },
        { "supermartijn642configlib", "supermartijn642s-config-lib" },
        { "prism", "prism-lib" },
        { "citadel", "citadel" },
        { "jei", "jei" },
        { "roughlyenoughitems", "rei" },
        { "libipn", "libipn" },
        { "sophisticatedcore", "sophisticated-core" },
        { "zeta", "zeta" },
        { "forgified-fabric-api", "forgified-fabric-api" },
        { "connectormod", "connector" },
        { "iris", "iris" },
        { "sodium", "sodium" },
        { "indium", "indium" },
        { "ferritecore", "ferrite-core" },
        { "lithium", "lithium" },
        { "kotlin", "fabric-language-kotlin" },
        { "searchables", "searchables" },
        { "konkrete", "konkrete" },
        { "fancymenu", "fancymenu" },
        { "melody", "melody" },
        { "libjf", "libjf" },
        { "satin", "satin-api" },
        { "porting_lib", "porting-lib" },
        { "monkeylib538", "monkeylib538" },
        { "nochatreports", "no-chat-reports" },
        { "voicechat", "simple-voice-chat" },
        { "travelersbackpack", "travelersbackpack" },
        { "patchouli", "patchouli" },
        { "lambdynlights", "lambdynamiclights" },
        { "spectrelib", "spectrelib" },
        { "uranus", "uranus" },
        { "framework", "framework" },
        { "configured", "configured" },
        { "smartbrainlib", "smartbrainlib" },
        { "mru", "mru" },
        { "azurelib", "azurelib" },
        { "elytraslot", "elytra-slot" },
    };
    const auto id = canonicalId(rawId);
    if (auto it = known.find(id); it != known.end())
        return it.value();
    QString slug = id;
    slug.replace('_', '-');
    return slug;
}

Result scan(const QString& modsDir)
{
    Result result;
    QList<JarInfo> enabled;
    const QDir dir(modsDir);
    for (const auto& entry : dir.entryInfoList({ "*.jar", "*.jar.disabled" }, QDir::Files)) {
        auto info = readJar(entry.absoluteFilePath());
        if (info.name.isEmpty())
            info.name = entry.completeBaseName();
        if (entry.fileName().endsWith(".disabled")) {
            for (const auto& id : info.ids)
                result.disabledProviders.insert(canonicalId(id), entry.fileName());
            continue;
        }
        for (const auto& id : info.ids)
            result.provided.insert(canonicalId(id));
        enabled << info;
    }
    for (const auto& info : enabled) {
        for (const auto& dep : info.deps) {
            const auto id = canonicalId(dep);
            if (builtins().contains(id) || result.provided.contains(id))
                continue;
            if (!result.missing[id].contains(info.name))
                result.missing[id] << info.name;
        }
    }
    return result;
}

}  // namespace DependencyScanner
