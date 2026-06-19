// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bitcoinquantumtheme.h>

#include <QApplication>
#include <QPalette>
#include <QString>

namespace GUIUtil {

static QString BitcoinQuantumStyleSheet()
{
    /* Kept deliberately explicit (no Sass) for Qt 5 stylesheet parser. */
    return QStringLiteral(
            "/* ---- Core surfaces ---- */\n"
            "QMainWindow, QMainWindow QWidget#centralwidget { background-color: #06080c; }\n"
            "QWidget { background-color: transparent; color: rgba(230,237,246,0.94); }\n"
            "QFrame { border: none; }\n"

            /* ---- Menu ---- */
            "QMenuBar {\n"
            "  background-color: #06080c;\n"
            "  color: rgba(236,246,254,0.92);\n"
            "  border-bottom: 1px solid rgba(0,240,255,0.22);\n"
            "  padding: 4px;\n"
            "}\n"
            "QMenuBar::item {\n"
            "  padding: 6px 14px;\n"
            "  margin: 0 4px;\n"
            "  border-radius: 8px;\n"
            "}\n"
            "QMenuBar::item:selected {\n"
            "  background-color: rgba(0,240,255,0.10);\n"
            "  color: #b9fefe;\n"
            "}\n"
            "QMenu {\n"
            "  background-color: #0c1017;\n"
            "  color: rgba(236,246,254,0.94);\n"
            "  border: 1px solid rgba(0,240,255,0.32);\n"
            "  border-radius: 10px;\n"
            "  padding: 8px;\n"
            "}\n"
            "QMenu::separator { height: 1px; margin: 6px 2px; background: rgba(0,240,255,0.22); }\n"
            "QMenu::item:selected {\n"
            "  background-color: rgba(0,240,255,0.13);\n"
            "  color: #ccfaff;\n"
            "}\n"

            /* ---- Toolbar tabs ---- */
            "QToolBar {\n"
            "  background-color: #0c1017;\n"
            "  border: none;\n"
            "  border-bottom: 1px solid rgba(0,240,255,0.26);\n"
            "  padding: 10px;\n"
            "  spacing: 10px;\n"
            "}\n"
            "QToolBar::separator {\n"
            "  margin: 0 8px;\n"
            "  width: 1px;\n"
            "  background: rgba(0,240,255,0.28);\n"
            "}\n"
            "QToolButton {\n"
            "  background: transparent;\n"
            "  color: rgba(228,239,246,0.9);\n"
            "  border: 1px solid transparent;\n"
            "  border-radius: 10px;\n"
            "  padding: 8px 12px;\n"
            "  font-weight: 600;\n"
            "}\n"
            "QToolButton:hover {\n"
            "  background-color: rgba(0,240,255,0.06);\n"
            "  border-color: rgba(0,240,255,0.42);\n"
            "}\n"
            "QToolButton:checked {\n"
            "  background-color: rgba(0,240,255,0.13);\n"
            "  border-color: rgba(0,240,255,0.72);\n"
            "  color: #aaf8ff;\n"
            "}\n"

            /* ---- Status ---- */
            "QStatusBar {\n"
            "  background-color: #070a11;\n"
            "  color: rgba(220,229,239,0.88);\n"
            "  border-top: 1px solid rgba(0,240,255,0.26);\n"
            "}\n"
            "QStatusBar QLabel {\n"
            "  padding: 4px;\n"
            "  margin: 3px;\n"
            "}\n"

            /* ---- Splitters ---- */
            "QSplitter::handle { background-color: transparent; }\n"
            "QSplitter::handle:horizontal { width: 4px; }\n"
            "QSplitter::handle:vertical { height: 4px; }\n"

            /* ---- Tabs (RPC debug, dialogs) ---- */
            "QTabWidget::pane {\n"
            "  border: 1px solid rgba(0,240,255,0.35);\n"
            "  border-radius: 10px;\n"
            "  background-color: #0c1017;\n"
            "  top: -6px;\n"
            "}\n"
            "QTabBar::tab {\n"
            "  background: #0a0e15;\n"
            "  color: rgba(220,229,239,0.78);\n"
            "  border: 1px solid rgba(0,240,255,0.22);\n"
            "  border-bottom: none;\n"
            "  border-radius: 8px 8px 0 0;\n"
            "  padding: 8px 22px;\n"
            "}\n"
            "QTabBar::tab:!selected:hover { background-color: rgba(0,240,255,0.06); }\n"
            "QTabBar::tab:selected {\n"
            "  background: #0f141d;\n"
            "  color: #aaf8ff;\n"
            "  border-bottom: 3px solid #00f0ff;\n"
            "}\n"

            /* ---- Lists / tables ---- */
            "QAbstractItemView {\n"
            "  background-color: #0c1017;\n"
            "  alternate-background-color: #0e1622;\n"
            "  color: rgba(230,237,246,0.92);\n"
            "  border: 1px solid rgba(0,240,255,0.32);\n"
            "  border-radius: 10px;\n"
            "  selection-background-color: rgba(0,240,255,0.24);\n"
            "  selection-color: #06080c;\n"
            "  outline: none;\n"
            "}\n"
            "QAbstractItemView::item { padding: 6px; }\n"
            "QAbstractItemView::item:disabled { color: rgba(206,217,229,0.45); }\n"
            "QHeaderView {\n"
            "  background-color: #090d14;\n"
            "}\n"
            "QHeaderView::section {\n"
            "  background-color: #090d14;\n"
            "  color: rgba(206,229,239,0.88);\n"
            "  border: none;\n"
            "  border-right: 1px solid rgba(0,240,255,0.18);\n"
            "  border-bottom: 1px solid rgba(0,240,255,0.42);\n"
            "  padding: 8px;\n"
            "  font-weight: 700;\n"
            "  font-family: \"Consolas\", \"Cascadia Mono\", \"DejaVu Sans Mono\", \"Courier New\", monospace;\n"
            "}\n"
            "QHeaderView::section:last { border-right: none; }\n"

            /* ---- Generic labels emphasis ---- */
            "QLabel[heading=\"true\"] {\n"
            "  letter-spacing: 2px;\n"
            "  font-weight: 800;\n"
            "  color: rgba(0,240,255,0.85);\n"
            "}\n"

            "QLineEdit, QAbstractSpinBox {\n"
            "  background-color: #0c1017;\n"
            "  color: rgba(244,246,252,0.96);\n"
            "  border: 1px solid rgba(0,240,255,0.28);\n"
            "  border-radius: 10px;\n"
            "  padding: 9px;\n"
            "  selection-background-color: #00f0ff;\n"
            "  selection-color: #06080c;\n"
            "}\n"
            "QComboBox {\n"
            "  background-color: #0c1017;\n"
            "  color: rgba(244,246,252,0.96);\n"
            "  border: 1px solid rgba(0,240,255,0.32);\n"
            "  border-radius: 10px;\n"
            "  padding: 9px;\n"
            "  min-height: 1.65em;\n"
            "}\n"
            "QComboBox QAbstractItemView {\n"
            "  background-color: #0c1017;\n"
            "  selection-background-color: rgba(0,240,255,0.34);\n"
            "  selection-color: #06080c;\n"
            "}\n"
            "QComboBox:on, QComboBox:focus { border-color: rgba(0,240,255,0.72); }\n"
            "QPlainTextEdit, QTextEdit {\n"
            "  background-color: #05070b;\n"
            "  color: rgba(220,239,246,0.95);\n"
            "  border: 1px solid rgba(0,240,255,0.32);\n"
            "  border-radius: 10px;\n"
            "  padding: 8px;\n"
            "}\n"

            /* ---- Buttons ---- */
            "QPushButton {\n"
            "  background-color: #0c1017;\n"
            "  color: rgba(244,246,252,0.96);\n"
            "  border: 1px solid rgba(0,240,255,0.42);\n"
            "  border-radius: 10px;\n"
            "  padding: 11px 20px;\n"
            "  font-weight: 700;\n"
            "}\n"
            "QPushButton:!enabled { color: rgba(240,246,252,0.35); }\n"
            "QPushButton:hover {\n"
            "  background-color: #111927;\n"
            "  border-color: rgba(0,240,255,0.74);\n"
            "}\n"
            "QPushButton:focus { border-width: 2px; }\n"
            "QPushButton:default {\n"
            "  background-color: #00f0ff;\n"
            "  color: #06080c;\n"
            "  border: none;\n"
            "}\n"
            "QDialogButtonBox QPushButton { min-width: 96px;\n }\n"

            /* ---- Checkbox / radio ---- */
            "QCheckBox, QRadioButton { spacing: 8px;\n }\n"

            /* ---- Group ---- */
            "QGroupBox {\n"
            "  border: 1px solid rgba(0,240,255,0.42);\n"
            "  border-radius: 12px;\n"
            "  margin-top: 18px;\n"
            "  padding-top: 12px;\n"
            "  padding-bottom: 8px;\n"
            "  background-color: rgba(168,85,247,0.02);\n"
            "}\n"
            "QGroupBox::title {\n"
            "  color: rgba(185,254,254,0.94);\n"
            "  subcontrol-origin: margin;\n"
            "  left: 16px;\n"
            "  padding: 0 8px;\n"
            "}\n"

            /* ---- Scroll bars ---- */
            "QScrollBar:horizontal { height: 14px;\n }\n"
            "QScrollBar:vertical { width: 14px;\n }\n"
            "QScrollBar {\n"
            "  background: transparent;\n"
            "  border-radius: 6px;\n"
            "}\n"
            "QScrollBar::handle {\n"
            "  background: rgba(0,240,255,0.42);\n"
            "  min-height: 40px;\n"
            "  min-width: 40px;\n"
            "  border-radius: 6px;\n"
            "}\n"
            "QScrollBar::handle:hover { background: rgba(0,240,255,0.74); }\n"
            "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }\n"

            /* ---- Progress ---- */
            "QProgressBar {\n"
            "  background-color: #0c1017;\n"
            "  border: 1px solid rgba(0,240,255,0.32);\n"
            "  border-radius: 8px;\n"
            "  text-align: center;\n"
            "  color: rgba(244,246,252,0.96);\n"
            "  padding: 2px;\n"
            "}\n"
            "QProgressBar::chunk {\n"
            "  border-radius: 6px;\n"
            "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,\n"
            "    stop:0 #0891b2, stop:1 #00f0ff);\n"
            "}\n"

            /* ---- Tooltip ---- */
            "QToolTip {\n"
            "  border: 1px solid rgba(0,240,255,0.55);\n"
            "  background-color: #0c1017;\n"
            "  color: #e6fafe;\n"
            "  border-radius: 8px;\n"
            "}\n"

            /* ---- Overview / overlay warning (!) glyphs (need contrast on dark chrome) ---- */
            "QPushButton#labelWalletStatus,\n"
            "QPushButton#labelTransactionsStatus {\n"
            "  background-color: #ffffff;\n"
            "  border: 1px solid rgba(0,240,255,0.45);\n"
            "  border-radius: 14px;\n"
            "  padding: 9px;\n"
            "  margin-left: 5px;\n"
            "  min-width: 42px;\n"
            "  min-height: 42px;\n"
            "}\n"
            "QPushButton#labelWalletStatus:hover,\n"
            "QPushButton#labelTransactionsStatus:hover {\n"
            "  background-color: #f3f4f6;\n"
            "  border-color: rgba(0,240,255,0.72);\n"
            "}\n"
            "QPushButton#warningIcon {\n"
            "  background-color: #ffffff;\n"
            "  border: 1px solid rgba(0,0,0,0.12);\n"
            "  border-radius: 16px;\n"
            "  padding: 13px;\n"
            "  margin: 5px;\n"
            "}\n"
            "QPushButton#warningIcon:disabled {\n"
            "  background-color: #ffffff;\n"
            "  border: 1px solid rgba(0,0,0,0.12);\n"
            "  padding: 13px;\n"
            "  margin: 5px;\n"
            "}\n"

            /* ---- Dialog shells (non-Intro) ---- */
            "QDialog { background-color: #06080c; }\n"
            "QMessageBox QLabel { background: transparent; color: rgba(230,237,246,0.94); }\n");

    /* Keep Intro dialog tweaks from overriding — merge with existing Intro rules handled in intro.cpp */
}

void InstallBitcoinQuantumTheme(QApplication& app)
{
    app.setStyle(QStringLiteral("Fusion"));

    constexpr int kBgR{6};
    constexpr int kBgG{8};
    constexpr int kBgB{12};

    constexpr int kPanelR{12};
    constexpr int kPanelG{16};
    constexpr int kPanelB{23};

    QColor baseBG(kBgR, kBgG, kBgB);
    QColor panel(kPanelR, kPanelG, kPanelB);
    QColor altPanel(17, 25, 39);
    QColor text(232, 240, 250);
    QColor muted(173, 186, 200);
    QColor cyan(0, 240, 255);
    QColor onCyan{kBgR, kBgG, kBgB};

    QPalette pal;
    pal.setColor(QPalette::Window, baseBG);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, panel);
    pal.setColor(QPalette::AlternateBase, altPanel);
    pal.setColor(QPalette::PlaceholderText, muted);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, panel);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::BrightText, QColor(251, 113, 133));
    pal.setColor(QPalette::HighlightedText, onCyan);
    pal.setColor(QPalette::Highlight, cyan);

    pal.setColor(QPalette::Light, QColor(22, 32, 48));
    pal.setColor(QPalette::Midlight, QColor(48, 64, 90));
    pal.setColor(QPalette::Dark, QColor(4, 6, 9));
    pal.setColor(QPalette::Mid, QColor(30, 40, 54));
    pal.setColor(QPalette::Shadow, QColor(0, 0, 0));

    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(160, 170, 182, 150));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(160, 170, 182, 150));
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(cyan.red(), cyan.green(), cyan.blue(), 140));

    pal.setColor(QPalette::ToolTipBase, panel);
    pal.setColor(QPalette::ToolTipText, text);

    pal.setColor(QPalette::Link, QColor(125, 211, 252));
    pal.setColor(QPalette::LinkVisited, QColor(196, 181, 253));

    app.setPalette(pal);
    app.setStyleSheet(BitcoinQuantumStyleSheet());
}

} // namespace GUIUtil
