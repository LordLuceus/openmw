#!/usr/bin/env python3
"""Convert a Project Hortator accessibility document from Markdown to a single
self-contained, screen-reader-friendly HTML file.

Why a script rather than a one-off conversion: the Markdown is the source of
truth and will keep changing, so the HTML must be regenerable on demand and
identical every time.

The output is deliberately plain. Every choice here is a screen-reader choice:

  * Real semantic elements only -- <table>, <th scope="col">, <h2>, <nav>. A
    screen reader announces "column one of two, Key" from the markup itself, so
    the table must be a real table, not a styled grid of divs.
  * <html lang="en"> so the synthesiser picks the right voice and pronunciation.
  * A skip link and a <main> landmark, so a reader can jump past the contents
    list instead of hearing it on every visit.
  * No JavaScript, no web fonts, no external stylesheet: the file must work when
    opened straight from a release zip with no network.
  * Colours only ever ADD to a cue that already exists in the text, so nothing
    is conveyed by colour alone.

Usage:
    python scripts/a11y_md_to_html.py ACCESSIBILITY_KEYS.md ACCESSIBILITY_KEYS.html
    python scripts/a11y_md_to_html.py --all      (regenerates every known doc)
"""

import argparse
import re
import sys
from pathlib import Path

try:
    import markdown
except ImportError:
    sys.exit("The 'markdown' package is required: python -m pip install markdown")

# Docs that ship to players in HTML form, as (markdown, html) pairs.
SHIPPED_DOCS = [
    ("ACCESSIBILITY_KEYS.md", "ACCESSIBILITY_KEYS.html"),
]

# Plain, high-contrast, and sized for low-vision readers who are not using a
# screen reader. `prefers-color-scheme` honours the reader's own setting rather
# than imposing a theme, and every size is in rem so browser zoom and the user's
# default font size are respected.
STYLE = """
:root { color-scheme: light dark; }
* { box-sizing: border-box; }
body {
  font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
  font-size: 1.05rem;
  line-height: 1.6;
  max-width: 60rem;
  margin: 0 auto;
  padding: 1.5rem 1.25rem 4rem;
  color: #14171a;
  background: #fff;
}
h1 { font-size: 2rem; line-height: 1.25; margin: 0 0 0.5rem; }
h2 {
  font-size: 1.5rem;
  margin: 2.5rem 0 0.75rem;
  padding-bottom: 0.3rem;
  border-bottom: 2px solid #c8ced4;
}
h3 { font-size: 1.2rem; margin: 1.75rem 0 0.5rem; }
p, li { max-width: 46rem; }
a { color: #0b5fa5; }
a:focus-visible, :focus-visible { outline: 3px solid #0b5fa5; outline-offset: 2px; }
code {
  font-family: ui-monospace, Consolas, "Courier New", monospace;
  font-size: 0.95em;
  background: #eef1f4;
  padding: 0.1em 0.35em;
  border-radius: 3px;
}
table {
  border-collapse: collapse;
  width: 100%;
  margin: 1rem 0 1.5rem;
}
caption { text-align: left; font-style: italic; padding-bottom: 0.5rem; }
th, td {
  border: 1px solid #b9c0c7;
  padding: 0.55rem 0.7rem;
  text-align: left;
  vertical-align: top;
}
th { background: #eef1f4; font-weight: 600; }
tbody tr:nth-child(even) td { background: #f7f9fa; }
/* The first column is always the key itself; keep it from wrapping mid-chord. */
td:first-child { white-space: nowrap; font-weight: 600; }
hr { border: 0; border-top: 1px solid #c8ced4; margin: 2.5rem 0; }
blockquote {
  margin: 1rem 0;
  padding: 0.5rem 1rem;
  border-left: 4px solid #0b5fa5;
  background: #f2f6fa;
}
.skip-link {
  position: absolute;
  left: -9999px;
  top: 0;
  background: #0b5fa5;
  color: #fff;
  padding: 0.6rem 1rem;
  z-index: 10;
}
.skip-link:focus { left: 0; }
@media (prefers-color-scheme: dark) {
  body { color: #e8eaed; background: #16191c; }
  h2 { border-bottom-color: #3a4249; }
  a { color: #6db3f2; }
  code { background: #24292e; }
  th { background: #22272b; }
  tbody tr:nth-child(even) td { background: #1c2023; }
  th, td { border-color: #3a4249; }
  blockquote { background: #1c2329; }
}
@media print {
  body { max-width: none; color: #000; background: #fff; font-size: 11pt; }
  .skip-link, nav.toc { display: none; }
  h2 { page-break-after: avoid; }
  table, tr { page-break-inside: avoid; }
}
"""


def convert(md_path: Path, html_path: Path) -> None:
    text = md_path.read_text(encoding="utf-8")

    # Pull the H1 out for <title>; fall back to the filename so a doc without a
    # heading still produces a usable, named window title.
    m = re.search(r"^#\s+(.+)$", text, flags=re.MULTILINE)
    title = m.group(1).strip() if m else md_path.stem.replace("_", " ").title()

    html_body = markdown.markdown(
        text,
        extensions=["tables", "toc", "sane_lists", "attr_list"],
        output_format="html5",
    )

    # python-markdown emits bare <th>. Add scope="col" so a screen reader
    # reliably associates each cell with its column header when reading across a
    # row ("Key: Page Down, Action: next target") instead of guessing. Every
    # table in these docs is a simple column-headed table, so this is always the
    # correct scope.
    html_body = html_body.replace("<th>", '<th scope="col">')

    # Mark up the generated contents list as a real navigation landmark so a
    # screen reader can jump to (or skip) it. The Markdown "## Contents" heading
    # is followed by a plain <ul>; wrap that specific list.
    html_body = re.sub(
        r"(<h2[^>]*>Contents</h2>\s*)(<ul>)",
        r'\1<nav class="toc" aria-label="Contents">\2',
        html_body,
        count=1,
    )
    if 'class="toc"' in html_body:
        # Close the nav after the list that immediately follows the heading.
        idx = html_body.index('<nav class="toc"')
        end = html_body.index("</ul>", idx) + len("</ul>")
        html_body = html_body[:end] + "</nav>" + html_body[end:]

    # Escaping note: the Markdown source contains literal `&`, `<` in prose
    # rarely, and markdown() already escapes those. We only inject our own
    # trusted chrome below.
    doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>{STYLE}</style>
</head>
<body>
<a class="skip-link" href="#main">Skip to main content</a>
<main id="main">
{html_body}
</main>
</body>
</html>
"""
    html_path.write_text(doc, encoding="utf-8")
    print(f"{md_path.name} -> {html_path.name} ({len(doc):,} bytes)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", nargs="?", help="Markdown file to convert")
    ap.add_argument("dest", nargs="?", help="HTML file to write")
    ap.add_argument(
        "--all",
        action="store_true",
        help="regenerate every document that ships in HTML form",
    )
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent

    if args.all:
        for src, dst in SHIPPED_DOCS:
            convert(root / src, root / dst)
        return 0

    if not args.source or not args.dest:
        ap.error("give a source and destination, or use --all")

    convert(Path(args.source), Path(args.dest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
