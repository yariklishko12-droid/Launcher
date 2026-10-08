// SPDX-License-Identifier: GPL-3.0-only
#include "AIAssistantPage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "Application.h"
#include "ai/GeminiClient.h"
#include "launch/LaunchTask.h"
#include "launch/LogModel.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/ModIndex.h"
#include "settings/SettingsObject.h"

QList<QPointer<AIAssistantPage>> AIAssistantPage::s_pages;
QSet<QString> AIAssistantPage::s_pendingAutoAnalyze;

namespace {

const char* FIX_PROMPT = R"(You are an expert Minecraft: Java Edition modpack doctor built into a launcher.
You receive the instance info, the exact list of files in the mods folder, and the latest launcher/game log and crash report.
Find the ROOT CAUSE of the crash or problem and propose the minimal, safe set of fixes.

Respond ONLY with a JSON object of this exact shape:
{
  "diagnosis": "short explanation for a regular player, in Russian, Markdown allowed",
  "actions": [
    {"type": "disable_mod", "file": "<exact file name from the list>", "reason": "<Russian>"},
    {"type": "enable_mod", "file": "<exact file name from the list>", "reason": "<Russian>"},
    {"type": "install_mod", "slug": "<Modrinth project slug>", "name": "<mod display name>", "reason": "<Russian>"},
    {"type": "update_mod", "file": "<exact file name>", "slug": "<Modrinth project slug>", "name": "<mod display name>", "reason": "<Russian>"},
    {"type": "set_memory", "mb": 4096, "reason": "<Russian>"},
    {"type": "advice", "text": "<Russian, a manual step the launcher cannot do itself>"}
  ]
}

Rules:
- Use file names EXACTLY as they appear in the list. Never invent files.
- Prefer the fewest changes. Never suggest deleting files; disabling is reversible.
- Missing dependency -> install_mod with the dependency's Modrinth slug (e.g. "fabric-api", "cloth-config", "architectury-api").
- Duplicate, wrong-loader, wrong-version or incompatible mods -> disable_mod (or update_mod if a newer build fixes it).
- OutOfMemoryError -> set_memory (2048..12288 MB).
- Wrong Java version -> advice explaining which Java to select in instance settings.
- If the log shows no real error, say so in diagnosis and return only advice.
- Only use Modrinth slugs you are confident exist.)";

const char* BUILD_PROMPT = R"(You are an expert Minecraft: Java Edition modpack builder built into a launcher.
Given the player's request, the Minecraft version, the mod loader and the already installed mods, pick mods from Modrinth that:
- have builds for EXACTLY this Minecraft version and loader;
- are mutually compatible (e.g. never Sodium together with OptiFine/Embeddium/Rubidium, only one minimap, only one recipe viewer);
- do not duplicate installed mods or each other.
Do NOT list pure library dependencies (Fabric API, Cloth Config, Architectury, etc.) unless the player asked for them: the launcher installs required dependencies automatically.

Respond ONLY with a JSON object:
{
  "summary": "short description of the resulting pack, in Russian, Markdown allowed",
  "mods": [ {"slug": "<Modrinth project slug>", "name": "<display name>", "reason": "<one short sentence in Russian>"} ]
}
At most 40 mods. Only use Modrinth slugs you are confident exist.)";

QString tail(const QString& text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return QStringLiteral("…[начало обрезано]…\n") + text.right(maxChars);
}

