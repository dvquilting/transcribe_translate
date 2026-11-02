#include "help.h"

#include <string>

#include <QObject>

#include "help_window.h"    // ../qt
#include "model_catalog.h"  // ../common: the speech and voice models
#include "models_help.h"    // translate_cpp: the translation models' languages

namespace st {

namespace {

QString q(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }

}  // namespace

QString help_html(bool all_tables) {
    QString h;
    h += QStringLiteral(
        "<h2>Speech translation</h2>"
        "<p>The Speech translation tab turns the speech in an audio or video file into subtitles in another language, "
        "and can also speak the translation in the speaker's voice. Everything runs on this computer; nothing is "
        "uploaded. Each model is downloaded once, the first time it is needed (<b>Model files</b> shows them and "
        "can delete them).</p>"
        "<p>Choose a file, the models and the languages, then press <b>Translate</b>. The steps run one after the "
        "other; <b>Cancel</b> stops the current step, and the <b>Log</b> tab shows what each step is doing.</p>"

        "<h3>Step 1 - Speech to text</h3>"
        "<p>A speech-recognition model writes down what is said, with the time of each line, as subtitles in the "
        "spoken language.</p>"
        "<ul>"
        "<li><b>Spoken language:</b> most models can detect it (<i>Detect automatically</i>); choosing it is more "
        "reliable, and Cohere Transcribe needs it.</li>"
        "<li>Parakeet works on the whole file at once. The other models listen to it in pieces (30 seconds to a few minutes); "
        "for them, <b>Word-level subtitle timing</b> (under Advanced) uses an extra model to time each word, which "
        "gives better-timed subtitles.</li>"
        "<li>Models that write no punctuation (ARK) get it added by a small punctuation model.</li>"
        "<li><b>Several speakers:</b> a small speaker model (24 MB, any language) finds who speaks when. The "
        "<b>Speakers</b> window then shows each speaker: listen to them, give them names, and mark two that are "
        "really the same person - or detect again with the right number of speakers. A subtitle in which the "
        "speaker changes shows each speaker's part on its own line, starting with a dash.</li>"
        "</ul>"

        "<h3>Step 2 - Translation</h3>"
        "<p>A translation model translates the subtitles into the language under <b>Translate into</b>. The timing "
        "of each subtitle is kept. You can also save the original subtitles, bilingual subtitles (the original line "
        "above its translation) and the translation as plain text.</p>"
        "<ul>"
        "<li>Hy-MT2 and Qwen can be shown the preceding text (Advanced), which helps with names and pronouns.</li>"
        "<li>A model warns when it wasn't trained on the chosen languages; Qwen 3.5 handles all of them.</li>"
        "<li>If a few pieces can't be translated, the subtitles are still saved with those lines in the original "
        "language, and the log says which.</li>"
        "</ul>"

        "<h3>Step 3 - Dubbed audio (optional)</h3>"
        "<p>A voice model speaks each translated subtitle and fits it to the original timing (speeding a line up a "
        "little if it is too long). The result is saved as the video with a new soundtrack, or as audio.</p>"
        "<ul>"
        "<li><b>Voice:</b> <i>Clone the speaker's voice</i> copies the voice from up to about 30 seconds of the "
        "original speech; it needs the confirmation that the speaker agreed. <i>Built-in voice</i> uses the model's own "
        "voice.</li>"
        "<li>With <b>several speakers</b>, each speaker's own voice is cloned from their own speech (tick that every "
        "speaker agreed). A speaker with too little clear speech (no stretch of 3 seconds or more) gets the model's "
        "built-in voice instead.</li>"
        "<li><b>Accent:</b> by default the clone speaks like a native speaker of the new language (Qwen3-TTS and "
        "CosyVoice3). <i>Keep the speaker's original accent</i> keeps it instead.</li>"
        "<li><b>Background:</b> keep the music and sound effects (the voice is separated from them first), the "
        "original audio quietly underneath, or the voice only.</li>"
        "<li>Cloned speech starts with a short spoken AI notice (for a video, as the first subtitle when it doesn't "
        "fit before the first line) and carries an inaudible watermark.</li>"
        "</ul>"

        "<h3>Advanced</h3>"
        "<p><b>Device</b> chooses where the models run: <i>auto</i> uses an NVIDIA graphics card with CUDA, other "
        "graphics cards with Vulkan, or the processor. <b>CPU threads</b> limits the processor threads used "
        "(0 = automatic).</p>");

    // ---- language tables, from the model data ----------------------------------
    h += all_tables ? QStringLiteral("<h2>Languages of each model</h2>") : QStringLiteral("<h2>Languages of the voices</h2>");

    if (all_tables) {  // the Transcribe and Translate parts of the Help window list these already
    h += QStringLiteral("<h3>Speech recognition (step 1)</h3><table cellpadding=\"4\">");
    for (const auto& m : pk::asr_models())
        h += pk::help_row(q(m.display), q(m.description));
    h += QStringLiteral(
        "</table><p>A language a speech model doesn't know is usually transcribed badly, even if the translation "
        "model knows it; for example, none of these models was trained on Haitian Creole.</p>");

    h += QStringLiteral("<h3>Translation (step 2)</h3><table cellpadding=\"4\">");
    h += QString::fromStdString(tl::translation_model_rows());
    h += QStringLiteral(
        "</table><p>A regional variant counts as its language: Spanish (Spain) works with every model that knows "
        "Spanish.</p>");
    }

    h += QStringLiteral("<h3>Voices (step 3)</h3><table cellpadding=\"4\">");
    for (const auto& m : pk::tts_models()) {
        const QString langs = m.langs.empty() ? QStringLiteral("600+ languages, for the languages the others don't cover.")
                                              : QString::fromStdString(tl::language_names(m.langs, true)) + QLatin1Char('.');
        h += pk::help_row(q(m.display), langs);
    }
    h += QStringLiteral(
        "</table><p>A voice model speaks the language of the translation; choose OmniVoice when the others don't "
        "list it.</p>");
    return h;
}

void show_help(QWidget* parent) { pk::show_help_window(parent, QObject::tr("Speech translation - Help"), help_html()); }

}  // namespace st
