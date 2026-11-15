"""Procedural vector source for zpilot's build-generated desktop icon; no imported artwork."""

import sys
from pathlib import Path

sys.dont_write_bytecode = True
from iconlib import Canvas, generate

ROOT = Path(__file__).resolve().parents[1]


def draw(canvas: Canvas) -> None:
    """One continuous rocket hull, engine flame and chained orb inside a rocky cavern."""
    canvas.circle(50, 50, 48, (19, 31, 51))
    canvas.polygon(((9, 43), (20, 33), (24, 48), (18, 63), (28, 70), (39, 68),
                    (48, 94), (29, 88), (14, 74), (5, 58)), (75, 83, 96))
    canvas.line(((20, 33), (24, 48), (18, 63), (28, 70), (39, 68)), 2, (76, 188, 195))
    canvas.line(((44, 53), (38, 59), (34, 67), (32, 74)), 3, (169, 190, 204))
    canvas.circle(31, 81, 10, (43, 143, 166))
    canvas.circle(31, 81, 7, (117, 241, 224))
    canvas.circle(29, 79, 3, (225, 255, 242))
    canvas.polygon(((51, 54), (61, 54), (56, 77)), (255, 157, 61))
    canvas.polygon(((53, 54), (59, 54), (56, 67)), (255, 230, 150))
    canvas.polygon(((56, 12), (64, 27), (64, 43), (74, 58), (60, 52), (57, 59),
                    (54, 52), (40, 58), (48, 43), (48, 27)), (226, 238, 244))
    canvas.polygon(((56, 12), (64, 27), (64, 43), (74, 58), (60, 52), (57, 59),
                    (56, 47)), (132, 184, 209))
    for x, y, r in ((82, 25, 1.5), (79, 75, 1.5), (35, 18, 1)):
        canvas.circle(x, y, r, (162, 199, 230))


if __name__ == "__main__":
    raise SystemExit(generate(draw, ROOT / "build/generated/zpilot.ico"))
