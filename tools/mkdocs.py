"""mkdocs.py - dokumentacja docs/DOKUMENTACJA.md -> docs/SpeedwayParty-dokumentacja.pdf
oraz angielska docs/DOCUMENTATION.md -> docs/SpeedwayParty-documentation.pdf

    python tools/mkdocs.py            # HTML + PDF, obie wersje jezykowe
    python tools/mkdocs.py --html     # tylko HTML (build/docs/), np. do podgladu w przegladarce
    python tools/mkdocs.py --pl       # tylko polska (--en: tylko angielska)

Zrodlem jest zwykly Markdown (edytuj go, nie PDF). Skrypt zamienia go na HTML przygotowany do druku
(strona tytulowa, spis tresci, numery stron, zrzuty ekranu), a PDF drukuje przegladarka Edge albo
Chrome w trybie bez okna. Wersja (commit z gita) i data wstawiaja sie same.

Obslugiwany Markdown: naglowki # / ## / ###, akapity, listy - i 1., tabele, bloki kodu ```,
cytaty >, obrazki ![opis](sciezka), **pogrubienie**, *kursywa*, `kod`, [link](adres).
# = czesc (nowa strona), ## = rozdzial (w spisie tresci). Bez zaleznosci poza biblioteka standardowa."""
import datetime
import html
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# jezyk: zrodlo, PDF, HTML, napisy na stronie tytulowej i w spisie tresci
LANGS = {
    "pl": dict(src=ROOT / "docs/DOKUMENTACJA.md", pdf=ROOT / "docs/SpeedwayParty-dokumentacja.pdf",
               html=ROOT / "build/docs/DOKUMENTACJA.html", toc="Spis treści",
               sub="Dokumentacja gry na Amigę 1200", ver="wersja", gen="wygenerowano"),
    "en": dict(src=ROOT / "docs/DOCUMENTATION.md", pdf=ROOT / "docs/SpeedwayParty-documentation.pdf",
               html=ROOT / "build/docs/DOCUMENTATION.html", toc="Contents",
               sub="Game manual for the Amiga 1200", ver="version", gen="generated"),
}

CSS = """
@page { size: A4; margin: 18mm 16mm 18mm 16mm;
        @bottom-center { content: counter(page); font: 9pt 'Segoe UI', sans-serif; color: #777; } }
@page :first { @bottom-center { content: none; } }
* { box-sizing: border-box; }
body { font: 10.5pt/1.5 'Segoe UI', Arial, sans-serif; color: #1d2230; margin: 0; }
.cover { height: 250mm; display: flex; flex-direction: column; justify-content: center; text-align: center;
         page-break-after: always; }
.cover img { width: 150mm; margin: 0 auto 10mm; image-rendering: pixelated; border-radius: 4px; }
.cover h1 { font-size: 34pt; margin: 0; color: #12205a; letter-spacing: 1px; }
.cover .sub { font-size: 14pt; color: #465080; margin: 4mm 0 14mm; }
.cover .meta { font-size: 10pt; color: #666; }
.toc { page-break-after: always; }
.toc h2 { border: none; }
.toc ol { list-style: none; padding: 0; }
.toc li { margin: 1.5mm 0; }
.toc li.part { font-weight: 700; margin-top: 5mm; color: #12205a; }
.toc li.chap { padding-left: 6mm; }
.toc a { color: inherit; text-decoration: none; }
h1 { font-size: 22pt; color: #12205a; page-break-before: always; margin: 0 0 6mm;
     border-bottom: 3px solid #e2b33c; padding-bottom: 2mm; }
h2 { font-size: 15pt; color: #12205a; margin: 8mm 0 3mm; border-bottom: 1px solid #cfd5ea; padding-bottom: 1mm;
     page-break-after: avoid; }
h3 { font-size: 12pt; color: #2b3a7a; margin: 5mm 0 2mm; page-break-after: avoid; }
p { margin: 0 0 2.5mm; }
ul, ol { margin: 0 0 3mm; padding-left: 6mm; }
li { margin: 0.8mm 0; }
code { font: 9pt Consolas, monospace; background: #eef1f8; padding: 0.3mm 1mm; border-radius: 2px; }
pre { background: #10162b; color: #e6e9f2; padding: 3mm 4mm; border-radius: 3px; font: 8.5pt/1.4 Consolas, monospace;
      white-space: pre-wrap; page-break-inside: avoid; }
pre code { background: none; color: inherit; padding: 0; }
blockquote { margin: 3mm 0; padding: 2mm 4mm; border-left: 3px solid #e2b33c; background: #fbf6e6; color: #4a4330; }
table { border-collapse: collapse; width: 100%; margin: 2mm 0 4mm; font-size: 9.5pt; page-break-inside: avoid; }
th, td { border: 1px solid #cfd5ea; padding: 1.2mm 2mm; text-align: left; vertical-align: top; }
th { background: #e7ebf7; color: #12205a; }
figure { margin: 3mm 0 5mm; text-align: center; page-break-inside: avoid; }
figure img { width: 120mm; image-rendering: pixelated; border: 1px solid #cfd5ea; }
figcaption { font-size: 9pt; color: #666; margin-top: 1mm; }
a { color: #2b4bb0; }
"""


