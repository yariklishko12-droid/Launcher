// SPDX-License-Identifier: GPL-3.0-only
#include "PixelDarkTheme.h"

#include <QObject>

QString PixelDarkTheme::name()
{
    return QObject::tr("Pixel Dark");
}

QPalette PixelDarkTheme::colorScheme()
{
    QPalette p;
    const QColor text(0xec, 0xec, 0xf1);
    p.setColor(QPalette::Window, QColor(0x18, 0x18, 0x1b));
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, QColor(0x12, 0x12, 0x15));
    p.setColor(QPalette::AlternateBase, QColor(0x1c, 0x1c, 0x20));
    p.setColor(QPalette::ToolTipBase, QColor(0x26, 0x26, 0x2c));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x2a, 0x2a, 0x31));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    p.setColor(QPalette::Link, QColor(0xb5, 0x93, 0xff));
    p.setColor(QPalette::LinkVisited, QColor(0x9b, 0x6d, 0xff));
    p.setColor(QPalette::Highlight, QColor(0x9b, 0x6d, 0xff));
    p.setColor(QPalette::HighlightedText, QColor(0x14, 0x0f, 0x22));
    p.setColor(QPalette::PlaceholderText, QColor(0x6f, 0x6f, 0x7a));
    p.setColor(QPalette::Mid, QColor(0x2c, 0x2c, 0x34));
    p.setColor(QPalette::Dark, QColor(0x10, 0x10, 0x13));
    p.setColor(QPalette::Light, QColor(0x3a, 0x3a, 0x44));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6b, 0x6b, 0x75));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6b, 0x6b, 0x75));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6b, 0x6b, 0x75));
    return fadeInactive(p, fadeAmount(), fadeColor());
}

QString PixelDarkTheme::appStyleSheet()
{
    // Accent #9b6dff, accent-dark #6a43c9, surfaces #18181b / #1f1f24 / #2a2a31.
    return QStringLiteral(R"QSS(
QMainWindow, QDialog { background: #18181b; }

QToolBar { background: #141417; border: none; border-bottom: 1px solid #26262c; spacing: 6px; padding: 5px; }
QToolBar QToolButton {
    background: #26262c; color: #ececf1; border: none; border-bottom: 3px solid #0e0e11;
    border-radius: 4px; padding: 6px 10px; font-weight: 600;
}
QToolBar QToolButton:hover { background: #31313a; }
QToolBar QToolButton:pressed { border-bottom-width: 1px; margin-top: 2px; }
QToolBar QToolButton:checked { background: #9b6dff; color: #140f22; border-bottom-color: #6a43c9; }
QToolBar QToolButton:disabled { background: #1e1e22; color: #5d5d66; }

QPushButton {
    background: #2a2a31; color: #ececf1; border: none; border-bottom: 3px solid #0e0e11;
    border-radius: 4px; padding: 7px 14px; font-weight: 600; min-height: 18px;
}
QPushButton:hover { background: #35353e; }
QPushButton:pressed { border-bottom-width: 1px; margin-top: 2px; }
QPushButton:checked { background: #3a3150; }
QPushButton:default, QPushButton[accent="true"] { background: #9b6dff; color: #140f22; border-bottom-color: #6a43c9; }
QPushButton:default:hover, QPushButton[accent="true"]:hover { background: #ac85ff; }
QPushButton:disabled, QPushButton[accent="true"]:disabled { background: #222226; color: #5d5d66; border-bottom-color: #141417; }

QLineEdit, QPlainTextEdit, QTextEdit, QTextBrowser, QSpinBox, QDoubleSpinBox, QComboBox, QKeySequenceEdit {
    background: #121215; color: #ececf1; border: 1px solid #2c2c34; border-radius: 6px; padding: 5px 8px;
    selection-background-color: #9b6dff; selection-color: #140f22;
}
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QSpinBox:hover, QComboBox:hover { border-color: #3d3d48; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #9b6dff; }
QComboBox QAbstractItemView {
    background: #1f1f24; border: 1px solid #2c2c34; selection-background-color: #9b6dff; selection-color: #140f22; outline: 0;
}

QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget {
    background: #141417; alternate-background-color: #1a1a1e; border: 1px solid #26262c; border-radius: 8px; outline: 0;
}
QListView::item, QTreeView::item, QListWidget::item { padding: 5px; border-radius: 6px; }
QListView::item:hover, QTreeView::item:hover, QListWidget::item:hover { background: #24242a; }
QListView::item:selected, QTreeView::item:selected, QListWidget::item:selected, QTableView::item:selected {
    background: #9b6dff; color: #140f22;
}
QHeaderView::section {
    background: #1c1c20; color: #a9a9b6; border: none; border-bottom: 1px solid #2a2a31; padding: 6px 8px; font-weight: 600;
}

QTabWidget::pane { border: 1px solid #26262c; border-radius: 8px; top: -1px; background: #18181b; }
QTabBar::tab {
    background: #222227; color: #a9a9b6; padding: 7px 16px; margin-right: 4px;
    border-top-left-radius: 6px; border-top-right-radius: 6px; font-weight: 600;
}
QTabBar::tab:selected { background: #9b6dff; color: #140f22; }
QTabBar::tab:hover:!selected { background: #2c2c33; color: #ececf1; }

QGroupBox { border: 1px solid #2a2a31; border-radius: 8px; margin-top: 16px; padding: 10px 8px 8px 8px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: #c8b6ff; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #3a3a44; border-radius: 4px; min-height: 32px; }
QScrollBar::handle:horizontal { background: #3a3a44; border-radius: 4px; min-width: 32px; }
QScrollBar::handle:hover { background: #9b6dff; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QProgressBar { background: #121215; border: none; border-radius: 5px; min-height: 10px; text-align: center; color: #ececf1; }
QProgressBar::chunk { background: #9b6dff; border-radius: 5px; }

QSlider::groove:horizontal { height: 6px; background: #2a2a31; border-radius: 3px; }
QSlider::sub-page:horizontal { background: #9b6dff; border-radius: 3px; }
QSlider::handle:horizontal { background: #ececf1; width: 14px; margin: -5px 0; border-radius: 7px; }

QMenuBar { background: #141417; }
QMenuBar::item:selected { background: #26262c; border-radius: 4px; }
QMenu { background: #1f1f24; border: 1px solid #2c2c34; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 22px; border-radius: 5px; }
QMenu::item:selected { background: #9b6dff; color: #140f22; }
QMenu::separator { height: 1px; background: #2c2c34; margin: 4px 8px; }

QToolTip { color: #ececf1; background: #26262c; border: 1px solid #9b6dff; padding: 5px; border-radius: 4px; }
QStatusBar { background: #141417; color: #a9a9b6; }
QSplitter::handle { background: #18181b; }
)QSS");
}
