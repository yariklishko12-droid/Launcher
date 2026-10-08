// SPDX-License-Identifier: GPL-3.0-only
// Lightweight UI animations: fade-in for windows and page switches.
#pragma once

#include <QObject>

class QWidget;

namespace Animations {
bool enabled();
/** Fades a child widget in (used for page switches). Safe to call repeatedly. */
void fadeIn(QWidget* widget, int durationMs = 180);
/** Installs an application-wide filter that fades top-level windows in when shown. */
void installWindowFade(QObject* application);
}  // namespace Animations
