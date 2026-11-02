package com.davidv.transcribe.engine

/** What the native engine reports while it works (called from the engine's thread). */
interface EngineListener {
    fun log(line: String)
    fun progress(phase: String, done: Long, total: Long, unit: String)

    /** The local path of a model file, downloading it first if needed. Throws if it can't be had. */
    fun modelFile(file: String, url: String, display: String, size: String): String
}

/**
 * The C++ engine (src/main/cpp/engine_api.h): transcription, translation and
 * the model list. Requests and results are JSON. transcribe() and translate()
 * block until done; they run in the engine service's own process.
 */
object NativeEngine {
    init {
        System.loadLibrary("pkengine")
    }

    external fun catalog(): String
    external fun paragraphs(text: String): String
    external fun transcribe(request: String, listener: EngineListener): String
    external fun translate(request: String, listener: EngineListener): String

    /** Ask the running job to stop as soon as it can. */
    external fun cancel()
}
