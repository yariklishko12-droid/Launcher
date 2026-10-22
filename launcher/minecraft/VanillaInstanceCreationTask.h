#pragma once

#include "InstanceCreationTask.h"
#include "minecraft/fpsboost/FpsBoost.h"

#include <utility>

class VanillaCreationTask final : public InstanceCreationTask {
    Q_OBJECT
   public:
    explicit VanillaCreationTask(BaseVersion::Ptr version) : m_version(std::move(version)) {}
    VanillaCreationTask(BaseVersion::Ptr version, QString loader, BaseVersion::Ptr loaderVersion);

    std::unique_ptr<MinecraftInstance> createInstance() override;
    void setFpsBoostPlan(FpsBoost::Plan plan) { m_fpsBoost = std::move(plan); }

   private:
    // Version to update to / create of the instance.
    BaseVersion::Ptr m_version;
    FpsBoost::Plan m_fpsBoost;

    bool m_using_loader = false;
    QString m_loader;
    BaseVersion::Ptr m_loader_version;
};
