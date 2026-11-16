#!/usr/bin/env python3
"""
transdiff.py - side-by-side, word-level diff for comparing two translations.

Line breaks and whitespace are ignored completely: both texts are split into
words, compared word by word, then reflowed and aligned sentence by sentence,
so one changed word highlights only that word instead of derailing the rest
of the comparison. Case, quote/dash style and punctuation can optionally be
ignored as well (all three are on by default).

Usage:
    python transdiff.py [fileA] [fileB]

Keys:
    F3 / Shift+F3      next / previous difference
    Ctrl + / Ctrl -    larger / smaller text (also Ctrl + mouse wheel)
"""
import bisect
import codecs
import os
import re
import sys
import unicodedata
from difflib import SequenceMatcher

import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter import font as tkfont

# ---------------------------------------------------------------------------
# Tokenizing and comparison
# ---------------------------------------------------------------------------

# Numbers with separators, words (with internal apostrophes), runs of dots or
# hyphens, or any single other non-space character.
TOKEN_RE = re.compile(r"\d+(?:[.,:]\d+)+|\w+(?:['’]\w+)*|\.{2,}|-{2,}|[^\w\s]")

SENTENCE_END = {".", "!", "?", "…", "。", "！", "？"}
CLOSERS = {'"', "'", "”", "’", "»", "›", ")", "]", "}"}

QUOTE_DASH = str.maketrans({
    "‘": "'", "’": "'", "‚": "'", "‛": "'", "′": "'",
    "`": "'", "´": "'", "‹": "'", "›": "'",
    "“": '"', "”": '"', "„": '"', "‟": '"', "″": '"',
    "«": '"', "»": '"',
    "‐": "-", "‑": "-", "‒": "-", "–": "-", "—": "-",
    "―": "-", "−": "-", "⸺": "-", "⸻": "-",
})


def make_key(tok, icase, norm, nopunct):
    """The form of a token that is actually compared ('' = ignore it)."""
    k = tok
    if norm:
        k = unicodedata.normalize("NFKC", k).translate(QUOTE_DASH)
        if k and set(k) == {"-"}:
            k = "-"
    if icase:
        k = k.casefold()
    if nopunct:
        k = "".join(ch for ch in k if ch.isalnum())
    return k


class Doc:
    """A text split into tokens of (text, space_before, paragraph_after)."""

    def __init__(self, text):
        toks = []
        matches = list(TOKEN_RE.finditer(text))
        for idx, m in enumerate(matches):
            space = idx > 0 and m.start() > matches[idx - 1].end()
            nxt = matches[idx + 1].start() if idx + 1 < len(matches) else len(text)
            para = text.count("\n", m.end(), nxt) >= 2
            toks.append((m.group(), space, para))
        self.toks = toks

    def index(self, icase, norm, nopunct):
        """Build the compared key sequence. Ignored tokens (punctuation) are
        attached for display to a neighbouring compared token: openers such as
        a quote glued to the next word go forward, everything else backward."""
        toks = self.toks
        comp, keys = [], []
        for i, (t, _, _) in enumerate(toks):
            k = make_key(t, icase, norm, nopunct)
            if k:
                comp.append(i)
                keys.append(k)
        starts, prev = [], -1
        for c in comp:
            s = c
            if prev == -1:
                s = 0
            else:
                j = c - 1
                while j > prev and not toks[j + 1][1]:
                    if toks[j][1]:
                        s = j
                        break
                    j -= 1
            starts.append(s)
            prev = c
        starts.append(len(toks))
        self.keys, self.starts = keys, starts

    def span(self, i1, i2):
        """Display tokens for compared tokens i1..i2 (exclusive)."""
        return self.toks[self.starts[i1]:self.starts[i2]]


