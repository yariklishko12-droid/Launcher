#include "VanillaInstanceCreationTask.h"

#include <utility>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"

VanillaCreationTask::VanillaCreationTask(BaseVersion::Ptr version, QString loader, BaseVersion::Ptr loaderVersion)
    : m_version(std::move(version)), m_using_loader(true), m_loader(std::move(loader)), m_loader_version(std::move(loaderVersion))
{}

std::unique_ptr<MinecraftInstance> VanillaCreationTask::createInstance()
{
    setStatus(tr("Creating instance from version %1").arg(m_version->name()));

    auto inst = std::make_unique<MinecraftInstance>(
        m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")), m_stagingPath);
    SettingsObject::Lock lock(inst->settings());

    auto* components = inst->getPackProfile();
    components->buildingFromScratch();
    components->setComponentVersion("net.minecraft", m_version->descriptor(), true);
    if (m_using_loader) {
        components->setComponentVersion(m_loader, m_loader_version->descriptor());
    }

    inst->setName(name());
    inst->setIconKey(m_instIcon);
    components->saveNow();
    if (!m_fpsBoost.files.isEmpty()) {
        setStatus(tr("Installing FPS Boost mods..."));
        const auto mods = FS::PathCombine(inst->gameRoot(), "mods");
        if (!QDir().mkpath(mods)) {
            setError(tr("Could not create the FPS Boost mods folder."));
            return nullptr;
        }
        QJsonArray installed;
        for (const auto& mod : m_fpsBoost.files) {
            if (m_abort) return nullptr;
            // Recheck at the transaction boundary. Never commit a partial or changed bundle.
            QFile source(mod.path);
            QCryptographicHash hash(QCryptographicHash::Sha512);
            if (!source.open(QIODevice::ReadOnly) || !hash.addData(&source) ||
                hash.result().toHex() != mod.sha512.toLatin1().toLower() ||
                mod.filename.contains('/') || mod.filename.contains('\\') || mod.filename.contains(':') ||
                !QFile::copy(mod.path, FS::PathCombine(mods, mod.filename))) {
                setError(tr("Could not install verified FPS Boost mod: %1").arg(mod.filename));
                return nullptr;
            }
            installed.append(QJsonObject{{"project_id", mod.project}, {"version_id", mod.version},
                                          {"filename", mod.filename}, {"sha512", mod.sha512}});
        }
        QSaveFile manifest(FS::PathCombine(m_stagingPath, "fps-boost.json"));
        const auto json = QJsonDocument(QJsonObject{{"minecraft", m_version->descriptor()},
                                                   {"loader", m_loader}, {"loader_version", m_loader_version->descriptor()},
                                                   {"mods", installed}}).toJson();
        if (!manifest.open(QIODevice::WriteOnly) || manifest.write(json) != json.size() || !manifest.commit()) {
            setError(tr("Could not save the FPS Boost mod list."));
            return nullptr;
        }
    }
    return inst;
}