QString readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QPushButton* accentButton(const QString& text, QWidget* parent)
{
    auto button = new QPushButton(text, parent);
    button->setProperty("accent", true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

}  // namespace

AIAssistantPage::AIAssistantPage(MinecraftInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    m_ai = new GeminiClient(this);
    connect(m_ai, &GeminiClient::finished, this, &AIAssistantPage::onAiFinished);
    connect(m_ai, &GeminiClient::failed, this, &AIAssistantPage::onAiFailed);
    buildUi();
    s_pages.append(this);
}

AIAssistantPage::~AIAssistantPage()
{
    s_pages.removeAll(QPointer<AIAssistantPage>(this));
    s_pages.removeAll(QPointer<AIAssistantPage>());
}

void AIAssistantPage::requestAutoAnalyze(const QString& instanceId)
{
    for (const auto& page : s_pages) {
        if (page && page->m_instance && page->m_instance->id() == instanceId) {
            QTimer::singleShot(150, page.data(), [page]() {
                if (page) {
                    page->m_tabs->setCurrentIndex(0);
                    page->analyze();
                }
            });
            return;
        }
    }
    s_pendingAutoAnalyze.insert(instanceId);
}

void AIAssistantPage::openedImpl()
{
    refreshHeader();
    if (s_pendingAutoAnalyze.remove(m_instance->id())) {
        m_tabs->setCurrentIndex(0);
        QTimer::singleShot(150, this, &AIAssistantPage::analyze);
    }
}

// ---------------------------------------------------------------- UI

void AIAssistantPage::buildUi()
{
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    m_header = new QLabel(this);
    m_header->setTextFormat(Qt::RichText);
    root->addWidget(m_header);

    m_keyBanner = new QFrame(this);
    m_keyBanner->setObjectName("aiKeyBanner");
    m_keyBanner->setStyleSheet("#aiKeyBanner { background: rgba(155,109,255,0.15); border: 1px solid #9b6dff; border-radius: 8px; }");
    auto bannerLayout = new QHBoxLayout(m_keyBanner);
    auto bannerText = new QLabel(tr("Чтобы помощник заработал, добавьте бесплатный API-ключ Gemini."), m_keyBanner);
    bannerText->setWordWrap(true);
    auto bannerButton = accentButton(tr("Открыть настройки ИИ"), m_keyBanner);
    connect(bannerButton, &QPushButton::clicked, this, [this]() {
        APPLICATION->ShowGlobalSettings(this, "ai");
        refreshHeader();
    });
    bannerLayout->addWidget(bannerText, 1);
    bannerLayout->addWidget(bannerButton);
    root->addWidget(m_keyBanner);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    // --- Repair tab
    auto repair = new QWidget(m_tabs);
    auto repairLayout = new QVBoxLayout(repair);
    auto repairHint = new QLabel(tr("Игра вылетает или не запускается? ИИ прочитает последний лог и краш-репорт, найдёт причину "
                                    "и предложит исправления. Ничего не удаляется: лишние моды только отключаются."),
                                 repair);
    repairHint->setWordWrap(true);
    m_analyzeButton = accentButton(tr("Найти и исправить проблему"), repair);
    connect(m_analyzeButton, &QPushButton::clicked, this, &AIAssistantPage::analyze);
    m_diagnosis = new QTextBrowser(repair);
    m_diagnosis->setOpenExternalLinks(true);
    m_diagnosis->setPlaceholderText(tr("Здесь появится диагноз."));
    m_fixList = new QListWidget(repair);
    m_fixList->setWordWrap(true);
    m_applyButton = accentButton(tr("Применить выбранные исправления"), repair);
    m_applyButton->setEnabled(false);
    connect(m_applyButton, &QPushButton::clicked, this, &AIAssistantPage::applySelectedFixes);

    auto repairTop = new QHBoxLayout();
    repairTop->addWidget(repairHint, 1);
    repairTop->addWidget(m_analyzeButton);
    repairLayout->addLayout(repairTop);
    repairLayout->addWidget(m_diagnosis, 3);
    repairLayout->addWidget(new QLabel(tr("<b>Предлагаемые действия</b>"), repair));
    repairLayout->addWidget(m_fixList, 2);
    repairLayout->addWidget(m_applyButton, 0, Qt::AlignRight);
    m_tabs->addTab(repair, tr("Починить сборку"));

    // --- Build tab
    auto build = new QWidget(m_tabs);
    auto buildLayout = new QVBoxLayout(build);
    m_request = new QPlainTextEdit(build);
    m_request->setPlaceholderText(tr("Опишите, какую сборку хотите. Например: «оптимизация и красивая графика, мини-карта, "
                                     "больше биомов и пара модов на магию»"));
    m_request->setMaximumHeight(90);
    m_suggestButton = accentButton(tr("Подобрать моды"), build);
    connect(m_suggestButton, &QPushButton::clicked, this, &AIAssistantPage::suggestMods);
    m_summary = new QTextBrowser(build);
    m_summary->setMaximumHeight(110);
    m_summary->setPlaceholderText(tr("Здесь появится описание сборки."));
    m_modList = new QListWidget(build);
    m_modList->setWordWrap(true);
    m_installButton = accentButton(tr("Установить выбранные моды"), build);
    m_installButton->setEnabled(false);
    connect(m_installButton, &QPushButton::clicked, this, &AIAssistantPage::installSelected);

    auto buildTop = new QHBoxLayout();
    buildTop->addWidget(m_request, 1);
    buildTop->addWidget(m_suggestButton, 0, Qt::AlignTop);
    buildLayout->addLayout(buildTop);
    buildLayout->addWidget(m_summary);
    buildLayout->addWidget(m_modList, 1);
    buildLayout->addWidget(m_installButton, 0, Qt::AlignRight);
    m_tabs->addTab(build, tr("Собрать с ИИ"));

    // --- Status row
    auto statusRow = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setMaximumWidth(160);
    m_progress->setTextVisible(false);
    m_progress->hide();
    m_cancelButton = new QPushButton(tr("Отмена"), this);
    m_cancelButton->hide();
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        m_ai->abort();
        if (m_installer)
            m_installer->abort();
        m_mode = Mode::Idle;
        setBusy(false, tr("Отменено."));
    });
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_progress);
    statusRow->addWidget(m_cancelButton);
    root->addLayout(statusRow);

    refreshHeader();
}