def _diff(a, b, alo, ahi, blo, bhi, out, depth):
    sa, sb = a[alo:ahi], b[blo:bhi]
    # For big inputs let difflib skip very common words for speed, then
    # re-diff the differing regions exactly to recover those words.
    junk = len(sa) * len(sb) > 2_000_000 and depth < 6
    sm = SequenceMatcher(None, sa, sb, autojunk=junk)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "replace" and junk:
            _diff(a, b, alo + i1, alo + i2, blo + j1, blo + j2, out, depth + 1)
        else:
            out.append((tag, alo + i1, alo + i2, blo + j1, blo + j2))


def word_diff(a, b, min_match=2):
    """Opcodes ('equal' or 'change', i1, i2, j1, j2) between key lists.
    Matching runs shorter than min_match words that sit between two changes
    are folded into the change, which removes 'chaff' like a lone 'the'."""
    raw = []
    _diff(a, b, 0, len(a), 0, len(b), raw, 0)
    ops = []
    last = len(raw) - 1
    for idx, (tag, i1, i2, j1, j2) in enumerate(raw):
        if tag != "equal":
            tag = "change"
        elif 0 < idx < last and i2 - i1 < min_match:
            tag = "change"
        if ops and ops[-1][0] == tag:
            p = ops[-1]
            ops[-1] = (tag, p[1], i2, p[3], j2)
        else:
            ops.append((tag, i1, i2, j1, j2))
    return ops


def ends_sentence(span):
    return any(t in SENTENCE_END or t.startswith("..") or para for t, _, para in span)


def is_glued_closer(span):
    return bool(span) and span[0][0] in CLOSERS and not span[0][1]


class RowBuilder:
    """Collects aligned rows: (left segments, right segments, changed)."""

    def __init__(self):
        self.rows = []
        self._new()

    def _new(self):
        self.sides = ([], [])
        self.changed = False

    def add(self, side, toks, tag):
        segs = self.sides[side]
        text = "".join((" " if sp else "") + t for t, sp, _ in toks)
        if not segs:
            text = text.lstrip(" ")
        elif tag and text.startswith(" "):
            self._push(segs, " ", None)  # don't highlight the gap before a word
            text = text[1:]
        if text:
            self._push(segs, text, tag)

    def gap(self, side):
        segs = self.sides[side]
        if segs:
            self._push(segs, " ", None)
        self._push(segs, "   ", "gap")

    @staticmethod
    def _push(segs, text, tag):
        if segs and segs[-1][1] == tag:
            segs[-1] = (segs[-1][0] + text, tag)
        else:
            segs.append((text, tag))

    def end_row(self):
        if self.sides[0] or self.sides[1]:
            segs = self.sides[0] + self.sides[1]
            total = sum(len(s) for s, tag in segs if tag != "gap") or 1
            differ = sum(len(s) for s, tag in segs if tag in ("del", "ins"))
            self.rows.append((self.sides[0], self.sides[1], self.changed, differ / total))
        self._new()


def build_rows(A, B, ops):
    rb = RowBuilder()
    pending_break = False
    for tag, i1, i2, j1, j2 in ops:
        if tag == "equal":
            for o in range(i2 - i1):
                sa, sb = A.span(i1 + o, i1 + o + 1), B.span(j1 + o, j1 + o + 1)
                # Break after a sentence end, but keep a closing quote or
                # bracket with the sentence it closes.
                if pending_break and not is_glued_closer(sa):
                    rb.end_row()
                    pending_break = False
                rb.add(0, sa, None)
                rb.add(1, sb, None)
                if ends_sentence(sa) or ends_sentence(sb):
                    pending_break = True
        else:
            if pending_break:
                rb.end_row()
                pending_break = False
            # If both sides of a change contain sentence ends, split the change
            # there (1st end with 1st end, ...) so rows stay sentence-sized.
            pairs = list(zip(_sentence_cuts(A, i1, i2), _sentence_cuts(B, j1, j2)))
            bounds_a = [i1] + [p for p, _ in pairs] + [i2]
            bounds_b = [j1] + [q for _, q in pairs] + [j2]
            for n in range(len(bounds_a) - 1):
                if n:
                    rb.end_row()
                a1, a2, b1, b2 = bounds_a[n], bounds_a[n + 1], bounds_b[n], bounds_b[n + 1]
                rb.changed = True
                if a2 > a1:
                    rb.add(0, A.span(a1, a2), "del")
                else:
                    rb.gap(0)
                if b2 > b1:
                    rb.add(1, B.span(b1, b2), "ins")
                else:
                    rb.gap(1)
            pending_break = ((i2 > i1 and ends_sentence(A.span(i2 - 1, i2))) or
                             (j2 > j1 and ends_sentence(B.span(j2 - 1, j2))))
    rb.end_row()
    return rb.rows


