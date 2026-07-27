#!/usr/bin/env python3
"""Render the project's Markdown docs into a static HTML site in site/.

The Markdown files stay the single source of truth — nothing here is written
back into them, and no HTML is committed. Run it locally to preview:

    pip install markdown
    python3 tools/build_docs.py && open site/index.html

Links between docs are rewritten to the generated pages. Links to files the
site doesn't publish (v3 hardware notes, CLAUDE.md, LICENSE) are rewritten to
point at GitHub, so they resolve instead of 404ing. Referenced images and PDFs
are copied into site/assets/.
"""

import html
import os
import re
import shutil
import sys
from dataclasses import dataclass

import markdown

REPO = "https://github.com/qxzzxq/TinyJuke"
BLOB = f"{REPO}/blob/main"
TREE = f"{REPO}/tree/main"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "site")

ASSET_EXT = {".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".avif", ".pdf",
             ".mp4", ".webm", ".mov", ".m4v"}


@dataclass(frozen=True)
class Page:
    src: str      # path relative to the repo root
    out: str      # filename written into site/
    nav: str      # label in the navigation
    blurb: str    # shown under the title


PAGES = [
    Page("README.md", "index.html", "Home",
         "What TinyJuke is, how to build one, and how to use it."),
    Page("docs/technical-reference.md", "technical-reference.html", "Technical Reference",
         "Hardware, wiring, SD card format, build profiles, and the HTTP API."),
    Page("docs/esp32_wrover_e_pin_map.md", "pin-map.html", "Pin Map",
         "GPIO allocation for the custom WROVER-E mainboard."),
]

BY_SRC = {os.path.normpath(p.src): p for p in PAGES}


def slugify(text, separator="-"):
    """Match GitHub's heading anchors so existing #links keep working."""
    text = html.unescape(text).strip().lower()
    text = "".join(c for c in text if c.isalnum() or c in " -_")
    return text.replace(" ", separator)


def rewrite_link(target, src_dir, assets):
    """Map one Markdown link target onto something the built site can serve."""
    if re.match(r"^(https?:|mailto:|data:|//|#)", target):
        return target

    path, sep, frag = target.partition("#")
    if not path:
        return target

    rel = os.path.normpath(os.path.join(src_dir, path))
    page = BY_SRC.get(rel)
    if page:
        return page.out + sep + frag

    ext = os.path.splitext(rel)[1].lower()
    if ext in ASSET_EXT and os.path.exists(os.path.join(ROOT, rel)):
        assets.add(rel)
        return "assets/" + os.path.basename(rel) + sep + frag

    # Not published by the site — send the reader to the repo instead.
    base = TREE if target.endswith("/") else BLOB
    return f"{base}/{rel}" + sep + frag


def render_nav(current, toc_tokens):
    """Page links (always shown) plus this page's headings (desktop only).

    Deliberately not a <details>: Chrome hides a closed <details>' contents via
    ::details-content, which a `display: block` override on the child no longer
    beats, so the sidebar rendered empty on desktop.
    """
    items = []
    for p in PAGES:
        active = ' class="active"' if p.out == current else ""
        items.append(f'<li><a href="{p.out}"{active}>{html.escape(p.nav)}</a></li>')
    nav = '<nav class="pagenav"><ul>\n' + "\n".join(items) + "\n</ul></nav>"

    if not toc_tokens:
        return nav

    def branch(tokens):
        out = ["<ul>"]
        for t in tokens:
            out.append(f'<li><a href="#{t["id"]}">{html.escape(t["name"])}</a>')
            if t["children"]:
                out.append(branch(t["children"]))
            out.append("</li>")
        out.append("</ul>")
        return "\n".join(out)

    return (nav + '\n<nav class="toc"><p class="toc-label">On this page</p>\n'
            + branch(toc_tokens) + "</nav>")


TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{blurb}">
<link rel="stylesheet" href="style.css">
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><text y='14' font-size='14'>&#127925;</text></svg>">
</head>
<body>
<a class="skip" href="#content">Skip to content</a>
<header class="topbar">
  <a class="brand" href="index.html">TinyJuke</a>
  <a class="repo" href="{repo}">GitHub</a>
</header>
<div class="shell">
  <aside class="sidebar">{nav}</aside>
  <main id="content">
    {body}
    <footer>
      <p>Source: <a href="{blob}/{src}">{src}</a> &middot; Firmware GPL-3.0-or-later,
      hardware CERN-OHL-S-2.0, docs CC-BY-SA-4.0.</p>
    </footer>
  </main>
</div>
</body>
</html>
"""

CSS = """/* Palette echoes the device's default "Bamboo Moss" UI theme (theme.cpp). */
:root {
  --bg: #ffffff; --surface: #f5f7f3; --text: #1b2118; --muted: #5c6b52;
  --accent: #3f6f24; --line: #dfe5d9; --code-bg: #f2f4ef;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #0c120c; --surface: #141e14; --text: #e8f0e2; --muted: #87977d;
    --accent: #8fc46b; --line: #202c1f; --code-bg: #141e14;
  }
}
* { box-sizing: border-box; }
html { scroll-padding-top: 4.5rem; }
body {
  margin: 0; background: var(--bg); color: var(--text);
  font: 16px/1.65 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  -webkit-text-size-adjust: 100%;
}
a { color: var(--accent); text-decoration-thickness: 1px; text-underline-offset: 2px; }
.skip { position: absolute; left: -999px; }
.skip:focus { left: 1rem; top: 1rem; background: var(--surface); padding: .5rem 1rem; z-index: 10; }

.topbar {
  position: sticky; top: 0; z-index: 5; display: flex; align-items: center;
  justify-content: space-between; gap: 1rem; padding: .85rem 1.25rem;
  background: var(--surface); border-bottom: 1px solid var(--line);
}
.brand { font-weight: 650; letter-spacing: .01em; text-decoration: none; color: var(--text); }
.repo { font-size: .9rem; }

.shell { display: block; max-width: 62rem; margin: 0 auto; padding: 0 1.25rem 4rem; }
.sidebar { padding-top: 1.5rem; }
.sidebar ul { list-style: none; margin: .35rem 0; padding-left: .9rem; }
.sidebar li { margin: .3rem 0; }
.sidebar a { text-decoration: none; color: var(--muted); font-size: .93rem; }
.sidebar a:hover { color: var(--accent); text-decoration: underline; }
.sidebar a.active { color: var(--accent); font-weight: 600; }
.pagenav ul {
  display: flex; flex-wrap: wrap; gap: .35rem 1.1rem;
  padding: 0 0 .9rem; margin: 0; border-bottom: 1px solid var(--line);
}
.pagenav a { color: var(--text); font-size: 1rem; }
.toc { display: none; }
.toc-label {
  margin: 1.2rem 0 .2rem; font-size: .75rem; letter-spacing: .09em;
  text-transform: uppercase; color: var(--muted);
}

main { min-width: 0; padding-top: 1.25rem; }
h1, h2, h3, h4 { line-height: 1.25; }
h1 { font-size: 2rem; margin: 1.2rem 0 .6rem; }
h2 { font-size: 1.4rem; margin: 2.4rem 0 .6rem; padding-top: .6rem; border-top: 1px solid var(--line); }
h3 { font-size: 1.12rem; margin: 1.8rem 0 .5rem; }
h4 { font-size: 1rem; margin: 1.3rem 0 .4rem; color: var(--muted); }

blockquote {
  margin: 1.2rem 0; padding: .8rem 1.1rem; background: var(--surface);
  border-left: 3px solid var(--accent); border-radius: 0 5px 5px 0;
}
blockquote > :first-child { margin-top: 0; }
blockquote > :last-child { margin-bottom: 0; }

