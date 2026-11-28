#!/usr/bin/env python3
"""Draws the README's speed and quality charts from its own tables.

tools/readme-charts.py [README.md] [output directory]

Writes speed-{light,dark}.svg and quality-{light,dark}.svg, by default in
doc/images. Run it after changing either table.
"""

import os
import sys
from html import escape

FONT = 'system-ui, -apple-system, "Segoe UI", sans-serif'

THEMES = {
    "light": {
        "surface": "#ffffff", "ink": "#191919", "ink2": "#666666",
        "grid": "#e7e7e7", "axis": "#c2c2c2", "other": "#a8a8a8",
        "fast": "#84b9f3",
        "series": ["#257de0", "#f47230", "#00b894", "#f2a600"],
    },
    "dark": {
        "surface": "#191919", "ink": "#efefef", "ink2": "#9c9c9c",
        "grid": "#303030", "axis": "#474747", "other": "#6b6b6b",
        "fast": "#1757ac",
        "series": ["#338ced", "#e76624", "#00a888", "#d28e05"],
    },
}

SPEED_COLUMN = "44.1 kHz stereo 128 kbps"
QUALITY_SERIES = ["`shineenc -Q 1`", "LAME 4.0, default",
                  "FDK AAC 2.0, AAC-LC", "`shineenc -Q 0`"]
QUALITY_BITRATES = [64, 96, 128, 192]


def table_after(lines, heading):
    """The first Markdown table after `heading`, as a header and rows."""
    start = lines.index(heading)
    rows = []
    for line in lines[start:]:
        if line.startswith("|"):
            rows.append([cell.strip() for cell in line.strip("|\n").split("|")])
        elif rows:
            break
    return rows[0], [row for row in rows[2:]]


def is_fast_mode(label):
    return "-Q 0" in label or "quality 0" in label


def plain(label):
    return label.replace("`", "")


def svg(width, height, theme, body, title):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
            f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
            f'aria-label="{escape(title)}" font-family=\'{FONT}\'>\n'
            f'<title>{escape(title)}</title>\n'
            f'<rect width="{width}" height="{height}" rx="8" '
            f'fill="{theme["surface"]}"/>\n{body}</svg>\n')


def text(x, y, content, fill, size=13, anchor="start", weight="normal"):
    return (f'<text x="{x:.1f}" y="{y:.1f}" fill="{fill}" font-size="{size}" '
            f'text-anchor="{anchor}" font-weight="{weight}">'
            f'{escape(content)}</text>\n')


def bar(x, y, length, thickness, fill):
    """A horizontal bar: square at the baseline, 4px rounded at its end."""
    r = min(4, length / 2, thickness / 2)
    return (f'<path d="M{x:.1f},{y:.1f} h{length - r:.1f} '
            f'a{r},{r} 0 0 1 {r},{r} v{thickness - 2 * r:.1f} '
            f'a{r},{r} 0 0 1 -{r},{r} h-{length - r:.1f} z" fill="{fill}"/>\n')


def speed_chart(header, rows, theme):
    column = header.index(SPEED_COLUMN)
    data = sorted(((plain(row[0]), float(row[column].rstrip("x")))
                   for row in rows), key=lambda item: -item[1])
    width, left, right, top, row_height, thickness = 720, 250, 70, 92, 34, 22
    height = top + row_height * len(data) + 40
    scale = (width - left - right) / max(value for _, value in data)
    title = "Encoding speed at 44.1 kHz stereo 128 kbps"
    body = text(24, 34, title, theme["ink"], 17, weight="600")
    body += text(24, 56, "Apple M5, one thread, multiples of realtime: "
                 "longer is faster", theme["ink2"], 13)
    # Legend: identity is never colour alone.
    x = 24
    for fill, label in ((theme["series"][0], "shine"),
                        (theme["fast"], "shine fast mode (level 0)"),
                        (theme["other"], "other encoders")):
        body += bar(x, 68, 14, 10, fill)
        body += text(x + 20, 77, label, theme["ink2"], 12)
        x += 36 + len(label) * 5.8
    longest = max(value for _, value in data)
    spacing = next(s for s in (100, 250, 500, 1000) if longest / s <= 5)
    for step in range(0, int(longest) + 1, spacing):
        x = left + step * scale
        body += (f'<line x1="{x:.1f}" y1="{top - 6}" x2="{x:.1f}" '
                 f'y2="{height - 34}" stroke="{theme["grid"]}"/>\n')
        body += text(x, height - 18, f"{step}x", theme["ink2"], 11, "middle")
    for i, (label, value) in enumerate(data):
        y = top + i * row_height
        shine = label.startswith("shineenc") or label.startswith("WebAssembly")
        fill = (theme["fast"] if is_fast_mode(label) else
                theme["series"][0] if shine else theme["other"])
        body += text(left - 12, y + thickness / 2 + 5, label, theme["ink"], 13,
                     "end")
        body += bar(left, y, value * scale, thickness, fill)
        body += text(left + value * scale + 8, y + thickness / 2 + 5,
                     f"{value:g}x", theme["ink"], 13)
    body += (f'<line x1="{left}" y1="{top - 6}" x2="{left}" y2="{height - 34}" '
             f'stroke="{theme["axis"]}"/>\n')
    return svg(width, height, theme, body, title)


