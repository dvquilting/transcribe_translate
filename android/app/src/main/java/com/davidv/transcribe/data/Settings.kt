package com.davidv.transcribe.data

import android.content.Context
import android.content.SharedPreferences

/** The user's choices, remembered between runs. */
class Settings(context: Context) {
    private val p: SharedPreferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    /** Downloads over mobile data (otherwise Wi-Fi and other unmetered networks only). */
    var allowMobileData: Boolean
        get() = p.getBoolean("allowMobileData", false)
        set(v) = p.edit().putBoolean("allowMobileData", v).apply()

    /** Processor threads for the models (0 = automatic: the big cores). */
    var threads: Int
        get() = p.getInt("threads", 0)
        set(v) = p.edit().putInt("threads", v).apply()

    /** A folder the user picked for the results (a document tree URI), or null for Documents/Transcribe. */
    var outputTree: String?
        get() = p.getString("outputTree", null)
        set(v) = p.edit().putString("outputTree", v).apply()

    // ---- last choices ----
    var asrModel: String
        get() = p.getString("asrModel", "parakeet-ultra")!!
        set(v) = p.edit().putString("asrModel", v).apply()
    var spokenLanguage: String
        get() = p.getString("spokenLanguage", "auto")!!
        set(v) = p.edit().putString("spokenLanguage", v).apply()
    var formats: Set<String>
        get() = p.getStringSet("formats", setOf("txt"))!!
        set(v) = p.edit().putStringSet("formats", v).apply()
    var polish: Boolean
        get() = p.getBoolean("polish", false)
        set(v) = p.edit().putBoolean("polish", v).apply()
    var wordTiming: Boolean
        get() = p.getBoolean("wordTiming", true)
        set(v) = p.edit().putBoolean("wordTiming", v).apply()
    var translationModel: String
        get() = p.getString("translationModel", "hy-mt2-1.8b")!!
        set(v) = p.edit().putString("translationModel", v).apply()
    var translateFrom: String
        get() = p.getString("translateFrom", "auto")!!
        set(v) = p.edit().putString("translateFrom", v).apply()
    var translateTo: String
        get() = p.getString("translateTo", "en")!!
        set(v) = p.edit().putString("translateTo", v).apply()
    var useContext: Boolean
        get() = p.getBoolean("useContext", true)
        set(v) = p.edit().putBoolean("useContext", v).apply()
}