def git_version():
    try:
        h = subprocess.run(["git", "-C", str(ROOT), "log", "-1", "--format=%h %cd", "--date=short"],
                           capture_output=True, text=True, check=True).stdout.strip()
        return h
    except (OSError, subprocess.CalledProcessError):
        return "?"


def slug(text, used):
    s = re.sub(r"[^\w]+", "-", text.lower()).strip("-") or "sekcja"
    base, n = s, 2
    while s in used:
        s = f"{base}-{n}"; n += 1
    used.add(s)
    return s


def inline(text, base):
    """**b**, *i*, `kod`, [link](url) - z ucieczka HTML"""
    parts = re.split(r"(`[^`]+`)", text)
    out = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 1:
            out.append("<code>" + html.escape(p[1:-1]) + "</code>")
            continue
        p = html.escape(p, quote=False)
        p = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", p)
        p = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", p)
        p = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", lambda m: f'<a href="{m.group(2)}">{m.group(1)}</a>', p)
        out.append(p)
    return "".join(out)


def img_src(path, base):
    p = (base / path).resolve()
    return p.as_uri() if p.exists() else path


def convert(md, base):
    """Markdown -> (HTML, lista (poziom, tytul, id)) dla spisu tresci"""
    lines = md.splitlines()
    out, toc, used = [], [], set()
    i = 0
    para = []

    def flush():
        if para:
            out.append("<p>" + inline(" ".join(para), base) + "</p>")
            para.clear()

    while i < len(lines):
        ln = lines[i]
        s = ln.strip()
        if s.startswith("```"):
            flush()
            code = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(lines[i]); i += 1
            out.append("<pre><code>" + html.escape("\n".join(code)) + "</code></pre>")
            i += 1
            continue
        m = re.match(r"^(#{1,3})\s+(.*)$", s)
        if m:
            flush()
            lvl, title = len(m.group(1)), m.group(2).strip()
            sid = slug(title, used)
            if lvl <= 2:
                toc.append((lvl, title, sid))
            out.append(f'<h{lvl} id="{sid}">{inline(title, base)}</h{lvl}>')
            i += 1
            continue
        m = re.match(r"^!\[([^\]]*)\]\(([^)]+)\)$", s)
        if m:
            flush()
            out.append(f'<figure><img src="{img_src(m.group(2), base)}" alt="{html.escape(m.group(1))}">'
                       f'<figcaption>{html.escape(m.group(1))}</figcaption></figure>')
            i += 1
            continue
        if s.startswith("|"):
            flush()
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
                rows.append(cells); i += 1
            head, body = rows[0], [r for r in rows[1:] if not all(re.fullmatch(r":?-+:?", c) for c in r)]
            t = ["<table><thead><tr>" + "".join(f"<th>{inline(c, base)}</th>" for c in head) + "</tr></thead><tbody>"]
            for r in body:
                t.append("<tr>" + "".join(f"<td>{inline(c, base)}</td>" for c in r) + "</tr>")
            out.append("".join(t) + "</tbody></table>")
            continue
        if s.startswith(">"):
            flush()
            q = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                q.append(lines[i].strip()[1:].strip()); i += 1
            out.append("<blockquote>" + inline(" ".join(q), base) + "</blockquote>")
            continue
        m = re.match(r"^(-|\d+\.)\s+(.*)$", s)
        if m:
            flush()
            tag = "ul" if m.group(1) == "-" else "ol"
            items = []
            while i < len(lines):
                mm = re.match(r"^(-|\d+\.)\s+(.*)$", lines[i].strip())
                if mm and (mm.group(1) == "-") == (tag == "ul"):
                    items.append(mm.group(2)); i += 1
                elif lines[i].startswith("   ") and lines[i].strip() and items:
                    items[-1] += " " + lines[i].strip(); i += 1      # kontynuacja punktu
                else:
                    break
            out.append(f"<{tag}>" + "".join(f"<li>{inline(t, base)}</li>" for t in items) + f"</{tag}>")
            continue
        if not s:
            flush()
        else:
            para.append(s)
        i += 1
    flush()
    return "\n".join(out), toc


