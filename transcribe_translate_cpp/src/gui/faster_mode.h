// faster_mode.h - the "Faster mode" choice (speculative decoding with a helper
// model, "tt_engine translate --draft") on the Translate and Speech
// translation tabs.
#pragma once

#include <QComboBox>
#include <QCoreApplication>
#include <QSettings>
#include <QString>

#include "models.h"  // ../translate

namespace tt {

// Off, auto, or one of the helper models (tl::draft_model_table()); the item
// data is --draft's value.
inline QComboBox* makeFasterModeCombo() {
    auto* c = new QComboBox;
    auto tr = [](const char* s) { return QCoreApplication::translate("FasterMode", s); };
    c->addItem(tr("On - the best helper that fits on the graphics card"), QStringLiteral("auto"));
    c->addItem(tr("On - TranslateGemma 4B helper (2.5 GB, best guesses)"), QStringLiteral("translategemma-4b"));
    c->addItem(tr("On - Gemma 3 1B helper (1.1 GB, fits beside 12B on 16 GB)"), QStringLiteral("gemma-3-1b"));
    c->addItem(tr("Off"), QStringLiteral("off"));
    c->setToolTip(tr("For TranslateGemma 12B and 27B: a small helper model guesses a few words ahead and the large "
                     "model checks them all in one step, keeping only what it would have written itself - the same "
                     "translation, sooner. It times itself and drafts only where that pays off. Both models must fit "
                     "on the graphics card: TranslateGemma 4B guesses best; Gemma 3 1B is smaller, so it fits where "
                     "4B doesn't (beside 12B on a 16 GB card), but guesses less well. \"The best helper that fits\" "
                     "takes 4B when there is room, else 1B. The helper is downloaded on first use."));
    return c;
}

// Faster mode applies to TranslateGemma's larger models.
inline bool fasterModeApplies(const tl::ModelInfo* m) {
    return m && m->family == tl::Family::TranslateGemma && m->key != "translategemma-4b";
}

// The saved choice ("draftModel"; before, a yes/no "draft").
inline QString loadFasterMode(const QSettings& s) {
    if (s.contains(QStringLiteral("draftModel"))) return s.value(QStringLiteral("draftModel")).toString();
    return s.value(QStringLiteral("draft"), true).toBool() ? QStringLiteral("auto") : QStringLiteral("off");
}

inline void selectFasterMode(QComboBox* c, const QString& value) {
    const int i = c->findData(value);
    c->setCurrentIndex(i >= 0 ? i : 0);
}

}  // namespace tt