def quality_chart(header, rows, theme):
    columns = [i for i, name in enumerate(header)
               if any(name.endswith(f"{b} kbps") for b in QUALITY_BITRATES)
               and "mono" not in name]
    series = [(plain(row[0]) + (" (fast mode)" if is_fast_mode(row[0]) else ""),
               [float(row[i]) for i in columns])
              for name in QUALITY_SERIES for row in rows if row[0] == name]
    # The fast mode in a lighter step of shine's blue, as in the speed chart.
    colours = [theme["fast"] if label.endswith("(fast mode)")
               else theme["series"][i] for i, (label, _) in enumerate(series)]
    width, height, left, right, top, bottom = 720, 440, 64, 170, 100, 56
    low, high = 30, 100
    x_of = lambda kbps: left + (kbps - QUALITY_BITRATES[0]) / (
        QUALITY_BITRATES[-1] - QUALITY_BITRATES[0]) * (width - left - right)
    y_of = lambda score: top + (high - score) / (high - low) * (
        height - top - bottom)
    title = "Quality against bitrate, 44.1 kHz stereo"
    body = text(24, 34, title, theme["ink"], 17, weight="600")
    body += text(24, 56, "Estimated listening-test score (PEAQ 2f model), mean "
                 "of 53 items: higher is better", theme["ink2"], 13)
    x = 24
    for i, (label, _) in enumerate(series):
        body += (f'<line x1="{x}" y1="76" x2="{x + 16}" y2="76" '
                 f'stroke="{colours[i]}" stroke-width="2"/>\n')
        body += (f'<circle cx="{x + 8}" cy="76" r="4" '
                 f'fill="{colours[i]}"/>\n')
        body += text(x + 22, 80, label, theme["ink2"], 12)
        x += 30 + len(label) * 6.6
    for score in range(low, high + 1, 10):
        y = y_of(score)
        body += (f'<line x1="{left}" y1="{y:.1f}" x2="{width - right}" '
                 f'y2="{y:.1f}" stroke="{theme["grid"]}"/>\n')
        body += text(left - 10, y + 4, str(score), theme["ink2"], 11, "end")
    for kbps in QUALITY_BITRATES:
        body += text(x_of(kbps), height - bottom + 22, f"{kbps} kbps",
                     theme["ink2"], 11, "middle")
    body += (f'<line x1="{left}" y1="{height - bottom}" x2="{width - right}" '
             f'y2="{height - bottom}" stroke="{theme["axis"]}"/>\n')
    # Direct labels at the line ends, nudged apart where they would collide.
    ends = sorted(((y_of(values[-1]), i) for i, (_, values) in
                   enumerate(series)))
    label_y, last = {}, -100.0
    for y, i in ends:
        last = max(y, last + 16)
        label_y[i] = last
    # The first series is drawn last, over lines it nearly coincides with.
    for i, (label, values) in reversed(list(enumerate(series))):
        colour = colours[i]
        points = " ".join(f"{x_of(k):.1f},{y_of(v):.1f}"
                          for k, v in zip(QUALITY_BITRATES, values))
        body += (f'<polyline points="{points}" fill="none" stroke="{colour}" '
                 f'stroke-width="2" stroke-linejoin="round"/>\n')
        for k, v in zip(QUALITY_BITRATES, values):
            body += (f'<circle cx="{x_of(k):.1f}" cy="{y_of(v):.1f}" r="4" '
                     f'fill="{colour}" stroke="{theme["surface"]}" '
                     f'stroke-width="2"/>\n')
        body += text(width - right + 12, label_y[i] + 4, label, theme["ink"],
                     12)
    return svg(width, height, theme, body, title)


def main():
    readme = sys.argv[1] if len(sys.argv) > 1 else "README.md"
    output = sys.argv[2] if len(sys.argv) > 2 else "doc/images"
    with open(readme) as file:
        lines = file.read().split("\n")
    speed = table_after(lines, "How fast is it?")
    quality = table_after(lines, "How good is it?")
    os.makedirs(output, exist_ok=True)
    for mode, theme in THEMES.items():
        for name, chart in (("speed", speed_chart(*speed, theme)),
                            ("quality", quality_chart(*quality, theme))):
            with open(os.path.join(output, f"{name}-{mode}.svg"), "w") as file:
                file.write(chart)


if __name__ == "__main__":
    main()
