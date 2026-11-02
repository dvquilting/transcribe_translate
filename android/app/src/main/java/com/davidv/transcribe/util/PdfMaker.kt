package com.davidv.transcribe.util

import android.graphics.Color
import android.graphics.Typeface
import android.graphics.pdf.PdfDocument
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import java.io.File
import java.io.FileOutputStream
import java.util.Locale

/**
 * A PDF of a transcript, corrected copy or translation (as the desktop app
 * makes them): the title, an AI note in italics when the text starts with one
 * ("[Spelling and grammar corrected by ...]"), then the paragraphs, with page
 * numbers. Letter paper in the US and Canada, A4 elsewhere; 2.5 cm margins.
 * Android's text layout uses the system fonts, so every script prints.
 */
object PdfMaker {
    fun write(text: String, title: String, dest: File) {
        val paras = text.replace("\r\n", "\n").split(Regex("\n\\s*\n")).map { it.trim() }.filter { it.isNotEmpty() }.toMutableList()
        val note = paras.firstOrNull()?.takeIf { it.startsWith("[") && it.endsWith("]") }?.also { paras.removeAt(0) }

        val country = Locale.getDefault().country
        val letter = country == "US" || country == "CA"
        val pageW = if (letter) 612 else 595
        val pageH = if (letter) 792 else 842
        val margin = 71  // 2.5 cm
        val width = pageW - 2 * margin

        val body = TextPaint(TextPaint.ANTI_ALIAS_FLAG).apply { textSize = 11f; color = Color.BLACK }
        val heading = TextPaint(body).apply { textSize = 20f; typeface = Typeface.DEFAULT_BOLD }
        val italic = TextPaint(body).apply { color = Color.rgb(0x59, 0x59, 0x59); typeface = Typeface.create(Typeface.DEFAULT, Typeface.ITALIC) }
        val pageNo = TextPaint(body).apply { textSize = 9f; color = Color.GRAY }

        fun layout(s: String, p: TextPaint) =
            StaticLayout.Builder.obtain(s, 0, s.length, p, width).setAlignment(Layout.Alignment.ALIGN_NORMAL)
                .setLineSpacing(0f, 1.15f).build()

        val blocks = buildList {
            add(layout(title, heading) to 14f)
            note?.let { add(layout(it, italic) to 10f) }
            paras.forEach { add(layout(it, body) to 8f) }
        }

        val doc = PdfDocument()
        var page: PdfDocument.Page? = null
        var number = 0
        var y = 0f
        fun newPage() {
            page?.let { finish(doc, it, number, pageNo, pageW, pageH, margin) }
            number++
            page = doc.startPage(PdfDocument.PageInfo.Builder(pageW, pageH, number).create())
            y = margin.toFloat()
        }
        newPage()
        val bottom = (pageH - margin).toFloat()
        for ((l, gap) in blocks) {
            // Line by line, so a long paragraph continues on the next page.
            for (line in 0 until l.lineCount) {
                val top = l.getLineTop(line).toFloat()
                val h = l.getLineBottom(line) - top
                if (y + h > bottom) newPage()
                val c = page!!.canvas
                c.save()
                c.translate(margin.toFloat(), y - top)
                c.clipRect(0f, top, width.toFloat(), top + h)
                l.draw(c)
                c.restore()
                y += h
            }
            y += gap
        }
        page?.let { finish(doc, it, number, pageNo, pageW, pageH, margin) }
        val tmp = File(dest.path + ".tmp")
        FileOutputStream(tmp).use { doc.writeTo(it) }
        doc.close()
        if (!tmp.renameTo(dest)) throw java.io.IOException("could not write ${dest.name}")
    }

    private fun finish(doc: PdfDocument, page: PdfDocument.Page, n: Int, p: TextPaint, w: Int, h: Int, margin: Int) {
        val s = n.toString()
        page.canvas.drawText(s, w - margin - p.measureText(s), h - margin / 2f, p)
        doc.finishPage(page)
    }
}