def build_html(lang):
    L = LANGS[lang]
    SRC, OUT_HTML = L["src"], L["html"]
    md = SRC.read_text(encoding="utf-8")
    title = md.splitlines()[0].lstrip("# ").strip()
    body_md = "\n".join(md.splitlines()[1:])
    body, toc = convert(body_md, SRC.parent)
    version = git_version()
    today = datetime.date.today().isoformat()
    cover_img = img_src("../art/title_art.png", SRC.parent)
    toc_html = [f'<section class="toc"><h2>{L["toc"]}</h2><ol>']
    for lvl, t, sid in toc:
        toc_html.append(f'<li class="{"part" if lvl == 1 else "chap"}"><a href="#{sid}">{html.escape(t)}</a></li>')
    toc_html.append("</ol></section>")
    doc = f"""<!doctype html><html lang="{lang}"><head><meta charset="utf-8"><title>{html.escape(title)}</title>
<style>{CSS}</style></head><body>
<section class="cover"><img src="{cover_img}" alt=""><h1>Speedway Party</h1>
<div class="sub">{L["sub"]}</div>
<div class="meta">{L["ver"]} {html.escape(version)} · {L["gen"]} {today}</div></section>
{''.join(toc_html)}
{body}
</body></html>"""
    OUT_HTML.parent.mkdir(parents=True, exist_ok=True)
    OUT_HTML.write_text(doc, encoding="utf-8")
    return OUT_HTML


def find_browser():
    cands = [os.environ.get("BROWSER_PDF", ""),
             r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
             r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
             r"C:\Program Files\Google\Chrome\Application\chrome.exe",
             shutil.which("chromium") or "", shutil.which("google-chrome") or "", shutil.which("msedge") or ""]
    return next((c for c in cands if c and Path(c).exists()), None)


def main():
    langs = [l for l in LANGS if f"--{l}" in sys.argv] or list(LANGS)
    for lang in langs:
        page = build_html(lang)
        print(page)
        if "--html" in sys.argv:
            continue
        br = find_browser()
        if not br:
            sys.exit("mkdocs: brak Edge/Chrome - otworz HTML w przegladarce i wydrukuj do PDF")
        out_pdf = LANGS[lang]["pdf"]
        out_pdf.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([br, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                        "--allow-file-access-from-files", f"--print-to-pdf={out_pdf}", page.as_uri()],
                       check=True, capture_output=True, timeout=180)
        print(out_pdf, f"{out_pdf.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
