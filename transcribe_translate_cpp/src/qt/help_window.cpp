#include "help_window.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
#include <QPointer>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace pk {

void show_help_window(QWidget* parent, const QString& title, const QString& html) {
    static QHash<QString, QPointer<QDialog>> open;
    if (QPointer<QDialog> d = open.value(title)) {
        d->raise();
        d->activateWindow();
        return;
    }
    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(title);
    dlg->resize(760, 680);
    auto* layout = new QVBoxLayout(dlg);
    auto* text = new QTextBrowser;
    text->setOpenExternalLinks(true);
    text->setHtml(html);
    layout->addWidget(text, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    layout->addWidget(buttons);
    open.insert(title, dlg);
    dlg->show();
}

QString help_row(const QString& name_html, const QString& text_html) {
    return QStringLiteral("<tr><td valign=\"top\" width=\"175\"><b>%1</b></td><td>%2</td></tr>").arg(name_html, text_html);
}

}  // namespace pk