void AIAssistantPage::refreshHeader()
{
    const auto loaderName = loader();
    const int mods = QDir(m_instance->modsRoot()).entryList({ "*.jar", "*.zip" }, QDir::Files).size();
    m_header->setText(tr("<span style='font-size:15px'><b>%1</b></span> &nbsp;·&nbsp; Minecraft %2 &nbsp;·&nbsp; %3 &nbsp;·&nbsp; "
                         "модов включено: %4 &nbsp;·&nbsp; <span style='color:#a9a9b6'>%5</span>")
                          .arg(m_instance->name().toHtmlEscaped(), mcVersion(),
                               loaderName.isEmpty() ? tr("без загрузчика") : loaderName, QString::number(mods), GeminiClient::currentModel()));
    m_keyBanner->setVisible(!GeminiClient::isConfigured());
    if (loaderName.isEmpty() && m_status->text().isEmpty())
        m_status->setText(tr("В сборке нет загрузчика модов. Установите Fabric, Forge, NeoForge или Quilt на вкладке «Версия»."));
}

void AIAssistantPage::setBusy(bool busy, const QString& status)
{
    m_analyzeButton->setEnabled(!busy);
    m_suggestButton->setEnabled(!busy);
    m_applyButton->setEnabled(!busy && m_fixList->count() > 0);
    m_installButton->setEnabled(!busy && m_modList->count() > 0);
    m_progress->setVisible(busy);
    m_cancelButton->setVisible(busy);
    if (!status.isNull())
        m_status->setText(status);
}

void AIAssistantPage::onAiFinished(const QString& text)
{
    const auto mode = m_mode;
    m_mode = Mode::Idle;
    if (mode == Mode::Diagnose)
        onDiagnosis(text);
    else if (mode == Mode::Suggest)
        onSuggestions(text);
}

void AIAssistantPage::onAiFailed(const QString& error)
{
    m_mode = Mode::Idle;
    setBusy(false, error);
    refreshHeader();
}

void AIAssistantPage::recreateInstaller()
{
    if (m_installer)
        m_installer->deleteLater();
    m_installer = new ModrinthInstaller(mcVersion(), loader(), m_instance->modsRoot(), this);
    connect(m_installer, &ModrinthInstaller::progress, this, [this](const QString& msg) { m_status->setText(msg); });
    connect(m_installer, &ModrinthInstaller::resolved, this, &AIAssistantPage::onResolved);
    connect(m_installer, &ModrinthInstaller::installFinished, this, &AIAssistantPage::onInstallFinished);
}

// ---------------------------------------------------------------- context

QString AIAssistantPage::mcVersion() const
{
    return m_instance->getPackProfile()->getComponentVersion("net.minecraft");
}

QString AIAssistantPage::loader() const
{
    const auto loaders = m_instance->getPackProfile()->getModLoadersList();
    return loaders.isEmpty() ? QString() : ModPlatform::getModLoaderAsString(loaders.first());
}

QString AIAssistantPage::modListText() const
{
    QHash<QString, Mod*> meta;
    if (auto model = m_instance->loaderModList()) {
        for (auto* mod : model->allMods())
            meta.insert(mod->fileinfo().fileName(), mod);
    }
    QStringList lines;
    const auto files = QDir(m_instance->modsRoot()).entryList({ "*.jar", "*.zip", "*.disabled" }, QDir::Files, QDir::Name);
    for (const auto& file : files) {
        QString line = file;
        if (auto* mod = meta.value(file)) {
            line += QStringLiteral(" | id=%1 | %2 | %3").arg(mod->mod_id(), mod->name(), mod->version());
        }
        line += file.endsWith(".disabled") ? QStringLiteral(" | DISABLED") : QStringLiteral(" | enabled");
        lines << line;
        if (lines.size() >= 500) {
            lines << QStringLiteral("… and %1 more").arg(files.size() - 500);
            break;
        }
    }
    return lines.isEmpty() ? QStringLiteral("(no mods)") : lines.join('\n');
}

