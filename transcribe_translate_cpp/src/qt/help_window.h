// help_window.h - the GUIs' Help window: an HTML page in a window that can stay
// open next to the program (each program writes its own page).
#pragma once

#include <QString>

class QWidget;

namespace pk {

// Show `html` in a window titled `title`. Asking again for the same title brings
// the open window forward instead of opening a second one.
void show_help_window(QWidget* parent, const QString& title, const QString& html);

// One row of a two-column table in a help page: a bold name (may contain <br>)
// and its description (HTML).
QString help_row(const QString& name_html, const QString& text_html);

}  // namespace pk
