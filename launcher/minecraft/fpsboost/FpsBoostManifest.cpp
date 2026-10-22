// SPDX-License-Identifier: GPL-3.0-only
#include "FpsBoost.h"

#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <toml++/toml.h>
#include "archive/ArchiveReader.h"

namespace FpsBoost {
namespace {
// Loader versions use numeric semantic versions. Do not use FlexVer here: e.g.
// 0.16 and 0.16.0 are equal for loader constraints, not separate releases.
std::optional<QList<int>> numeric(QString value)
{
    value = value.trimmed().section('+', 0, 0).section('-', 0, 0);
    static const QRegularExpression valid("^[0-9]+(\\.[0-9]+)*$");
    if (!valid.match(value).hasMatch())
        return {};
    QList<int> parts;
    for (const auto& part : value.split('.')) {
        bool ok = false;
        auto number = part.toInt(&ok);
        if (!ok) return {};
        parts.append(number);
    }
    return parts;
}
int compare(QList<int> a, QList<int> b)
{
    const auto length = qMax(a.size(), b.size());
    while (a.size() < length) a.append(0);
    while (b.size() < length) b.append(0);
    for (qsizetype i = 0; i < length; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}
int compareVersions(const QString& a, const QString& b)
{
    const auto base = compare(*numeric(a), *numeric(b));
    if (base != 0) return base;
    const auto av = a.section('+', 0, 0);
    const auto bv = b.section('+', 0, 0);
    if (!av.contains('-')) return bv.contains('-') ? 1 : 0;
    if (!bv.contains('-')) return -1;
    const auto ap = av.mid(av.indexOf('-') + 1).split('.');
    const auto bp = bv.mid(bv.indexOf('-') + 1).split('.');
    for (qsizetype i = 0; i < qMin(ap.size(), bp.size()); ++i) {
        if (ap[i] == bp[i]) continue;
        bool an = false, bn = false;
        const auto ai = ap[i].toLongLong(&an), bi = bp[i].toLongLong(&bn);
        if (an && bn) return ai < bi ? -1 : 1;
        if (an != bn) return an ? -1 : 1;
        return ap[i] < bp[i] ? -1 : 1;
    }
    return ap.size() == bp.size() ? 0 : ap.size() < bp.size() ? -1 : 1;
}
bool term(const QString& version, QString constraint)
{
    constraint = constraint.trimmed();
    if (constraint == "*" || constraint.isEmpty()) return true;
    static const QRegularExpression expr("^(>=|<=|>|<|=|~|\\^)?([0-9xX*]+(?:\\.[0-9xX*]+)*(?:-[A-Za-z0-9.-]+)?(?:\\+[A-Za-z0-9.-]+)?)$");
    auto match = expr.match(constraint);
    if (!match.hasMatch()) return version == constraint; // exact prerelease only
    auto actual = numeric(version);
    if (!actual) return false;
    const auto op = match.captured(1);
    const auto wanted = match.captured(2);
    const auto baseWanted = wanted.section('+', 0, 0).section('-', 0, 0);
    const auto parts = baseWanted.split('.');
    if (baseWanted.contains('*') || baseWanted.contains('x', Qt::CaseInsensitive)) {
        if (!op.isEmpty() && op != "=") return false;
        for (qsizetype i = 0; i < parts.size(); ++i) {
            if (parts[i] == "*" || parts[i].compare("x", Qt::CaseInsensitive) == 0) {
                for (qsizetype j = i; j < parts.size(); ++j)
                    if (parts[j] != "*" && parts[j].compare("x", Qt::CaseInsensitive) != 0) return false;
                return true;
            }
            if (actual->value(i, 0) != parts[i].toInt()) return false;
        }
        return true;
    }
    auto target = numeric(wanted);
    if (!target) return false;
    const auto order = compareVersions(version, wanted);
    if (op == ">=") return order >= 0;
    if (op == "<=") return order <= 0;
    if (op == ">") return order > 0;
    if (op == "<") return order < 0;
    if (op == "~" || op == "^") {
        auto upper = *target;
        while (upper.size() < 3) upper.append(0);
        qsizetype bump = parts.size() == 1 ? 0 : 1;
        if (op == "^") {
            bump = 0;
            while (bump + 1 < parts.size() && upper[bump] == 0) ++bump;
        }
        ++upper[bump];
        for (qsizetype i = bump + 1; i < upper.size(); ++i) upper[i] = 0;
        return order >= 0 && compare(*actual, upper) < 0;
    }
    return order == 0;
}
QStringList alternatives(const QJsonValue& value)
{
    if (value.isString()) return { value.toString() };
    if (!value.isArray()) return { "!unsupported!" };
    QStringList result;
    for (const auto& item : value.toArray()) {
        if (!item.isString()) return { "!unsupported!" };
        result.append(item.toString());
    }
    return result;
}
}

bool matchesVersion(const QString& version, const QString& requirement, bool maven)
{
    auto input = requirement.trimmed();
    if (input.isEmpty() || input == "*") return true;
    if (maven) {
        if (!input.startsWith('[') && !input.startsWith('(')) return term(version, input);
        static const QRegularExpression interval(R"(([\[(])([^\[\]()]*)([\])]))");
        auto it = interval.globalMatch(input);
        bool found = false;
        qsizetype consumed = 0;
        while (it.hasNext()) {
            auto match = it.next();
            if (!input.mid(consumed, match.capturedStart() - consumed).trimmed().remove(',').isEmpty()) return false;
            consumed = match.capturedEnd();
            const auto body = match.captured(2);
            const auto edges = body.split(',');
            bool accepted = false;
            if (edges.size() == 1) {
                accepted = match.captured(1) == "[" && match.captured(3) == "]" && term(version, "=" + body.trimmed());
            } else if (edges.size() == 2) {
                accepted = (edges[0].trimmed().isEmpty() || term(version, (match.captured(1) == "[" ? ">=" : ">") + edges[0].trimmed())) &&
                           (edges[1].trimmed().isEmpty() || term(version, (match.captured(3) == "]" ? "<=" : "<") + edges[1].trimmed()));
            }
            found = found || accepted;
        }
        return consumed == input.size() && found;
    }
    for (const auto& branch : input.split("||")) {
        bool accepted = true;
        const auto terms = branch.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (terms.isEmpty()) continue;
        for (const auto& part : terms) accepted = accepted && term(version, part);
        if (accepted) return true;
    }
    return false;
}

QString apiLoader(const QString& component)
{
    if (component == "net.fabricmc.fabric-loader") return "fabric";
    if (component == "net.minecraftforge") return "forge";
    if (component == "net.neoforged") return "neoforge";
    if (component == "org.quiltmc.quilt-loader") return "quilt";
    return {};
}

bool readManifest(const QString& path, const QString& loader, Manifest& result, int depth)
{
    if (depth > 8) return false;
    MMCZip::ArchiveReader archive(path);
    auto read = [&archive](const QString& name) {
        auto file = archive.goToFile(name);
        return file ? file->readAll() : QByteArray();
    };
    auto nestedMod = [&](const QString& name) {
        const auto bytes = read(name);
        if (bytes.isEmpty()) return false;
        QTemporaryDir tmp;
        QFile nested(tmp.filePath("nested.jar"));
        if (!tmp.isValid() || !nested.open(QIODevice::WriteOnly) || nested.write(bytes) != bytes.size()) return false;
        nested.close();
        MMCZip::ArchiveReader child(nested.fileName());
        bool isMod = false;
        for (const auto& descriptor : {"fabric.mod.json", "quilt.mod.json", "META-INF/mods.toml", "META-INF/neoforge.mods.toml"}) {
            if (child.goToFile(QString::fromLatin1(descriptor))) { isMod = true; break; }
        }
        return !isMod || readManifest(nested.fileName(), loader, result, depth + 1);
    };
    if (loader == "fabric" || loader == "quilt") {
        auto data = read(loader == "quilt" ? "quilt.mod.json" : "fabric.mod.json");
        bool quilt = !data.isEmpty() && loader == "quilt";
        if (data.isEmpty()) data = read("fabric.mod.json");
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
        auto obj = doc.object();
        if (quilt) {
            auto q = obj.value("quilt_loader").toObject();
            const auto id = q.value("id").toString();
            if (id.isEmpty()) return false;
            result.provides.insert(id, q.value("version").toString());
            for (const auto& dependency : q.value("depends").toArray()) {
                auto dep = dependency.toObject();
                if (dep.value("optional").toBool()) continue;
                const auto depId = dep.value("id").toString();
                if (depId.isEmpty() || dep.contains("unless")) return false;
                result.requirements.append({depId, alternatives(dep.value("versions")), false, false});
            }
            // Nested Quilt alternatives and break rules require a full Quilt solver.
            if (!q.value("breaks").toArray().isEmpty() || !obj.value("jars").toArray().isEmpty()) return false;
        } else {
            const auto id = obj.value("id").toString();
            if (id.isEmpty()) return false;
            result.provides.insert(id, obj.value("version").toString());
            for (const auto& provided : obj.value("provides").toArray())
                result.provides.insert(provided.toString(), obj.value("version").toString());
            for (const auto& key : {QString("depends"), QString("breaks")}) {
                const auto deps = obj.value(key).toObject();
                for (auto it = deps.begin(); it != deps.end(); ++it)
                    result.requirements.append({it.key(), alternatives(it.value()), key == "breaks", false});
            }
            for (const auto& jar : obj.value("jars").toArray()) {
                if (!nestedMod(jar.toObject().value("file").toString())) return false;
            }
        }
        return true;
    }
    auto data = read(loader == "neoforge" ? "META-INF/neoforge.mods.toml" : "META-INF/mods.toml");
    if (data.isEmpty() && loader == "neoforge") data = read("META-INF/mods.toml");
    if (data.isEmpty()) return false;
    try {
        const auto table = toml::parse(data.toStdString());
        auto mods = table["mods"].as_array();
        if (!mods || mods->empty()) return false;
        QString jarVersion;
        for (auto line : QString::fromUtf8(read("META-INF/MANIFEST.MF")).split('\n')) {
            if (line.startsWith("Implementation-Version: ")) jarVersion = line.mid(24).trimmed();
        }
        for (const auto& modNode : *mods) {
            auto modTable = modNode.as_table();
            if (!modTable) return false;
            const auto& mod = *modTable;
            const auto id = QString::fromStdString(mod["modId"].value_or(std::string()));
            auto version = QString::fromStdString(mod["version"].value_or(std::string()));
            if (id.isEmpty()) return false;
            if (version == "${file.jarVersion}") version = jarVersion;
            result.provides.insert(id, version);
        }
        const auto fml = table["modLoader"].value_or(std::string());
        if (loader == "forge" && fml == "javafml")
            result.requirements.append({"forge", {QString::fromStdString(table["loaderVersion"].value_or(std::string("*")))}, false, true});
        if (auto deps = table["dependencies"].as_table()) {
            for (const auto& entry : *deps) {
                const auto list = entry.second.as_array();
                if (!list) return false;
                for (const auto& depNode : *list) {
                    auto depTable = depNode.as_table();
                    if (!depTable) return false;
                    const auto& dep = *depTable;
                    const auto side = dep["side"].value_or(std::string("BOTH"));
                    if (side == "SERVER") continue;
                    const auto type = dep["type"].value_or(std::string());
                    const auto required = dep["mandatory"].value_or(false) || type == "required";
                    if (!required && type != "incompatible") continue;
                    const auto id = QString::fromStdString(dep["modId"].value_or(std::string()));
                    if (id.isEmpty()) return false;
                    result.requirements.append({id, {QString::fromStdString(dep["versionRange"].value_or(std::string("*")))}, type == "incompatible", true});
                }
            }
        }
        const auto jarjar = read("META-INF/jarjar/metadata.json");
        if (!jarjar.isEmpty()) {
            QJsonParseError error;
            const auto metadata = QJsonDocument::fromJson(jarjar, &error);
            if (error.error != QJsonParseError::NoError || !metadata.isObject()) return false;
            for (const auto& entry : metadata.object().value("jars").toArray())
                if (!nestedMod(entry.toObject().value("path").toString())) return false;
        }
        return true;
    } catch (const toml::parse_error&) {
        return false;
    }
}

bool compatible(const QList<Manifest>& manifests, const QString& minecraft, const QString& loader, const QString& loaderVersion)
{
    QMap<QString, QString> provided;
    provided.insert("minecraft", minecraft);
    provided.insert(loader == "fabric" ? "fabricloader" : loader == "quilt" ? "quilt_loader" : loader, loaderVersion);
    for (const auto& manifest : manifests) {
        for (auto it = manifest.provides.begin(); it != manifest.provides.end(); ++it) {
            if (provided.contains(it.key()) && provided.value(it.key()) != it.value()) return false;
            provided.insert(it.key(), it.value());
        }
    }
    for (const auto& manifest : manifests) {
        for (const auto& req : manifest.requirements) {
            if (req.id == "java") continue; // Java is selected separately by the launcher's existing Java resolver.
            bool matches = false;
            if (provided.contains(req.id)) {
                for (const auto& choice : req.alternatives) {
                    if (req.incompatible) {
                        // An unknown break rule must not become a false "no conflict" result.
                        static const QRegularExpression knownTerm("^(>=|<=|>|<|=|~|\\^)?[0-9xX*]+(?:\\.[0-9xX*]+)*(?:-[A-Za-z0-9.-]+)?(?:\\+[A-Za-z0-9.-]+)?$");
                        static const QRegularExpression knownMaven("^[0-9.,()\\[\\] \\t]+$");
                        if (!choice.isEmpty() && choice != "*") {
                            if (!numeric(provided.value(req.id))) return false;
                            if (req.maven && (choice.startsWith('[') || choice.startsWith('('))) {
                                if (!knownMaven.match(choice).hasMatch()) return false;
                            } else {
                                for (const auto& branch : choice.split("||")) {
                                    for (const auto& part : branch.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts))
                                        if (!knownTerm.match(part).hasMatch()) return false;
                                }
                            }
                        }
                    }
                    matches = matches || matchesVersion(provided.value(req.id), choice, req.maven);
                }
            }
            if (req.incompatible ? matches : !matches) return false;
        }
    }
    return true;
}
}  // namespace FpsBoost