QString AIAssistantPage::collectLogs() const
{
    QString log;
    if (auto* task = m_instance->getLaunchTask()) {
        if (auto model = task->getLogModel())
            log = model->toPlainText();
    }
    if (log.trimmed().isEmpty())
        log = readFile(QDir(m_instance->gameRoot()).filePath("logs/latest.log"));

    QString crash;
    QDir crashDir(QDir(m_instance->gameRoot()).filePath("crash-reports"));
    const auto reports = crashDir.entryInfoList({ "*.txt" }, QDir::Files, QDir::Time);
    if (!reports.isEmpty()) {
        crash = QStringLiteral("File: %1 (modified %2)\n%3")
                    .arg(reports.first().fileName(), reports.first().lastModified().toString(Qt::ISODate),
                         tail(readFile(reports.first().absoluteFilePath()), 30000));
    }
    QString hsErr;  // native JVM crash
    const auto jvmLogs = QDir(m_instance->gameRoot()).entryInfoList({ "hs_err_pid*.log" }, QDir::Files, QDir::Time);
    if (!jvmLogs.isEmpty())
        hsErr = tail(readFile(jvmLogs.first().absoluteFilePath()), 8000);

    return GeminiClient::redact(QStringLiteral("=== Launcher / game log (tail) ===\n%1\n\n=== Latest crash report ===\n%2\n\n=== JVM crash log ===\n%3")
                                    .arg(log.isEmpty() ? "(none)" : tail(log, 60000), crash.isEmpty() ? "(none)" : crash,
                                         hsErr.isEmpty() ? "(none)" : hsErr));
}

// ---------------------------------------------------------------- repair

void AIAssistantPage::analyze()
{
    if (m_mode != Mode::Idle || (m_installer && m_installer->busy()))
        return;
    refreshHeader();
    if (!GeminiClient::isConfigured()) {
        m_status->setText(tr("Сначала добавьте API-ключ Gemini в настройках."));
        return;
    }
    m_fixList->clear();
    m_diagnosis->clear();
    m_mode = Mode::Diagnose;
    setBusy(true, tr("Читаю логи и спрашиваю ИИ… Обычно это занимает 10–40 секунд."));

    auto settings = m_instance->settings();
    const QString prompt = QStringLiteral("Instance: %1\nMinecraft: %2\nMod loader: %3\nMax memory: %4 MB\nJava: %5\n\nMods folder:\n%6\n\n%7")
                               .arg(m_instance->name(), mcVersion(), loader().isEmpty() ? "none (vanilla)" : loader(),
                                    settings->get("MaxMemAlloc").toString(), settings->get("JavaVersion").toString(), modListText(),
                                    collectLogs());
    m_ai->generate(FIX_PROMPT, prompt, true);
}

void AIAssistantPage::onDiagnosis(const QString& text)
{
    QString error;
    const auto json = GeminiClient::parseJsonObject(text, &error);
    if (json.isEmpty()) {
        setBusy(false, error);
        m_diagnosis->setPlainText(text);
        return;
    }

    QString markdown = json.value("diagnosis").toString();
    const QDir modsDir(m_instance->modsRoot());
    for (const auto& value : json.value("actions").toArray()) {
        const auto action = value.toObject();
        const auto type = action.value("type").toString();
        const auto reason = action.value("reason").toString();
        const auto file = action.value("file").toString();
        QString label;
        bool valid = true;

        if (type == "advice") {
            markdown += QStringLiteral("\n\n> 💡 %1").arg(action.value("text").toString());
            continue;
        } else if (type == "disable_mod") {
            label = tr("Отключить мод «%1»").arg(file);
            valid = modsDir.exists(file);
        } else if (type == "enable_mod") {
            label = tr("Включить мод «%1»").arg(file);
            valid = modsDir.exists(file) || modsDir.exists(file + ".disabled");
        } else if (type == "update_mod") {
            label = tr("Заменить «%1» свежей версией %2").arg(file, action.value("name").toString(action.value("slug").toString()));
            valid = modsDir.exists(file);
        } else if (type == "install_mod") {
            label = tr("Установить с Modrinth: %1").arg(action.value("name").toString(action.value("slug").toString()));
        } else if (type == "set_memory") {
            label = tr("Выделить игре %1 МБ памяти").arg(action.value("mb").toInt());
            valid = action.value("mb").toInt() >= 512;
        } else {
            continue;
        }

        auto item = new QListWidgetItem(reason.isEmpty() ? label : QStringLiteral("%1 — %2").arg(label, reason), m_fixList);
        item->setData(Qt::UserRole, QString::fromUtf8(QJsonDocument(action).toJson(QJsonDocument::Compact)));
        if (valid) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        } else {
            item->setFlags(Qt::NoItemFlags);
            item->setText(item->text() + tr(" (файл не найден — пропущено)"));
        }
    }
    m_diagnosis->setMarkdown(markdown.isEmpty() ? tr("ИИ не нашёл явной причины.") : markdown);
    setBusy(false, m_fixList->count() ? tr("Готово. Проверьте действия и нажмите «Применить».") : tr("Готово. Автоматических исправлений нет."));
}

