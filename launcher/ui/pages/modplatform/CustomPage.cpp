// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "CustomPage.h"
#include "ui_CustomPage.h"

#include <QTabBar>
#include <QSignalBlocker>

#include "Application.h"
#include "Filter.h"
#include "Version.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "ui/dialogs/NewInstanceDialog.h"

CustomPage::CustomPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), dialog(dialog), ui(new Ui::CustomPage)
{
    ui->setupUi(this);
    m_fpsTimer = new QTimer(this);
    m_fpsTimer->setSingleShot(true);
    m_fpsTimer->setInterval(500);
    connect(m_fpsTimer, &QTimer::timeout, this, &CustomPage::checkFpsBoost);
    connect(ui->fpsBoostToggle, &QCheckBox::toggled, this, &CustomPage::suggestCurrent);
    connect(ui->fpsBoostRetry, &QPushButton::clicked, this, [this] {
        m_fpsKey.clear();
        suggestCurrent();
    });
    ui->fpsBoostRetry->hide();
    connect(ui->versionList, &VersionSelectWidget::selectedVersionChanged, this, &CustomPage::setSelectedVersion);
    filterChanged();
    connect(ui->alphaFilter, &QCheckBox::stateChanged, this, &CustomPage::filterChanged);
    connect(ui->betaFilter, &QCheckBox::stateChanged, this, &CustomPage::filterChanged);
    connect(ui->snapshotFilter, &QCheckBox::stateChanged, this, &CustomPage::filterChanged);
    connect(ui->releaseFilter, &QCheckBox::stateChanged, this, &CustomPage::filterChanged);
    connect(ui->experimentsFilter, &QCheckBox::stateChanged, this, &CustomPage::filterChanged);
    connect(ui->refreshBtn, &QPushButton::clicked, this, &CustomPage::refresh);

    connect(ui->loaderVersionList, &VersionSelectWidget::selectedVersionChanged, this, &CustomPage::setSelectedLoaderVersion);
    connect(ui->noneFilter, &QRadioButton::toggled, this, &CustomPage::loaderFilterChanged);
    connect(ui->neoForgeFilter, &QRadioButton::toggled, this, &CustomPage::loaderFilterChanged);
    connect(ui->forgeFilter, &QRadioButton::toggled, this, &CustomPage::loaderFilterChanged);
    connect(ui->fabricFilter, &QRadioButton::toggled, this, &CustomPage::loaderFilterChanged);
    connect(ui->quiltFilter, &QRadioButton::toggled, this, &CustomPage::loaderFilterChanged);
    connect(ui->liteLoaderFilter, &QRadioButton::toggled, this, &CustomPage::loaderFilterChanged);
    connect(ui->loaderRefreshBtn, &QPushButton::clicked, this, &CustomPage::loaderRefresh);
}

void CustomPage::openedImpl()
{
    if (!initialized) {
        auto vlist = APPLICATION->metadataIndex()->get("net.minecraft");
        ui->versionList->initialize(vlist.get());
        initialized = true;
    } else {
        suggestCurrent();
    }
}

void CustomPage::refresh()
{
    ui->versionList->loadList(true);
}

void CustomPage::loaderRefresh()
{
    if (ui->noneFilter->isChecked())
        return;
    ui->loaderVersionList->loadList(true);
}

void CustomPage::filterChanged()
{
    QStringList out;
    if (ui->alphaFilter->isChecked())
        out << "(alpha)";
    if (ui->betaFilter->isChecked())
        out << "(beta)";
    if (ui->snapshotFilter->isChecked())
        out << "(snapshot)";
    if (ui->releaseFilter->isChecked())
        out << "(release)";
    if (ui->experimentsFilter->isChecked())
        out << "(experiment)";
    auto regexp = out.join('|');
    ui->versionList->setFilter(BaseVersionList::TypeRole, Filters::regexp(QRegularExpression(regexp)));
}

void CustomPage::loaderFilterChanged()
{
    m_selectedLoader.clear();
    m_selectedLoaderVersion.reset();
    QString minecraftVersion;
    if (m_selectedVersion) {
        minecraftVersion = m_selectedVersion->descriptor();
    } else {
        ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, "AAA");  // empty list
        ui->loaderVersionList->setEmptyString(tr("No Minecraft version is selected."));
        ui->loaderVersionList->setEmptyMode(VersionListView::String);
        suggestCurrent();
        return;
    }
    if (ui->noneFilter->isChecked()) {
        ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, "AAA");  // empty list
        ui->loaderVersionList->setEmptyString(tr("No mod loader is selected."));
        ui->loaderVersionList->setEmptyMode(VersionListView::String);
        suggestCurrent();
        return;
    } else if (ui->neoForgeFilter->isChecked()) {
        ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, minecraftVersion);
        m_selectedLoader = "net.neoforged";
    } else if (ui->forgeFilter->isChecked()) {
        ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, minecraftVersion);
        m_selectedLoader = "net.minecraftforge";
    } else if (ui->fabricFilter->isChecked()) {
        // FIXME: dirty hack because the launcher is unaware of Fabric's dependencies
        if (Version(minecraftVersion) >= Version("1.14"))  // Fabric/Quilt supported
            ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, "");
        else                                                                                   // Fabric/Quilt unsupported
            ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, "AAA");  // clear list
        m_selectedLoader = "net.fabricmc.fabric-loader";
    } else if (ui->quiltFilter->isChecked()) {
        // FIXME: dirty hack because the launcher is unaware of Quilt's dependencies (same as Fabric)
        if (Version(minecraftVersion) >= Version("1.14"))  // Fabric/Quilt supported
            ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, "");
        else                                                                                   // Fabric/Quilt unsupported
            ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, "AAA");  // clear list
        m_selectedLoader = "org.quiltmc.quilt-loader";
    } else if (ui->liteLoaderFilter->isChecked()) {
        ui->loaderVersionList->setExactFilter(BaseVersionList::ParentVersionRole, minecraftVersion);
        m_selectedLoader = "com.mumfrey.liteloader";
    }

    auto vlist = APPLICATION->metadataIndex()->get(m_selectedLoader);
    ui->loaderVersionList->initialize(vlist.get());
    ui->loaderVersionList->selectRecommended();
    ui->loaderVersionList->setEmptyString(tr("No versions are currently available for Minecraft %1").arg(minecraftVersion));
    m_selectedLoaderVersion = ui->loaderVersionList->selectedVersion();
    suggestCurrent();
}

