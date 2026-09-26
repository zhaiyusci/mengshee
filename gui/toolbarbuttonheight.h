/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_TOOLBARBUTTONHEIGHT_H
#define OKULAR_TOOLBARBUTTONHEIGHT_H

#include <QComboBox>
#include <QEvent>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace Okular
{
// Qt gives text-only tool buttons a shorter size hint than icon buttons.
// Equalize their heights together with toolbar selectors; retain text, icons,
// widths and toolbar styles.
class ToolbarButtonHeight final : public QObject
{
public:
    static void install(QToolBar *toolbar)
    {
        if (!toolbar || toolbar->findChild<QObject *>(QStringLiteral("uniformToolbarButtonHeight"), Qt::FindDirectChildrenOnly)) {
            return;
        }
        new ToolbarButtonHeight(toolbar);
    }

private:
    explicit ToolbarButtonHeight(QToolBar *toolbar)
        : QObject(toolbar)
        , m_toolbar(toolbar)
    {
        setObjectName(QStringLiteral("uniformToolbarButtonHeight"));
        toolbar->installEventFilter(this);
        connect(toolbar, &QToolBar::iconSizeChanged, this, [this] { schedule(); });
        connect(toolbar, &QToolBar::toolButtonStyleChanged, this, [this] { schedule(); });
        schedule();
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::LayoutRequest:
        case QEvent::ChildAdded:
        case QEvent::ChildRemoved:
        case QEvent::ActionAdded:
        case QEvent::ActionChanged:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::Show:
            schedule();
            break;
        default:
            break;
        }
        return QObject::eventFilter(object, event);
    }

    void schedule()
    {
        if (m_pending) {
            return;
        }
        m_pending = true;
        QTimer::singleShot(0, this, [this] {
            m_pending = false;
            QList<QWidget *> controls;
            for (auto *button : m_toolbar->findChildren<QToolButton *>()) controls.append(button);
            for (auto *combo : m_toolbar->findChildren<QComboBox *>()) controls.append(combo);
            int height = 0;
            for (auto *button : controls) {
                // Do not resize buttons belonging to a toolbar popup/menu.
                if (button->window() != m_toolbar->window()) {
                    continue;
                }
                button->installEventFilter(this);
                height = qMax(height, button->sizeHint().height());
            }
            for (auto *button : controls) {
                if (button->window() == m_toolbar->window()
                    && (button->minimumHeight() != height || button->maximumHeight() != height)) {
                    button->setFixedHeight(height);
                }
            }
        });
    }

    QToolBar *m_toolbar;
    bool m_pending = false;
};
}

#endif