def _sentence_cuts(D, i1, i2):
    """Positions inside (i1, i2) just after a token that ends a sentence."""
    return [k + 1 for k in range(i1, i2 - 1) if ends_sentence(D.span(k, k + 1))]


def read_text(path):
    with open(path, "rb") as f:
        data = f.read()
    if data.startswith((codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE)):
        return data.decode("utf-16")
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------

COLORS = {
    "rowA": "#ffffff",
    "rowB": "#f3f5f8",
    "flash": "#fff1a8",
    "del": "#ffc4c4",
    "ins": "#b8efc0",
    "gap": "#b9bec5",
    "minimap_bg": "#eceef1",
    "minimap_mark": "#e06666",
}


class App:
    def __init__(self, root, path_a=None, path_b=None):
        self.root = root
        self.paths = [path_a, path_b]
        self.icase = tk.BooleanVar(value=True)
        self.norm = tk.BooleanVar(value=True)
        self.nopunct = tk.BooleanVar(value=True)
        self.min_match = tk.StringVar(value="2")
        self.rows = []
        self.changed_rows = []
        self.row_tops = []
        self.row_heights = []
        self._relayout_job = None
        self._last_width = None
        self._flash_job = None
        self._cur = -1

        self.font = tkfont.nametofont("TkTextFont").copy()
        self.font.configure(size=11)
        self._build_ui()
        self._update_headers()
        if all(self.paths):
            root.after(100, self.compare)

    # ----- layout ---------------------------------------------------------
    def _build_ui(self):
        root = self.root
        root.title("Translation Diff")
        root.geometry("1400x850")
        root.minsize(700, 400)

        bar = ttk.Frame(root, padding=(8, 8, 8, 2))
        bar.pack(fill="x")
        ttk.Button(bar, text="Open A…", command=lambda: self.choose(0)).pack(side="left")
        ttk.Button(bar, text="Open B…", command=lambda: self.choose(1)).pack(side="left", padx=(4, 0))
        ttk.Button(bar, text="Swap", command=self.swap).pack(side="left", padx=(4, 0))
        ttk.Button(bar, text="Reload", command=self.compare).pack(side="left", padx=(4, 0))
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=10)
        for text, var in (("Ignore case", self.icase),
                          ("Ignore quote/dash style", self.norm),
                          ("Ignore punctuation", self.nopunct)):
            ttk.Checkbutton(bar, text=text, variable=var, command=self.compare).pack(side="left", padx=(0, 8))
        ttk.Label(bar, text="Min. match (words):").pack(side="left", padx=(6, 2))
        spin = ttk.Spinbox(bar, from_=1, to=10, width=3, textvariable=self.min_match, command=self.compare)
        spin.pack(side="left")
        spin.bind("<Return>", lambda e: self.compare())
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=10)
        ttk.Button(bar, text="◀ Prev", command=lambda: self.jump(-1)).pack(side="left")
        ttk.Button(bar, text="Next ▶", command=lambda: self.jump(1)).pack(side="left", padx=(4, 0))
        self.pos_label = ttk.Label(bar, text="")
        self.pos_label.pack(side="left", padx=8)

        main = ttk.Frame(root, padding=(8, 4, 8, 4))
        main.pack(fill="both", expand=True)
        main.columnconfigure(0, weight=1, uniform="col")
        main.columnconfigure(1, weight=1, uniform="col")
        main.rowconfigure(1, weight=1)

        hfont = self.font.copy()
        hfont.configure(size=10, weight="bold")
        self.heads = []
        self.texts = []
        for c in (0, 1):
            h = ttk.Label(main, anchor="w", font=hfont)
            h.grid(row=0, column=c, sticky="ew", pady=(0, 3), padx=(0, 6) if c == 0 else 0)
            self.heads.append(h)
            t = tk.Text(main, wrap="word", font=self.font, padx=10, pady=6,
                        borderwidth=1, relief="solid", highlightthickness=0,
                        background="white", foreground="#1b1b1b", undo=False,
                        state="disabled", cursor="arrow", takefocus=0)
            t.grid(row=1, column=c, sticky="nsew", padx=(0, 6) if c == 0 else 0)
            t.configure(yscrollcommand=lambda f, l, c=c: self._on_yscroll(c, f, l))
            for tag in ("rowA", "rowB", "flash", "del", "ins", "gap"):
                t.tag_configure(tag, background=COLORS[tag])
            t.bind("<Control-MouseWheel>", self._ctrl_wheel)
            self.texts.append(t)
        self.texts[0].bind("<Configure>", self._on_resize)

        self.minimap = tk.Canvas(main, width=16, background=COLORS["minimap_bg"],
                                 highlightthickness=0, cursor="hand2")
        self.minimap.grid(row=1, column=2, sticky="ns", padx=(6, 0))
        self.minimap.bind("<Configure>", lambda e: self._draw_minimap())
        self.minimap.bind("<Button-1>", self._minimap_click)
        self.minimap.bind("<B1-Motion>", self._minimap_click)
        self.minimap.bind("<MouseWheel>", lambda e: self.texts[0].yview_scroll(int(-e.delta / 40), "units"))

        self.vbar = ttk.Scrollbar(main, orient="vertical", command=self._scroll_both)
        self.vbar.grid(row=1, column=3, sticky="ns")

        status = ttk.Frame(root, padding=(8, 2, 8, 6))
        status.pack(fill="x")
        self.stats = ttk.Label(status, text="")
        self.stats.pack(side="left")
        ttk.Label(status, text="F3 / Shift+F3: next/prev   Ctrl +/−: text size").pack(side="right")
        for label, color in (("missing on this side", "gap"), ("only in B / differs", "ins"),
                             ("only in A / differs", "del")):
            tk.Label(status, text=" " + label + " ", background=COLORS[color]).pack(side="right", padx=(0, 6))

        root.bind("<F3>", lambda e: self.jump(1))
        root.bind("<Shift-F3>", lambda e: self.jump(-1))
        for seq in ("<Control-equal>", "<Control-plus>", "<Control-KP_Add>"):
            root.bind(seq, lambda e: self.zoom(1))
        for seq in ("<Control-minus>", "<Control-KP_Subtract>"):
            root.bind(seq, lambda e: self.zoom(-1))

    def _update_headers(self):
        names = []
        for side, p in enumerate(self.paths):
            label = "AB"[side]
            if p:
                self.heads[side].configure(text=f"{label}:  {p}")
                names.append(os.path.basename(p))
            else:
                self.heads[side].configure(text=f"{label}:  (click Open {label}…)")
        title = "Translation Diff"
        if len(names) == 2:
            title += f" — {names[0]}  ↔  {names[1]}"
        self.root.title(title)

    # ----- files ----------------------------------------------------------
    def choose(self, side):
        initial = os.path.dirname(self.paths[side] or self.paths[1 - side] or "") or None
        p = filedialog.askopenfilename(
            title=f"Choose file {'AB'[side]}", initialdir=initial,
            filetypes=[("Text files", "*.txt *.md *.srt *.vtt"), ("All files", "*.*")])
        if p:
            self.paths[side] = p
            self._update_headers()
            self.compare()

    def swap(self):
        self.paths.reverse()
        self._update_headers()
        self.compare()

    # ----- compare & render -------------------------------------------------
    def compare(self):
        if not all(self.paths):
            return
        try:
            contents = [read_text(p) for p in self.paths]
        except OSError as e:
            messagebox.showerror("Translation Diff", f"Could not read file:\n{e}")
            return
        try:
            min_match = max(1, int(self.min_match.get()))
        except ValueError:
            min_match = 2
        self.root.configure(cursor="watch")
        self.root.update_idletasks()
        try:
            A, B = Doc(contents[0]), Doc(contents[1])
            opts = (self.icase.get(), self.norm.get(), self.nopunct.get())
            A.index(*opts)
            B.index(*opts)
            ops = word_diff(A.keys, B.keys, min_match)
            self.rows = build_rows(A, B, ops)
            self.changed_rows = [i for i, r in enumerate(self.rows) if r[2]]
            self._cur = -1
            self._render()
            self.relayout()
            for t in self.texts:
                t.yview_moveto(0)

            na, nb = len(A.keys), len(B.keys)
            same = sum(i2 - i1 for tag, i1, i2, _, _ in ops if tag == "equal")
            nchg = sum(1 for op in ops if op[0] != "equal")
            sim = 100.0 * 2 * same / (na + nb) if na + nb else 100.0
            self.stats.configure(
                text=f"Similarity {sim:.1f}%    {nchg} differences in {len(self.changed_rows)} of "
                     f"{len(self.rows)} sentences    A: {na:,} words ({na - same:,} differ)    "
                     f"B: {nb:,} words ({nb - same:,} differ)")
            self.pos_label.configure(text=f"{nchg} differences" if nchg else "No differences")
        finally:
            self.root.configure(cursor="")

    def _render(self):
        n = len(self.rows)
        for side, t in enumerate(self.texts):
            t.configure(state="normal")
            t.delete("1.0", "end")
            for m in t.mark_names():
                if m.startswith("row"):
                    t.mark_unset(m)
            for i, row in enumerate(self.rows):
                rt = "rowA" if i % 2 == 0 else "rowB"
                t.mark_set(f"row{i}", "end-1c")
                t.mark_gravity(f"row{i}", "left")
                args = []
                for s, tag in row[side]:
                    args += [s, (tag, rt) if tag else (rt,)]
                args += ["\n", (rt,)]
                t.insert("end-1c", *args)
            t.mark_set(f"row{n}", "end-1c")
            t.mark_gravity(f"row{n}", "left")
            t.configure(state="disabled")

    @staticmethod
    def _count_lines(t, i):
        r = t.tk.call(t._w, "count", "-update", "-displaylines", f"row{i}", f"row{i + 1}")
        try:
            return int(r)
        except (TypeError, ValueError):
            return 0

    def relayout(self):
        """Pad the shorter side of every row with blank lines so the two
        columns stay aligned at the current width and font size."""
        self._relayout_job = None
        L, R = self.texts
        top = L.yview()[0]
        for t in self.texts:
            t.configure(state="normal")
            rng = t.tag_ranges("pad")
            for k in range(len(rng) - 2, -1, -2):
                t.delete(rng[k], rng[k + 1])
        self.root.update_idletasks()
        self._last_width = L.winfo_width()
        n = len(self.rows)
        heights = []
        for i in range(n):
            heights.append((self._count_lines(L, i), self._count_lines(R, i)))
        for i in range(n - 1, -1, -1):
            d = heights[i][0] - heights[i][1]
            if d:
                rt = "rowA" if i % 2 == 0 else "rowB"
                (R if d > 0 else L).insert(f"row{i + 1}-1c", "\n" * abs(d), ("pad", rt))
        for t in self.texts:
            t.configure(state="disabled")
        self.row_heights = [max(h) for h in heights]
        self.row_tops, acc = [], 0
        for h in self.row_heights:
            self.row_tops.append(acc)
            acc += h
        for t in self.texts:
            t.yview_moveto(top)
        self._draw_minimap()

    def _on_resize(self, event):
        if self.rows and event.width != self._last_width:
            if self._relayout_job:
                self.root.after_cancel(self._relayout_job)
            self._relayout_job = self.root.after(250, self.relayout)

    def zoom(self, delta):
        size = self.font.cget("size")
        self.font.configure(size=max(6, min(36, size + delta)))
        if self.rows:
            if self._relayout_job:
                self.root.after_cancel(self._relayout_job)
            self._relayout_job = self.root.after(150, self.relayout)

    def _ctrl_wheel(self, event):
        self.zoom(1 if event.delta > 0 else -1)
        return "break"

    # ----- scrolling --------------------------------------------------------
    def _scroll_both(self, *args):
        for t in self.texts:
            t.yview(*args)

    def _on_yscroll(self, side, first, last):
        first, last = float(first), float(last)
        self.vbar.set(first, last)
        other = self.texts[1 - side]
        if abs(other.yview()[0] - first) > 1e-4:
            other.yview_moveto(first)
        self._draw_viewport(first, last)

    def _draw_minimap(self):
        c = self.minimap
        c.delete("all")
        total = sum(self.row_heights)
        h = c.winfo_height()
        if not total or h < 2:
            return
        w = int(c.winfo_width())
        # Shade each sentence by how much of it differs (light = a word or
        # two, dark red = mostly different), so heavy divergence stands out.
        bg = [int(COLORS["minimap_bg"][k:k + 2], 16) for k in (1, 3, 5)]
        fg = [int(COLORS["minimap_mark"][k:k + 2], 16) for k in (1, 3, 5)]
        for i in self.changed_rows:
            s = 0.25 + 0.75 * min(1.0, self.rows[i][3] * 1.5)
            color = "#%02x%02x%02x" % tuple(int(b + (f - b) * s) for b, f in zip(bg, fg))
            y0 = self.row_tops[i] / total * h
            y1 = max(y0 + 2, (self.row_tops[i] + self.row_heights[i]) / total * h)
            c.create_rectangle(2, y0, w - 2, y1, fill=color, outline="")
        self._draw_viewport(*self.texts[0].yview())

    def _draw_viewport(self, first, last):
        c = self.minimap
        c.delete("vp")
        h = c.winfo_height()
        if h > 2 and self.rows:
            c.create_rectangle(0, first * h, c.winfo_width() - 1, max(first * h + 3, last * h),
                               outline="#4a5560", width=1, tags="vp")

    def _minimap_click(self, event):
        h = self.minimap.winfo_height()
        first, last = self.texts[0].yview()
        frac = event.y / max(h, 1) - (last - first) / 2
        for t in self.texts:
            t.yview_moveto(max(0.0, frac))

    # ----- navigation -------------------------------------------------------
    def jump(self, direction):
        if not self.changed_rows:
            self.root.bell()
            return
        L = self.texts[0]
        top_line = int(L.index("@0,0").split(".")[0])
        starts = [int(L.index(f"row{i}").split(".")[0]) for i in self.changed_rows]
        if direction > 0:
            # Go to the change at the top of the view, unless we just jumped there.
            k = bisect.bisect_left(starts, top_line)
            if k < len(starts) and starts[k] == top_line and self._cur == k:
                k += 1
            if k >= len(starts):
                self.root.bell()
                return
        else:
            k = bisect.bisect_left(starts, top_line) - 1
            if k < 0:
                self.root.bell()
                return
        self._cur = k
        row = self.changed_rows[k]
        for t in self.texts:
            t.yview(f"row{row}")
        self.pos_label.configure(text=f"Sentence {k + 1} of {len(self.changed_rows)} with differences")
        self._flash(row)

    def _flash(self, row):
        for t in self.texts:
            t.tag_remove("flash", "1.0", "end")
            t.tag_add("flash", f"row{row}", f"row{row + 1}")
        if self._flash_job:
            self.root.after_cancel(self._flash_job)
        self._flash_job = self.root.after(
            900, lambda: [t.tag_remove("flash", "1.0", "end") for t in self.texts])


def main():
    if sys.platform == "win32":
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass
    args = sys.argv[1:]
    root = tk.Tk()
    App(root, args[0] if len(args) > 0 else None, args[1] if len(args) > 1 else None)
    root.mainloop()


if __name__ == "__main__":
    main()
