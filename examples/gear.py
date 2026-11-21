"""A spur gear from its module and tooth count.

Run it in the app (Scripts → Run Script…) or headless:

    nibform --run gear.py [gear.step]

It adds two parameters, `module` and `teeth`, and the gear as a feature of the history.
Change a parameter, or double tap the feature to change its values: the gear is made
again from them.
"""
import math
import sys

import nibform as nf


@nf.generator("Spur gear", module=nf.mm(2), teeth=nf.number(20), width=nf.mm(10),
              bore=nf.mm(8), pressure_angle=nf.deg(20))
def spur_gear(module, teeth, width, bore, pressure_angle):
    """Involute teeth on the XY plane, `width` thick upward, a round bore through it."""
    z = max(6, int(round(teeth)))
    alpha = math.radians(pressure_angle)
    pitch = module * z / 2.0
    base = pitch * math.cos(alpha)
    tip = pitch + module
    root = pitch - 1.25 * module

    def involute(r):
        # The angle the involute has turned through where it reaches radius r.
        t = math.sqrt(max(0.0, (r / base) ** 2 - 1.0))
        return t - math.atan(t)

    # Half a tooth's angle at the base circle: half its thickness at the pitch circle
    # (a quarter of the pitch) plus the involute's turn up to the pitch circle.
    half = math.pi / (2 * z) + involute(pitch)
    start = max(base, root)
    steps = 8
    radii = [start + (tip - start) * i / steps for i in range(steps + 1)]

    def at(r, a):
        return (r * math.cos(a), r * math.sin(a))

    # Where a tooth's rising flank leaves the root circle (straight up to the base circle
    # first, when the root is below it), for the tooth at angle c.
    def foot(c):
        return at(root, c - half + (0.0 if root < base else involute(root)))

    # Tooth by tooth, counterclockwise: up one flank, over the tip, down the other, and
    # along the root to the next tooth.
    gap = 2 * math.pi / z
    path = nf.Path(foot(0.0))
    for i in range(z):
        c = gap * i
        if root < base:
            path.line_to(at(base, c - half))
        path.curve_through([at(r, c - half + involute(r)) for r in radii[1:]])
        path.arc_to(at(tip, c), at(tip, c + half - involute(tip)))
        path.curve_through([at(r, c + half - involute(r)) for r in reversed(radii[:-1])])
        if root < base:
            path.line_to(at(root, c + half))
        path.arc_to(at(root, c + gap / 2), foot(c + gap) if i + 1 < z else foot(0.0))
    gear = path.close().extrude(width)
    if bore > 0:
        gear = gear.cut(nf.circle(bore / 2).extrude(width))
    return gear


doc = nf.document()
doc.set_parameter("module", 2, comment="Gear module")
doc.set_parameter("teeth", 20, kind="number", comment="Number of teeth")
doc.generate(spur_gear, module="module", teeth="teeth")
errors = doc.errors
if errors:
    print("The gear didn't build:", *errors)
elif len(sys.argv) > 1:
    doc.export(sys.argv[1])
    print("Exported", sys.argv[1])
