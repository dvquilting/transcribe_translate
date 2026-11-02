// help.h - the Help window of Speech Translate: how the three steps work and
// which languages each model handles. The model and language tables come from
// the same data the menus use (../common/src/model_catalog.cpp and
// translate_cpp/src/models.cpp), so the help can't drift from what the
// programs actually do.
#pragma once

#include <QString>

class QWidget;

namespace st {

// The help text as HTML (also used by the tests).
QString help_html();

// Show the Help window (non-modal: it can stay open next to the main window).
void show_help(QWidget* parent);

}  // namespace st
