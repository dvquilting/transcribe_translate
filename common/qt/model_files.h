// model_files.h - the model files the programs have downloaded: where they are,
// listing them, and deleting them (for the GUIs' "Model files" window).
//
// Two folders hold downloads:
//   * transcribe_multi (speech recognition, the spelling/summary LLM, voices,
//     the voice separator): CrispASR's model cache;
//   * translate_text (the translation LLMs): Translate's models folder.
// A model is a *.gguf file. With it go its download leftovers: CrispASR's
// "<model>.src" (where it came from) and "<model>.part.<pid>.<n>" (unfinished),
// Translate's "<model>.part" and "<model>.lock". Nothing else in a folder is
// ever touched, and folders are not searched below the top level.
//
// Other programs may keep their own models in the same folder (CrispASR's cache
// is shared by everything built on CrispASR). A model counts as *ours* (see
// ModelOwner) when these programs downloaded it: in CrispASR's cache when its
// "<model>.src" sidecar exists (CrispASR writes one for every download), in
// Translate's folder when its name is one of Translate's model files. Only ours
// are deleted by delete_all_models ("Clean All models"); any model can still be
// deleted on its own.
//
// An unfinished download that is still going is never deleted: a
// "<model>.part.<pid>.<n>" whose process is running, a "<model>.lock" that a
// Translate program holds, and the "<model>.part" it guards.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace pk {

struct ModelFile {
    QString path;    // absolute, native separators
    qint64 bytes = 0;
    bool ours = false;  // downloaded by these programs (see ModelOwner)
};

// How to tell the models these programs downloaded from other programs' models
// in the same folder. The default: none are ours.
struct ModelOwner {
    // CrispASR's cache: a model whose "<model>.src" sidecar exists is ours.
    bool src_sidecar = false;
    // Translate's folder: the file names of its models (tl::model_table()'s
    // `file`), compared case-insensitively.
    QStringList names;
};

// CrispASR's cache, as the tools find it: `override_dir` (the GUI's "Models
// folder" setting) if set, else CRISPASR_CACHE_DIR, CRISPASR_MODELS_DIR, else
// $HOME/.cache/crispasr; on Windows, when HOME is not set, %USERPROFILE% and then
// %LOCALAPPDATA% instead of $HOME (CrispASR's own order).
QString crispasr_models_dir(const QString& override_dir = QString());
// Translate's models folder: `override_dir` if set, else
// %LOCALAPPDATA%\Translate\models ($XDG_CACHE_HOME/translate, ~/.cache/translate).
QString translate_models_dir(const QString& override_dir = QString());

// The models (*.gguf) in `dir`, by name, each marked ours or not. A missing
// folder has none.
QList<ModelFile> list_models(const QString& dir, const ModelOwner& owner = ModelOwner());
// Unfinished downloads and sidecars whose model is gone, of models that are (or
// would be) ours; downloads still in progress are left out.
QList<ModelFile> list_leftovers(const QString& dir, const ModelOwner& owner = ModelOwner());

// Delete one model (ours or not) with its sidecars, except a download of it
// that is still in progress. False (and *error) if the model itself could not
// be deleted (on Windows: a program is using it).
bool delete_model(const QString& model_path, QString* error = nullptr);
// Delete our models and leftovers in `dir` (other programs' models stay).
// Returns how many models were deleted; files that could not be deleted are
// added to *failed.
int delete_all_models(const QString& dir, const ModelOwner& owner, QStringList* failed = nullptr);

// "512 KB", "1.2 GB".
QString human_size(qint64 bytes);

}  // namespace pk
