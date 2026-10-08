// SPDX-License-Identifier: GPL-3.0-only
#include "Animations.h"

#include <QDialog>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QMainWindow>
#include <QPointer>
#include <QPropertyAnimation>
#include <QWidget>

#include "Application.h"
#include "settings/SettingsObject.h"

namespace Animations {

bool enabled()
{
    auto app = APPLICATION_DYN;
    return app && app->settings() && app->settings()->get("UIAnimations").toBool();
}

void fadeIn(QWidget* widget, int durationMs)
{
    if (!widget || !enabled())
        return;
    // Do not stack effects or replace effects that the widget set itself.
    if (widget->graphicsEffect() && !widget->graphicsEffect()->property("launcherFade").toBool())
        return;
    auto effect = new QGraphicsOpacityEffect(widget);
    effect->setProperty("launcherFade", true);
    effect->setOpacity(0.0);
    widget->setGraphicsEffect(effect);

    auto anim = new QPropertyAnimation(effect, "opacity", effect);
    anim->setDuration(durationMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QWidget> guard(widget);
    QObject::connect(anim, &QPropertyAnimation::finished, widget, [guard, effect]() {
        // Remove the effect afterwards: opacity effects slow down painting of big lists.
        if (guard && guard->graphicsEffect() == effect)
            guard->setGraphicsEffect(nullptr);
    });
    anim->start();
}

namespace {
class WindowFadeFilter : public QObject {
   public:
    using QObject::QObject;

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show && enabled()) {
            auto widget = qobject_cast<QWidget*>(watched);
            if (widget && widget->isWindow() && (qobject_cast<QDialog*>(widget) || qobject_cast<QMainWindow*>(widget)) &&
                !widget->property("launcherFaded").toBool()) {
                widget->setProperty("launcherFaded", true);
                widget->setWindowOpacity(0.0);
                auto anim = new QPropertyAnimation(widget, "windowOpacity", widget);
                anim->setDuration(160);
                anim->setStartValue(0.0);
                anim->setEndValue(1.0);
                anim->setEasingCurve(QEasingCurve::OutCubic);
                anim->start(QAbstractAnimation::DeleteWhenStopped);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};
}  // namespace

void installWindowFade(QObject* application)
{
    application->installEventFilter(new WindowFadeFilter(application));
}

}  // namespace Animations