bool AIAssistantPage::setModEnabled(const QString& fileName, bool enabled)
{
    const QDir dir(m_instance->modsRoot());
    QString name = fileName;
    QString source, target;
    if (enabled) {
        if (!name.endsWith(".disabled"))
            name += ".disabled";
        source = dir.filePath(name);
        target = source.chopped(9);
    } else {
        if (name.endsWith(".disabled"))
            return true;
        source = dir.filePath(name);
        target = source + ".disabled";
    }
    if (!QFile::exists(source) || QFile::exists(target))
        return false;
    return QFile::rename(source, target);
}

void AIAssistantPage::applySelectedFixes()
{
    QList<QJsonObject> actions;
    for (int i = 0; i < m_fixList->count(); ++i) {
        auto item = m_fixList->item(i);
        if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked)
            actions << QJsonDocument::fromJson(item->data(Qt::UserRole).toString().toUtf8()).object();
    }
    if (actions.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Применить исправления"),
                              tr("Применить выбранные исправления (%1)? Отключённые моды можно включить обратно на вкладке «Моды».")
                                  .arg(actions.size())) != QMessageBox::Yes)
        return;

    QStringList done, failed;
    QList<ModCandidate> toInstall;
    for (const auto& action : actions) {
        const auto type = action.value("type").toString();
        const auto file = action.value("file").toString();
        if (type == "disable_mod" || type == "update_mod") {
            (setModEnabled(file, false) ? done : failed) << tr("отключён %1").arg(file);
        } else if (type == "enable_mod") {
            (setModEnabled(file, true) ? done : failed) << tr("включён %1").arg(file);
        } else if (type == "set_memory") {
            const int mb = action.value("mb").toInt();
            m_instance->settings()->set("OverrideMemory", true);
            m_instance->settings()->set("MaxMemAlloc", mb);
            if (m_instance->settings()->get("MinMemAlloc").toInt() > mb)
                m_instance->settings()->set("MinMemAlloc", qMin(512, mb));
            done << tr("память: %1 МБ").arg(mb);
        }
        if (type == "install_mod" || type == "update_mod") {
            ModCandidate candidate;
            candidate.slug = action.value("slug").toString();
            candidate.name = action.value("name").toString();
            if (!candidate.slug.isEmpty() || !candidate.name.isEmpty())
                toInstall << candidate;
        }
    }
    m_instance->loaderModList()->update();
    m_fixList->clear();

    QString summary = tr("Применено: %1").arg(done.isEmpty() ? tr("ничего") : done.join(", "));
    if (!failed.isEmpty())
        summary += tr(". Не удалось: %1").arg(failed.join(", "));
    if (!toInstall.isEmpty()) {
        startInstall(toInstall, summary + tr(". Устанавливаю моды…"));
    } else {
        setBusy(false, summary + tr(". Запустите игру, чтобы проверить."));
    }
}

// ---------------------------------------------------------------- build