code {
  font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
  font-size: .88em; background: var(--code-bg); padding: .12em .35em;
  border-radius: 4px; border: 1px solid var(--line);
}
pre {
  background: var(--code-bg); border: 1px solid var(--line); border-radius: 7px;
  padding: .9rem 1rem; overflow-x: auto;
}
pre code { background: none; border: 0; padding: 0; font-size: .85rem; }

.table-scroll { overflow-x: auto; margin: 1.1rem 0; }
table { border-collapse: collapse; width: 100%; font-size: .92rem; }
th, td { border: 1px solid var(--line); padding: .45rem .7rem; text-align: left; vertical-align: top; }
th { background: var(--surface); font-weight: 620; }

hr { border: 0; border-top: 1px solid var(--line); margin: 2.2rem 0; }
img { max-width: 100%; height: auto; border-radius: 8px; display: block; margin: 1.2rem 0; }
video { max-width: 100%; height: auto; border-radius: 8px; display: block; margin: 1.2rem 0; }
footer {
  margin-top: 3.5rem; padding-top: 1.1rem; border-top: 1px solid var(--line);
  color: var(--muted); font-size: .85rem;
}

@media (min-width: 60rem) {
  .shell {
    display: grid; grid-template-columns: 15rem minmax(0, 1fr);
    gap: 3rem; max-width: 70rem;
  }
  .sidebar {
    position: sticky; top: 3.6rem; align-self: start;
    max-height: calc(100vh - 4.6rem); overflow-y: auto; padding-bottom: 1rem;
  }
  .pagenav ul { display: block; padding-left: 0; }
  .toc { display: block; }
  main { padding-top: 0; }
}
"""


def build():
    md = markdown.Markdown(
        extensions=["extra", "toc"],
        extension_configs={"toc": {"slugify": slugify, "toc_depth": "2-3"}},
    )

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)

    assets = set()
    for page in PAGES:
        src_path = os.path.join(ROOT, page.src)
        if not os.path.exists(src_path):
            sys.exit(f"missing source document: {page.src}")

        with open(src_path, encoding="utf-8") as f:
            body = md.reset().convert(f.read())

        src_dir = os.path.dirname(page.src)
        # src= matters as much as href=: images embedded from docs/img/ are
        # relative to the Markdown file, not to the site root.
        body = re.sub(
            r'\b(href|src)="([^"]*)"',
            lambda m: '%s="%s"' % (m.group(1), html.escape(
                rewrite_link(html.unescape(m.group(2)), src_dir, assets), quote=True)),
            body,
        )
        body = body.replace("<table>", '<div class="table-scroll"><table>')
        body = body.replace("</table>", "</table></div>")

        title = page.nav if page.out != "index.html" else "TinyJuke"
        with open(os.path.join(OUT, page.out), "w", encoding="utf-8") as f:
            f.write(TEMPLATE.format(
                title=html.escape(title if title == "TinyJuke" else f"{title} — TinyJuke"),
                blurb=html.escape(page.blurb),
                nav=render_nav(page.out, md.toc_tokens),
                body=body,
                src=html.escape(page.src),
                repo=REPO,
                blob=BLOB,
            ))
        print(f"  {page.src} -> site/{page.out}")

    with open(os.path.join(OUT, "style.css"), "w", encoding="utf-8") as f:
        f.write(CSS)

    if assets:
        os.makedirs(os.path.join(OUT, "assets"), exist_ok=True)
        for rel in sorted(assets):
            shutil.copy2(os.path.join(ROOT, rel),
                         os.path.join(OUT, "assets", os.path.basename(rel)))
            print(f"  asset {rel}")

    # Tell GitHub Pages not to run Jekyll over the output.
    open(os.path.join(OUT, ".nojekyll"), "w").close()
    print(f"built {len(PAGES)} pages into {OUT}")


if __name__ == "__main__":
    build()
