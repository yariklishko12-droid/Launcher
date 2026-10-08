// SPDX-License-Identifier: GPL-3.0-only
// Dark theme with violet accent and chunky "pixel" buttons, inspired by modern Minecraft launchers.
#pragma once

#include "FusionTheme.h"

class PixelDarkTheme : public FusionTheme {
   public:
    ~PixelDarkTheme() override = default;

    QString id() override { return "pixel_dark"; }
    QString name() override;
    QString tooltip() override { return {}; }
    bool hasStyleSheet() override { return true; }
    QString appStyleSheet() override;
    QPalette colorScheme() override;
    double fadeAmount() override { return 0.45; }
    QColor fadeColor() override { return QColor(0x18, 0x18, 0x1b); }
};
