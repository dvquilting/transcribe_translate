package com.davidv.transcribe.util

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.MediaStore
import com.davidv.transcribe.data.OutputFile
import java.io.File
import java.io.IOException

/**
 * Save a finished result where the user can find it: in Documents/Transcribe
 * (no permission needed), or in a folder they chose in Settings.
 */
object Exporter {
    const val FOLDER = "Documents/Transcribe"

    fun mimeOf(name: String): String = when (name.substringAfterLast('.', "").lowercase()) {
        "txt" -> "text/plain"
        "srt" -> "application/x-subrip"
        "md" -> "text/markdown"
        "pdf" -> "application/pdf"
        "docx" -> "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
        "wav" -> "audio/wav"
        else -> "application/octet-stream"
    }

    fun export(context: Context, file: File, name: String, tree: String?): OutputFile {
        val mime = mimeOf(name)
        val uri = if (tree != null) toTree(context, file, name, mime, Uri.parse(tree)) else toDocuments(context, file, name, mime)
        return OutputFile(name, uri.toString(), mime)
    }

    private fun toDocuments(context: Context, file: File, name: String, mime: String): Uri {
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, name)
            put(MediaStore.MediaColumns.MIME_TYPE, mime)
            put(MediaStore.MediaColumns.RELATIVE_PATH, "$FOLDER/")
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val r = context.contentResolver
        val uri = r.insert(MediaStore.Files.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), values)
            ?: throw IOException("could not create $name in $FOLDER")
        try {
            r.openOutputStream(uri)!!.use { out -> file.inputStream().use { it.copyTo(out) } }
            r.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null)
        } catch (e: Exception) {
            r.delete(uri, null, null)
            throw IOException("could not save $name (${e.message})", e)
        }
        return uri
    }

    private fun toTree(context: Context, file: File, name: String, mime: String, tree: Uri): Uri {
        val parent = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        val r = context.contentResolver
        val uri = DocumentsContract.createDocument(r, parent, mime, name)
            ?: throw IOException("could not create $name in the chosen folder")
        try {
            r.openOutputStream(uri)!!.use { out -> file.inputStream().use { it.copyTo(out) } }
        } catch (e: Exception) {
            DocumentsContract.deleteDocument(r, uri)
            throw IOException("could not save $name (${e.message})", e)
        }
        return uri
    }
}