void AIAssistantPage::suggestMods()
{
    if (m_mode != Mode::Idle || (m_installer && m_installer->busy()))
        return;
    refreshHeader();
    if (loader().isEmpty()) {
        m_status->setText(tr("Сначала установите загрузчик модов (Fabric, Forge, NeoForge или Quilt) на вкладке «Версия»."));
        return;
    }
    const auto request = m_request->toPlainText().trimmed();
    if (request.isEmpty()) {
        m_status->setText(tr("Опишите, какую сборку вы хотите."));
        m_request->setFocus();
        return;
    }
    if (!GeminiClient::isConfigured()) {
        m_status->setText(tr("Сначала добавьте API-ключ Gemini в настройках."));
        return;
    }
    m_modList->clear();
    m_summary->clear();
    m_resolved.clear();
    m_mode = Mode::Suggest;
    setBusy(true, tr("ИИ подбирает моды…"));
    const QString prompt = QStringLiteral("Player request: %1\n\nMinecraft: %2\nMod loader: %3\n\nAlready installed:\n%4")
                               .arg(request, mcVersion(), loader(), modListText());
    m_ai->generate(BUILD_PROMPT, prompt, true);
}

void AIAssistantPage::onSuggestions(const QString& text)
{
    QString error;
    const auto json = GeminiClient::parseJsonObject(text, &error);
    if (json.isEmpty()) {
        setBusy(false, error);
        return;
    }
    m_summary->setMarkdown(json.value("summary").toString());

    QList<ModCandidate> candidates;
    for (const auto& value : json.value("mods").toArray()) {
        const auto obj = value.toObject();
        ModCandidate candidate;
        candidate.slug = obj.value("slug").toString();
        candidate.name = obj.value("name").toString();
        candidate.reason = obj.value("reason").toString();
        if (!candidate.slug.isEmpty() || !candidate.name.isEmpty())
            candidates << candidate;
    }
    if (candidates.isEmpty()) {
        setBusy(false, tr("ИИ не предложил ни одного мода. Попробуйте описать запрос иначе."));
        return;
    }
    recreateInstaller();
    setBusy(true, tr("Проверяю моды на Modrinth…"));
    m_installer->resolve(candidates);
}

void AIAssistantPage::onResolved(const QList<ModCandidate>& candidates)
{
    m_resolved = candidates;
    m_modList->clear();
    int ok = 0;
    for (int i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        auto item = new QListWidgetItem(m_modList);
        item->setData(Qt::UserRole, i);
        if (c.resolved) {
            ++ok;
            item->setText(c.reason.isEmpty() ? c.title : QStringLiteral("%1 — %2").arg(c.title, c.reason));
            item->setToolTip(QStringLiteral("%1\n%2").arg(c.versionNumber, c.fileName));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        } else {
            item->setText(QStringLiteral("%1 — %2").arg(c.name.isEmpty() ? c.slug : c.name, c.error));
            item->setFlags(Qt::NoItemFlags);
        }
    }
    setBusy(false, tr("Найдено %1 из %2 модов для Minecraft %3. Снимите галочки с лишних и нажмите «Установить».")
                       .arg(ok)
                       .arg(candidates.size())
                       .arg(mcVersion()));
}

void AIAssistantPage::installSelected()
{
    QList<ModCandidate> selected;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto item = m_modList->item(i);
        const int index = item->data(Qt::UserRole).toInt();
        if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked && index < m_resolved.size())
            selected << m_resolved[index];
    }
    if (selected.isEmpty())
        return;
    startInstall(selected, tr("Устанавливаю моды (%1) и их зависимости…").arg(selected.size()));
}

void AIAssistantPage::startInstall(QList<ModCandidate> candidates, const QString& title)
{
    recreateInstaller();
    setBusy(true, title);
    m_installer->install(std::move(candidates));
}

void AIAssistantPage::onInstallFinished(const QStringList& installed, const QStringList& skipped, const QStringList& errors)
{
    m_instance->loaderModList()->update();
    m_modList->clear();
    m_resolved.clear();
    setBusy(false, tr("Установлено: %1, уже были: %2, ошибок: %3.").arg(installed.size()).arg(skipped.size()).arg(errors.size()));
    refreshHeader();

    const auto html = [](const QStringList& list, const QString& sep) {
        QStringList escaped;
        for (const auto& entry : list)
            escaped << entry.toHtmlEscaped();
        return escaped.join(sep);
    };
    QString details;
    if (!installed.isEmpty())
        details += tr("<b>Установлено:</b><br>%1<br><br>").arg(html(installed, "<br>"));
    if (!skipped.isEmpty())
        details += tr("<b>Уже были установлены:</b><br>%1<br><br>").arg(html(skipped, ", "));
    if (!errors.isEmpty())
        details += tr("<b>Не получилось:</b><br>%1").arg(html(errors, "<br>"));
    if (!details.isEmpty())
        QMessageBox::information(this, tr("ИИ-помощник"), details);
}