CustomPage::~CustomPage()
{
    m_fpsTimer->stop();
    if (m_fpsTask) {
        disconnect(m_fpsTask.get(), nullptr, this, nullptr);
        m_fpsTask->abort();
    }
    delete ui;
}

bool CustomPage::shouldDisplay() const
{
    return true;
}

void CustomPage::retranslate()
{
    ui->retranslateUi(this);
}

BaseVersion::Ptr CustomPage::selectedVersion() const
{
    return m_selectedVersion;
}

BaseVersion::Ptr CustomPage::selectedLoaderVersion() const
{
    return m_selectedLoaderVersion;
}

QString CustomPage::selectedLoader() const
{
    return m_selectedLoader;
}

void CustomPage::suggestCurrent()
{
    if (!isOpened) {
        return;
    }

    if (!m_selectedVersion) {
        refreshFpsBoost();
        dialog->setSuggestedPack();
        return;
    }

    refreshFpsBoost();
    VanillaCreationTask* task;
    if (m_selectedLoader.isEmpty() || !m_selectedLoaderVersion)
        task = new VanillaCreationTask(m_selectedVersion);
    else
        task = new VanillaCreationTask(m_selectedVersion, m_selectedLoader, m_selectedLoaderVersion);
    if (ui->fpsBoostToggle->isEnabled() && ui->fpsBoostToggle->isChecked())
        task->setFpsBoostPlan(m_fpsPlan);
    dialog->setSuggestedPack(m_selectedVersion->descriptor(), task);
    dialog->setSuggestedIcon("default");
}

void CustomPage::setSelectedVersion(BaseVersion::Ptr version)
{
    m_selectedVersion = version;
    loaderFilterChanged();
}

void CustomPage::setSelectedLoaderVersion(BaseVersion::Ptr version)
{
    m_selectedLoaderVersion = version;
    suggestCurrent();
}

void CustomPage::refreshFpsBoost()
{
    const auto loader = FpsBoost::apiLoader(m_selectedLoader);
    const auto key = m_selectedVersion && m_selectedLoaderVersion && !loader.isEmpty()
                         ? m_selectedVersion->descriptor() + "/" + loader + "/" + m_selectedLoaderVersion->descriptor()
                         : QString("unsupported");
    if (key == m_fpsKey) return;
    m_fpsKey = key;
    m_fpsTimer->stop();
    if (m_fpsTask) {
        disconnect(m_fpsTask.get(), nullptr, this, nullptr);
        m_fpsTask->abort();
        m_fpsTask.reset();
    }
    m_fpsPlan = {};
    // Block the toggled callback while invalidating: it otherwise rebuilds the creation task recursively.
    const QSignalBlocker blocked(ui->fpsBoostToggle);
    ui->fpsBoostToggle->setChecked(false);
    ui->fpsBoostToggle->setEnabled(false);
    ui->fpsBoostToggle->setToolTip(QString());
    ui->fpsBoostStatus->setToolTip(QString());
    ui->fpsBoostRetry->hide();
    if (key == "unsupported") {
        ui->fpsBoostStatus->setText(tr("Выберите Minecraft и поддерживаемый загрузчик модов."));
        return;
    }
    ui->fpsBoostStatus->setText(tr("Проверка совместимых модов и требований к загрузчику…"));
    m_fpsTimer->start();
}

void CustomPage::checkFpsBoost()
{
    if (!m_selectedVersion || !m_selectedLoaderVersion) return;
    const auto key = m_fpsKey;
    m_fpsTask = makeShared<FpsBoost::ResolveTask>(m_selectedVersion->descriptor(),
                                               FpsBoost::apiLoader(m_selectedLoader), m_selectedLoaderVersion->descriptor());
    connect(m_fpsTask.get(), &Task::status, this, [this, key](const QString& text) {
        if (key == m_fpsKey) ui->fpsBoostStatus->setText(text);
    });
    connect(m_fpsTask.get(), &Task::succeeded, this, [this, key] {
        if (key != m_fpsKey) return;
        m_fpsPlan = m_fpsTask->plan();
        const auto available = !m_fpsPlan.files.isEmpty();
        ui->fpsBoostToggle->setEnabled(available);
        QStringList names;
        for (const auto& file : m_fpsPlan.files) names.append(file.label);
        ui->fpsBoostToggle->setToolTip(names.join("\n"));
        ui->fpsBoostStatus->setText(available ? tr("Готово к установке: %1 модов (включая зависимости).").arg(m_fpsPlan.files.size())
                                             : tr("Для этой версии Minecraft и загрузчика нет проверенного совместимого набора."));
        ui->fpsBoostRetry->show();
        suggestCurrent();
    });
    connect(m_fpsTask.get(), &Task::failed, this, [this, key](const QString& reason) {
        if (key != m_fpsKey) return;
        ui->fpsBoostStatus->setText(tr("Не удалось проверить моды. Проверьте интернет и повторите."));
        ui->fpsBoostStatus->setToolTip(reason);
        ui->fpsBoostRetry->show();
    });
    m_fpsTask->start();
}
