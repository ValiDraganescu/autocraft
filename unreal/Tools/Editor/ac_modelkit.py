"""The modelling kit: what every unreal/Tools/Editor/models/<model>.py imports
(MODELLING.md; .claude/skills/ringshadow-model-pipeline).

A model script says only the shape of the model:

    from ac_modelkit import Model, box, cyl, cone, sphere, prism, loft, bar, ring_ellipse

    m = Model("peregrine", category="unit")
    body = m.part("body", at=(0, 0, 60), driver=dict(what="bank and bob", channels=["rotation"], functions=[...]))
    m.mesh(body, "hullLight", [loft(...), box((40, 20, 10), at=(0, 0, 70))])
    m.write()

and the kit builds the Geometry Script meshes, writes one static mesh per
(part, material) at /Game/Models/<model>/SM_<part>__<material>, makes the
material instances it needs, and merges this model's entry into
Content/Models/ModelCatalog.json (every other entry stays as it was).

Authoring space: UE axes in the model frame, centimetres, the unit facing +X,
Y to its right, Z up (a building faces +Y). Prims and part pivots are given in
the model frame at rest; the kit converts each prim to its part's frame
(meshes are stored in the part's own frame, the rest transform not baked in).
`rot` is (pitch, yaw, roll) in degrees, UE's FRotator.

The plain-Python half of this file (the space maths, the ray pieces, the
catalog merge) imports without `unreal`, so `python3 ac_modelkit.py` runs the
checks of `selftest()` (the round trip through SceneKit space on the
Longbow's `hull` entry included).

Facts (2026-10-05, Peregrine): the Geometry Script primitives are all
`append_*` calls that take a UE Transform; a Rotator is built (roll, pitch,
yaw); a revolve takes (radius, height) profile points; the box's front faces
have (b - a) x (c - a) pointing into the solid, which the hand-made lofts
copy. See MODELLING.md "Facts and hiccups".
"""
import hashlib
import json
import math
import os
import sys

try:
    import unreal
except ImportError:  # plain Python: only the pure parts work
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import ac_models as A  # noqa: E402

# Bump when the meshes the kit makes change for the same input.
KIT_VERSION = "1"
CM = 100.0
REPO = os.path.normpath(os.path.join(A.UNREAL_ROOT, ".."))
MESH_ROOT = "/Game/Models"

# --- small vector and matrix maths (row vectors: p' = p . R + t) -------------


def v_add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def v_sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def v_mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def v_dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def v_len(a): return math.sqrt(v_dot(a, a))


def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def v_norm(a):
    n = v_len(a)
    return (a[0] / n, a[1] / n, a[2] / n)


IDENTITY = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def m_mul(a, b):
    """Rows of a . b (apply a, then b to a row vector)."""
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def m_T(a):
    return tuple(tuple(a[j][i] for j in range(3)) for i in range(3))


def m_apply(p, R, t=(0.0, 0.0, 0.0)):
    return (p[0] * R[0][0] + p[1] * R[1][0] + p[2] * R[2][0] + t[0],
            p[0] * R[0][1] + p[1] * R[1][1] + p[2] * R[2][1] + t[1],
            p[0] * R[0][2] + p[1] * R[1][2] + p[2] * R[2][2] + t[2])


def rot3(pitch=0.0, yaw=0.0, roll=0.0):
    """FRotationMatrix(FRotator(pitch, yaw, roll)): rows are the X, Y, Z axes."""
    sp, cp = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    sy, cy = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    sr, cr = math.sin(math.radians(roll)), math.cos(math.radians(roll))
    return ((cp * cy, cp * sy, sp),
            (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp),
            (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp))


def rows_to_rpy(R):
    """(pitch, yaw, roll) in degrees of a rotation matrix made by rot3."""
    sp = max(-1.0, min(1.0, R[0][2]))
    pitch = math.asin(sp)
    if abs(math.cos(pitch)) < 1e-6:
        yaw = 0.0
        roll = math.atan2(R[2][1], R[1][1])
    else:
        yaw = math.atan2(R[0][1], R[0][0])
        roll = math.atan2(-R[1][2], R[2][2])
    return (math.degrees(pitch), math.degrees(yaw), math.degrees(roll))


def aim(direction, side=(0.0, 1.0, 0.0)):
    """A rotation (rows) whose local Z axis points along `direction`, its
    local X chosen from `side` (the local Y axis lies along `side` as far as it
    can). det = +1, as FRotationMatrix's are."""
    z = v_norm(direction)
    x = v_cross(side, z)
    if v_len(x) < 1e-6:
        x = v_cross((1.0, 0.0, 0.0), z)
    x = v_norm(x)
    y = v_cross(z, x)
    return (x, y, z)


# Where a prim's local Z (its extrusion or height axis) goes.
AXES = {
    "z": IDENTITY,
    "x": ((0.0, 0.0, -1.0), (0.0, 1.0, 0.0), (1.0, 0.0, 0.0)),
    "y": ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)),
}
# A planar prism: its polygon in the two named axes, the thickness along the third.
PLANES = {
    "xy": IDENTITY,
    "xz": ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0)),
    "yz": ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
}

# --- UE space <-> SceneKit space (AcSpace.h, AcModelCatalog.cpp) -------------
# SceneKit (x, y, z) is UE (x, z, y) x 100 cm; the 4x4 matrices are row-vector
# with the translation in elements 12 to 14. The swap is its own inverse, so
# UE[i][j] = SK[p(i)][p(j)] with p swapping 1 and 2; the translation also
# changes unit.
_P = (0, 2, 1)


def ue_matrix(R, t):
    """A UE (R rows, t cm) as 16 floats, row by row."""
    rows = [list(R[i]) + [0.0] for i in range(3)] + [list(t) + [1.0]]
    return [x for r in rows for x in r]


def ue_to_sk(m16):
    """16 UE floats (rows X, Y, Z, T; cm) -> SceneKit's 16 floats (cells)."""
    g = lambda r, c: m16[r * 4 + c]
    out = [0.0] * 16
    for i in range(3):
        for j in range(3):
            out[i * 4 + j] = g(_P[i], _P[j])
    for j in range(3):
        out[12 + j] = g(3, _P[j]) / CM
    out[15] = 1.0
    return out


def sk_to_ue(m16):
    """The inverse of ue_to_sk (what AcModelCatalog::TransformFromSceneKitMatrix reads)."""
    g = lambda r, c: m16[r * 4 + c]
    out = [0.0] * 16
    for i in range(3):
        for j in range(3):
            out[i * 4 + j] = g(_P[i], _P[j])
    for j in range(3):
        out[12 + j] = g(3, _P[j]) * CM
    out[15] = 1.0
    return out


def sk_euler(m16):
    """SceneKit eulerAngles (x pitch, y yaw, z roll; applied roll, yaw, pitch)
    of a SceneKit row-vector matrix: R = Rx(pitch) Ry(yaw) Rz(roll)."""
    r = lambda i, j: m16[j * 4 + i]  # the column-vector matrix is the transpose
    yaw = math.asin(max(-1.0, min(1.0, r(0, 2))))
    if abs(math.cos(yaw)) < 1e-6:
        return [math.atan2(r(2, 1), r(1, 1)), yaw, 0.0]
    return [math.atan2(-r(1, 2), r(2, 2)), yaw, math.atan2(-r(0, 1), r(0, 0))]


def sk_point(p):
    """A UE point in cm -> SceneKit cells."""
    return [p[0] / CM, p[2] / CM, p[1] / CM]


def r6(x):
    """Round the way the catalog's floats look (keeps the file small, drops -0)."""
    v = round(float(x), 6)
    return 0.0 if v == 0 else (int(v) if v == int(v) else v)


# --- prims --------------------------------------------------------------------


class Prim:
    """A solid in the model frame: `R` (rows) and `at` place its local frame."""

    kind = "?"

    def __init__(self, at, R):
        self.at = tuple(float(x) for x in at)
        self.R = R

    # -- in the model frame -> in a part's frame
    def placed(self, Rw, tw):
        """The same solid with its frame expressed in the frame (Rw, tw)
        (a part's world rest: p_model = p_part . Rw + tw)."""
        c = self._clone()
        RwT = m_T(Rw)
        c.R = m_mul(self.R, RwT)
        c.at = m_apply(v_sub(self.at, tw), RwT)
        return c

    def _clone(self):
        import copy
        return copy.copy(self)

    def local_points(self):  # representative points in the prim's local frame
        raise NotImplementedError

    def points(self):
        return [m_apply(p, self.R, self.at) for p in self.local_points()]

    def spec(self):
        raise NotImplementedError

    def rays(self):  # SceneKit ray pieces (dicts), the prim's frame being the part's
        raise NotImplementedError

    def _sk_matrix(self, local_offset=(0.0, 0.0, 0.0)):
        t = m_apply(local_offset, self.R, self.at)
        return ue_to_sk(ue_matrix(self.R, t))

    def _transform(self, scale=(1.0, 1.0, 1.0)):
        pitch, yaw, roll = rows_to_rpy(self.R)
        return unreal.Transform(unreal.Vector(*self.at), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw),
                                unreal.Vector(*scale))

    def add_to(self, dm):
        raise NotImplementedError

    def _spec_head(self):
        return [self.kind, [r6(x) for x in self.at], [[r6(x) for x in row] for row in self.R]]


def _origin(base):
    return unreal.GeometryScriptPrimitiveOriginMode.BASE if base else unreal.GeometryScriptPrimitiveOriginMode.CENTER


def _opts():
    return unreal.GeometryScriptPrimitiveOptions()


class Box(Prim):
    kind = "box"

    def __init__(self, size, at, R, base):
        super().__init__(at, R)
        self.size, self.base = tuple(float(x) for x in size), base

    def _zrange(self):
        return (0.0, self.size[2]) if self.base else (-self.size[2] / 2, self.size[2] / 2)

    def local_points(self):
        hx, hy = self.size[0] / 2, self.size[1] / 2
        z0, z1 = self._zrange()
        return [(sx * hx, sy * hy, z) for sx in (-1, 1) for sy in (-1, 1) for z in (z0, z1)]

    def spec(self):
        return self._spec_head() + [list(map(r6, self.size)), self.base]

    def rays(self):
        hx, hy, hz = (s / 2 for s in self.size)
        off = (0.0, 0.0, self.size[2] / 2 if self.base else 0.0)
        # SceneKit's local axes are UE's (x, z, y).
        return [{"hi": [r6(hx / CM), r6(hz / CM), r6(hy / CM)], "lo": [r6(-hx / CM), r6(-hz / CM), r6(-hy / CM)],
                 "transform": [r6(x) for x in self._sk_matrix(off)]}]

    def add_to(self, dm):
        unreal.GeometryScript_Primitives.append_box(dm, _opts(), self._transform(), self.size[0], self.size[1],
                                                    self.size[2], 0, 0, 0, _origin(self.base))


class Cyl(Prim):
    """A cylinder, or a cone frustum when r1 differs from r0; r0 at the base."""

    kind = "cyl"

    def __init__(self, r0, r1, h, at, R, base, sides):
        super().__init__(at, R)
        self.r0, self.r1, self.h, self.base, self.sides = float(r0), float(r1), float(h), base, int(sides)

    def local_points(self):
        z0, z1 = (0.0, self.h) if self.base else (-self.h / 2, self.h / 2)
        pts = []
        for k in range(16):
            a = 2 * math.pi * k / 16
            pts.append((self.r0 * math.cos(a), self.r0 * math.sin(a), z0))
            pts.append((self.r1 * math.cos(a), self.r1 * math.sin(a), z1))
        return pts

    def spec(self):
        return self._spec_head() + [r6(self.r0), r6(self.r1), r6(self.h), self.base, self.sides]

    def rays(self):
        r = (self.r0 + self.r1) / 2
        off = (0.0, 0.0, self.h / 2 if self.base else 0.0)
        return [{"hi": [r6(r / CM), r6(self.h / 2 / CM), r6(r / CM)], "lo": [r6(-r / CM), r6(-self.h / 2 / CM), r6(-r / CM)],
                 "transform": [r6(x) for x in self._sk_matrix(off)], "radius": r6(r / CM), "half": r6(self.h / 2 / CM),
                 "round": "cylinder"}]

    def add_to(self, dm):
        P = unreal.GeometryScript_Primitives
        if abs(self.r0 - self.r1) < 1e-6:
            P.append_cylinder(dm, _opts(), self._transform(), self.r0, self.h, self.sides, 0, True, _origin(self.base))
        else:
            P.append_cone(dm, _opts(), self._transform(), self.r0, max(self.r1, 0.01), self.h, self.sides, 1, True,
                          _origin(self.base))


class Sphere(Prim):
    kind = "sphere"

    def __init__(self, radii, at, R, steps):
        super().__init__(at, R)
        self.radii, self.steps = tuple(float(x) for x in radii), int(steps)

    def local_points(self):
        rx, ry, rz = self.radii
        return [(sx * rx, 0.0, 0.0) for sx in (-1, 1)] + [(0.0, sy * ry, 0.0) for sy in (-1, 1)] + \
               [(0.0, 0.0, sz * rz) for sz in (-1, 1)]

    def points(self):
        # An ellipsoid's extent along each model axis, not the axis points'.
        ext = [math.sqrt(sum((self.R[k][i] * self.radii[k]) ** 2 for k in range(3))) for i in range(3)]
        return [(self.at[0] + sx * ext[0], self.at[1] + sy * ext[1], self.at[2] + sz * ext[2])
                for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]

    def spec(self):
        return self._spec_head() + [list(map(r6, self.radii)), self.steps]

    def rays(self):
        rx, ry, rz = self.radii
        base = {"transform": [r6(x) for x in self._sk_matrix()]}
        if abs(rx - ry) < 1e-6 and abs(ry - rz) < 1e-6:
            r = r6(rx / CM)
            return [dict(base, hi=[r, r, r], lo=[-r, -r, -r], radius=r, round="sphere")]
        return [dict(base, hi=[r6(rx / CM), r6(rz / CM), r6(ry / CM)], lo=[r6(-rx / CM), r6(-rz / CM), r6(-ry / CM)])]

    def add_to(self, dm):
        rx, ry, rz = self.radii
        unreal.GeometryScript_Primitives.append_sphere_lat_long(
            dm, _opts(), self._transform((rx / 50.0, ry / 50.0, rz / 50.0)), 50.0, max(4, self.steps // 2), self.steps,
            unreal.GeometryScriptPrimitiveOriginMode.CENTER)


def _signed_area(pts):
    return 0.5 * sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1]
                     for i in range(len(pts)))


class Prism(Prim):
    """A polygon (two plane axes) extruded `thick` along the third, centred."""

    kind = "prism"

    def __init__(self, pts, thick, at, R):
        super().__init__(at, R)
        pts = [(float(x), float(y)) for x, y in pts]
        if _signed_area(pts) < 0:  # one winding for every caller
            pts.reverse()
        self.pts, self.thick = pts, float(thick)

    def local_points(self):
        h = self.thick / 2
        return [(x, y, z) for x, y in self.pts for z in (-h, h)]

    def spec(self):
        return self._spec_head() + [[[r6(x), r6(y)] for x, y in self.pts], r6(self.thick)]

    def rays(self):
        xs, ys = [p[0] for p in self.pts], [p[1] for p in self.pts]
        h = self.thick / 2
        return [{"hi": [r6(max(xs) / CM), r6(h / CM), r6(max(ys) / CM)], "lo": [r6(min(xs) / CM), r6(-h / CM), r6(min(ys) / CM)],
                 "transform": [r6(x) for x in self._sk_matrix()]}]

    def add_to(self, dm):
        verts = [unreal.Vector2D(x, y) for x, y in self.pts]
        unreal.GeometryScript_Primitives.append_simple_extrude_polygon(
            dm, _opts(), self._transform(), verts, self.thick, 0, True, unreal.GeometryScriptPrimitiveOriginMode.CENTER)


class Loft(Prim):
    """Rings of points joined into a closed shell (hand-made triangles, flat
    shaded). `sections` are rings of model-frame points, each ring with the
    same number of points, in order along the shape; the first and last ring
    are capped. Its frame is the model's (placed() moves the points)."""

    kind = "loft"

    def __init__(self, sections):
        super().__init__((0, 0, 0), IDENTITY)
        self.sections = [[tuple(float(c) for c in p) for p in ring] for ring in sections]
        n = len(self.sections[0])
        if n < 3 or any(len(r) != n for r in self.sections) or len(self.sections) < 2:
            raise ValueError("loft: rings of equal size (3 or more points), 2 or more of them")

    def placed(self, Rw, tw):
        c = self._clone()
        RwT = m_T(Rw)
        c.sections = [[m_apply(v_sub(p, tw), RwT) for p in ring] for ring in self.sections]
        return c

    def local_points(self):
        return [p for ring in self.sections for p in ring]

    def points(self):
        return self.local_points()

    def spec(self):
        return ["loft", [[[r6(c) for c in p] for p in ring] for ring in self.sections]]

    def rays(self):
        out = []
        for a, b in zip(self.sections, self.sections[1:]):
            pts = a + b
            lo = [min(p[i] for p in pts) for i in range(3)]
            hi = [max(p[i] for p in pts) for i in range(3)]
            out.append({"hi": [r6(hi[0] / CM), r6(hi[2] / CM), r6(hi[1] / CM)],
                        "lo": [r6(lo[0] / CM), r6(lo[2] / CM), r6(lo[1] / CM)],
                        "transform": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]})
        return out

    def triangles(self):
        """(a, b, c) point triples, each wound so (b - a) x (c - a) points into
        the solid, as the Geometry Script primitives' do."""
        every = [p for ring in self.sections for p in ring]
        centre = tuple(sum(p[i] for p in every) / len(every) for i in range(3))
        tris = []
        n = len(self.sections[0])
        for r0, r1 in zip(self.sections, self.sections[1:]):
            for k in range(n):
                a, b, c, d = r0[k], r0[(k + 1) % n], r1[(k + 1) % n], r1[k]
                tris += [(a, b, c), (a, c, d)]
        for ring, first in ((self.sections[0], True), (self.sections[-1], False)):
            mid = tuple(sum(p[i] for p in ring) / n for i in range(3))
            for k in range(n):
                tris.append((ring[k], ring[(k + 1) % n], mid))
        out = []
        for a, b, c in tris:
            nrm = v_cross(v_sub(b, a), v_sub(c, a))
            if v_len(nrm) < 1e-9:
                continue
            cen = tuple((a[i] + b[i] + c[i]) / 3 for i in range(3))
            if v_dot(nrm, v_sub(cen, centre)) > 0:  # points out: flip
                b, c = c, b
            out.append((a, b, c))
        return out

    def add_to(self, dm):
        verts, norms, uvs, tris = [], [], [], []
        for a, b, c in self.triangles():
            nrm = v_norm(v_cross(v_sub(b, a), v_sub(c, a)))
            out = v_mul(nrm, -1.0)  # the rule is inward; the normal points out
            base = len(verts)
            for p in (a, b, c):
                verts.append(unreal.Vector(*p))
                norms.append(unreal.Vector(*out))
                uvs.append(unreal.Vector2D(p[0] * 0.01, p[1] * 0.01))
            tris.append(unreal.IntVector(base, base + 1, base + 2))
        buf = unreal.GeometryScriptSimpleMeshBuffers()
        buf.set_editor_property("vertices", verts)
        buf.set_editor_property("normals", norms)
        buf.set_editor_property("uv0", uvs)
        buf.set_editor_property("triangles", tris)
        unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dm, buf, 0)


# --- constructors a model script uses ------------------------------------------


def _rot(rot, axis="z"):
    return m_mul(AXES[axis], rot3(*rot))


def box(size, at=(0, 0, 0), rot=(0, 0, 0), base=False):
    """A box (dx, dy, dz) centred on `at` (or, base=True, standing on it)."""
    return Box(size, at, rot3(*rot), base)


def cyl(r, h, at=(0, 0, 0), axis="z", rot=(0, 0, 0), base=False, r1=None, sides=16):
    """A cylinder of radius r, height h along `axis` (its local Z), centred on
    `at`, or starting at it with base=True. r1 makes it a cone frustum, r at
    the start and r1 at the end."""
    return Cyl(r, r if r1 is None else r1, h, at, _rot(rot, axis), base, sides)


def cone(r, r1, h, at=(0, 0, 0), axis="z", rot=(0, 0, 0), base=False, sides=16):
    return cyl(r, h, at, axis, rot, base, r1, sides)


def sphere(r, at=(0, 0, 0), rot=(0, 0, 0), squash=(1, 1, 1), steps=12):
    """A sphere of radius r, an ellipsoid with `squash` (scales the radius per local axis)."""
    return Sphere((r * squash[0], r * squash[1], r * squash[2]), at, rot3(*rot), steps)


def prism(pts, thick, plane="xy", at=(0, 0, 0), rot=(0, 0, 0)):
    """A flat shape: polygon `pts` in two axes (`plane`: xy, xz or yz; the
    points are relative to `at`), `thick` along the third axis, centred. Wings
    and fins; `rot` turns it about `at`."""
    return Prism(pts, thick, at, _rot(rot, "z") if plane == "xy" else m_mul(PLANES[plane], rot3(*rot)))


def ring_ellipse(x, w, h, cy=0.0, cz=0.0, n=12):
    """A ring of n points in the plane X = x: an ellipse w wide (Y), h high (Z)."""
    return [(x, cy + w * math.cos(2 * math.pi * k / n), cz + h * math.sin(2 * math.pi * k / n)) for k in range(n)]


def ring_box(x, w, h, cy=0.0, cz=0.0, n=12, power=4.0):
    """A ring of n points in X = x like a rounded box (a superellipse), w and h the half sizes."""
    pts = []
    for k in range(n):
        a = 2 * math.pi * k / n
        c, s = math.cos(a), math.sin(a)
        pts.append((x, cy + w * math.copysign(abs(c) ** (2 / power), c), cz + h * math.copysign(abs(s) ** (2 / power), s)))
    return pts


def loft(sections):
    return Loft(sections)


def bar(a, b, w, d=None, shape="box", taper=None, side=(0.0, 1.0, 0.0), sides=14):
    """A limb from point a to point b (model frame): a box w by d across
    (d defaults to w), or a cylinder of radius w/2; `taper` = the width factor
    at b. The long axis is local Z; `side` keeps the box's Y roughly that way."""
    direction = v_sub(b, a)
    length = v_len(direction)
    R = aim(direction, side)
    d = w if d is None else d
    if shape == "cyl":
        return Cyl(w / 2, w / 2 * (1 if taper is None else taper), length, a, R, True, sides)
    if taper is not None:
        # a frustum of a box: a loft of two rectangles
        pts = []
        for k, f in enumerate((1.0, taper)):
            p = v_add(a, v_mul(direction, k))
            corners = [(sx * w / 2 * f, sy * d / 2 * f, 0.0) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            pts.append([m_apply(c, R, p) for c in corners])
        return Loft(pts)
    return Box((w, d, length), a, R, True)


# --- materials ------------------------------------------------------------------


class Material:
    def __init__(self, name, key, instance, blend, team, spec=None):
        self.name, self.key, self.instance, self.blend, self.team, self.spec = name, key, instance, blend, team, spec


def _catalog_materials(doc):
    """name -> (key, materialInstance, blend, team) for every material the catalog
    knows, where the name has one key only (the shared family)."""
    seen = {}
    for m in doc["models"]:
        for p in m["parts"]:
            for x in p["meshes"]:
                seen.setdefault(x["material"], {})[x["materialKey"]] = (x["materialInstance"], x["blend"], list(x["team"]))
    return {n: (k, v[0], v[1], v[2]) for n, d in seen.items() if len(d) == 1 for k, v in d.items()}


def load_catalog():
    with open(A.CATALOG_FILE) as f:
        return json.load(f)


def save_catalog(doc):
    """Write the catalog as import_models.py does (indent 1, no trailing newline), atomically."""
    tmp = A.CATALOG_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(doc, f, indent=1)
    os.replace(tmp, A.CATALOG_FILE)


def merge_entry(doc, entry):
    """Replace the entry with this name, or add it at the end. Nothing else changes."""
    for i, m in enumerate(doc["models"]):
        if m["name"] == entry["name"]:
            doc["models"][i] = entry
            return "replaced"
    doc["models"].append(entry)
    return "added"


# --- the model ---------------------------------------------------------------------


class Part:
    def __init__(self, model, name, parent, at, rot, driver, hidden, handles, light, particles, opacity, note):
        self.model, self.name, self.parent = model, name, parent
        self.R = rot3(*rot)  # world rest rotation
        self.at = tuple(float(x) for x in at)  # world rest position (the pivot)
        self.driver, self.hidden, self.handles = driver, hidden, handles or [name]
        self.light, self.particles, self.opacity, self.note = light, particles, opacity, note
        self.meshes = {}  # material name -> [Prim] in the model frame

    def is_hidden(self):
        return self.hidden or (self.parent is not None and self.parent.is_hidden())

    def local(self):
        """(R, t) of this part relative to its parent (Rl = Rw Rp^T, tl = (tw - tp) Rp^T)."""
        if self.parent is None:
            return self.R, self.at
        RpT = m_T(self.parent.R)
        return m_mul(self.R, RpT), m_apply(v_sub(self.at, self.parent.at), RpT)

    def path(self):
        if self.parent is None:
            return "/" + self.model.canonical
        names, p = [], self
        while p.parent is not None:
            names.append(p.name)
            p = p.parent
        return "/" + self.model.canonical + "".join("/" + n for n in reversed(names))


class Model:
    def __init__(self, base, category="unit", facing=None, notes=None):
        self.base = base
        self.canonical = base + "_blue"
        self.category = category
        self.facing = facing or ("+X forward, +Y up, +Z its right side (heading 0 in the simulation)" if category != "building"
                                 else "+Z forward, +Y up")
        self.notes = list(notes or [])
        self.parts = []
        self.root = Part(self, self.canonical + "_root", None, (0, 0, 0), (0, 0, 0),
                         dict(what="the unit or building transform: position on the ground, heading (yaw = -heading for units)",
                              channels=["position", "rotation"], functions=[]), False, ["root"], None, None, None, "")
        self.parts.append(self.root)
        self._materials = {}
        self._doc = None

    # -- parts
    def part(self, name, parent=None, at=(0, 0, 0), rot=(0, 0, 0), driver=None, hidden=False, handles=None,
             light=None, particles=None, opacity=None, note=""):
        """A rigid part. `at` is its pivot (the joint) in the model frame, `rot` its rest rotation there
        (pitch, yaw, roll degrees, in the model frame); `parent` a Part, else the model's root.
        driver = dict(what=..., channels=[...], functions=[...]) says what moves it."""
        if any(p.name == name for p in self.parts):
            raise ValueError("two parts named " + name)
        p = Part(self, name, parent or self.root, at, rot, driver, hidden, handles, light, particles, opacity, note)
        self.parts.append(p)
        return p

    def mesh(self, part, material, prims, solid=True):
        """Add solids to a part in a material (name of a catalog material, or one made with material()).
        solid=False marks decoration (rivets, rings, light strips, slats): drawn, but no ray piece."""
        if isinstance(prims, Prim):
            prims = [prims]
        if not solid:
            for pr in prims:
                pr.decor = True
        part.meshes.setdefault(material, []).extend(prims)

    # -- materials
    def _known(self):
        if self._doc is None:
            self._doc = load_catalog()
        return _catalog_materials(self._doc)

    def material(self, name, parent=None, color=None, metallic=0.0, roughness=0.5, emissive=None, team=(),
                 blend="opaque", opacity=None, double_sided=False):
        """Declare a material. With no `parent` it is a name that already exists in the catalog (hullLight,
        hullMid, hullDark, hazard, team, chrome, glowCold, glowRed, ...), reused as it is. With `parent`
        ('hull' lit, 'emissive' unlit lamp, 'additive') a new instance is made on first write: color is the
        base colour (linear rgb), emissive the lamp colour (linear, may be above 1), team the flags
        (TeamTint, TeamGlow). The key is a hash of the description, so the same description is one
        instance in every model."""
        if parent is None:
            known = self._known()
            if name not in known:
                raise KeyError("no material %r in the catalog (give it a parent to make it)" % name)
            key, inst, blend_, team_ = known[name]
            self._materials[name] = Material(name, key, inst, blend_, team_)
            return name
        spec = {"name": name, "parent": parent, "color": list(color or (1, 1, 1)), "metallic": metallic,
                "roughness": roughness, "emissive": list(emissive) if emissive else None, "team": sorted(team),
                "blend": blend, "opacity": opacity, "doubleSided": double_sided}
        key = hashlib.md5(json.dumps(spec, sort_keys=True).encode()).hexdigest()[:12]
        path = A.mi_asset_path({"name": name, "key": key})
        self._materials[name] = Material(name, key, A.object_path(path), blend, sorted(team), spec)
        return name

    def _material(self, name):
        if name not in self._materials:
            self.material(name)
        return self._materials[name]

    # -- writing
    def write(self, with_assets=True, catalog=True):
        """Build the assets and merge the catalog entry. Returns the entry."""
        for p in self.parts:
            for mat in p.meshes:
                self._material(mat)
        counts = {}
        if with_assets:
            counts = self._write_assets()
        entry = self._entry(counts)
        if catalog:
            doc = load_catalog()  # fresh: other sessions edit the file
            how = merge_entry(doc, entry)
            save_catalog(doc)
            log("catalog: %s %s (%d models)" % (how, entry["name"], len(doc["models"])))
        self._parts_txt()
        return entry

    def _part_prims(self, part, material):
        Rw, tw = part.R, part.at
        return [pr.placed(Rw, tw) for pr in part.meshes[material]]

    def _write_assets(self):
        import make_materials
        masters = {k: unreal.load_asset(k) for k in (A.M_HULL, A.M_EMISSIVE, A.M_ADDITIVE)}
        for mat in self._materials.values():
            if mat.spec is not None:
                make_material_instance(mat, masters, make_materials)
        counts = {}
        for p in self.parts:
            for mname, prims in p.meshes.items():
                mat = self._materials[mname]
                mi = unreal.load_asset(mat.instance.split(".")[0])
                if mi is None:
                    raise RuntimeError("no material instance " + mat.instance)
                placed = self._part_prims(p, mname)
                path = "%s/%s/SM_%s__%s" % (MESH_ROOT, A.asset_name(self.base), A.asset_name(p.name), A.asset_name(mname))
                counts[(p.name, mname)] = write_mesh(path, placed, mi, mat.blend)
        return counts

    def bounds(self):
        """The rest pose's bounds in SceneKit cells, hidden parts left out."""
        pts = [pt for p in self.parts if not p.is_hidden() for pr in p.meshes.values() for prim in pr for pt in prim.points()]
        if not pts:
            return [0, 0, 0], [0, 0, 0]
        lo = [min(sk_point(p)[i] for p in pts) for i in range(3)]
        hi = [max(sk_point(p)[i] for p in pts) for i in range(3)]
        return [r6(x) for x in lo], [r6(x) for x in hi]

    def _entry(self, counts):
        parts_out = []
        total = 0
        for p in self.parts:
            Rl, tl = p.local()
            m16 = ue_to_sk(ue_matrix(Rl, tl))
            ent = {
                "name": p.name, "path": p.path(),
                "localTransform": [r6(x) for x in m16],
                "position": [r6(x) for x in m16[12:15]],
                "eulerAngles": [r6(x) for x in sk_euler(m16)],
                "scale": [1, 1, 1], "hidden": bool(p.hidden),
            }
            if p.opacity is not None:
                ent["opacity"] = p.opacity
            if p.parent is not None:
                ent["parent"] = p.parent.name
            ent["handles"] = list(p.handles)
            if p.driver:
                ent["driver"] = {k: p.driver[k] for k in ("what", "channels", "functions") if k in p.driver}
            if p.light:
                ent["light"] = p.light
            if p.particles:
                ent["particles"] = p.particles
            rays = []
            meshes = []
            for mname in p.meshes:
                mat = self._materials[mname]
                for prim in self._part_prims(p, mname):
                    if not getattr(prim, "decor", False):
                        rays += prim.rays()
                tris = counts.get((p.name, mname), (0, 0))[0]
                total += tris
                meshes.append({
                    "material": mname, "prim": "%s__%s" % (p.name, mname),
                    "mesh": A.object_path("%s/%s/SM_%s__%s" % (MESH_ROOT, A.asset_name(self.base), A.asset_name(p.name),
                                                                A.asset_name(mname))),
                    "materialInstance": mat.instance, "materialKey": mat.key, "blend": mat.blend,
                    "team": list(mat.team), "triangles": tris,
                })
            if rays:
                ent["rays"] = rays
            ent["meshes"] = meshes
            parts_out.append(ent)
        lo, hi = self.bounds()
        return {"name": self.canonical, "file": "", "category": self.category, "facing": self.facing,
                "boundsMin": lo, "boundsMax": hi, "triangles": total, "team": "blue", "base": self.base,
                "canonical": self.canonical, "notes": self.notes, "parts": parts_out}

    def _parts_txt(self):
        d = os.path.join(REPO, "art", "models", self.base)
        if not os.path.isdir(d):
            return
        lines = ["# Parts of %s, written by unreal/Tools/Editor/ac_modelkit.py from models/%s.py (do not edit)." % (self.base, self.base),
                 "# name, parent, pivot in the model frame (UE cm: x forward, y right, z up), what moves it, materials.",
                 "# `_0` is the unit's left (UE -Y), `_1` its right."]
        for p in self.parts:
            mats = ",".join("%s(%d)" % (m, len(v)) for m, v in p.meshes.items()) or "no mesh"
            what = (p.driver or {}).get("what", "rigid with its parent")
            hid = " [hidden at rest]" if p.hidden else ""
            lines.append("%-14s %-14s pivot (%6.1f, %6.1f, %6.1f)  %s%s | %s" % (
                p.name, p.parent.name if p.parent else "-", p.at[0], p.at[1], p.at[2], what, hid, mats))
        with open(os.path.join(d, "parts.txt"), "w") as f:
            f.write("\n".join(lines) + "\n")


# --- assets ---------------------------------------------------------------------------


def log(msg):
    if unreal:
        unreal.log("[modelkit] " + msg)
    else:
        print("[modelkit] " + msg)


def make_material_instance(mat, masters, make_materials):
    """A new MI_<name>_<key> through make_materials.make_instance, unless it exists."""
    path = mat.instance.split(".")[0]
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return
    s = mat.spec
    desc = {"name": s["name"], "key": mat.key, "blend": s["blend"],
            "lightingModel": "SCNLightingModelConstant" if s["parent"] == "emissive" else "SCNLightingModelPhysicallyBased",
            "baseColor": s["color"], "metallic": s["metallic"], "roughness": s["roughness"]}
    if s["parent"] == "additive":
        desc["blend"] = "additive"
    if s["emissive"]:
        desc["emissive"] = s["emissive"]
    if s["opacity"] is not None:
        desc["opacity"] = s["opacity"]
    if s["doubleSided"]:
        desc["doubleSided"] = True
    make_materials.make_instance(desc, set(s["team"]), masters)
    log("material instance " + path)


def nanite_for(blend, triangles):
    return blend == "opaque" and triangles >= A.NANITE_MIN_TRIANGLES


def build_mesh(prims):
    dm = unreal.DynamicMesh()
    dm.reset()
    for pr in prims:
        pr.add_to(dm)
    return dm


def write_mesh(path, prims, mi, blend):
    """One static mesh from the prims (in the part's frame). Skipped when the
    asset carries the same AcSource hash. Returns (triangles, built)."""
    EAL = unreal.EditorAssetLibrary
    h = hashlib.md5((KIT_VERSION + A.MESH_VERSION + json.dumps([p.spec() for p in prims])).encode()).hexdigest()
    existing = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if existing is not None:
        tag = EAL.get_metadata_tag(existing, "AcSource") or ""
        if tag.split("-")[0] == h and EAL.get_metadata_tag(existing, "AcTriangles"):
            tris = int(EAL.get_metadata_tag(existing, "AcTriangles"))
            nanite = nanite_for(blend, tris)
            if tag == h + ("-n" if nanite else "") and existing.get_material(0) == mi:
                return tris, False
    dm = build_mesh(prims)
    tris = unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(dm)
    nanite = nanite_for(blend, tris)
    ns = unreal.MeshNaniteSettings()
    ns.set_editor_property("enabled", nanite)
    if existing is None:
        folder = path.rsplit("/", 1)[0]
        if not EAL.does_directory_exist(folder):
            EAL.make_directory(folder)
        opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        opts.set_editor_property("enable_collision", False)
        opts.set_editor_property("collision_mode", unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
        opts.set_editor_property("enable_nanite", nanite)
        opts.set_editor_property("nanite_settings", ns)
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_recompute_tangents", True)
        sm, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm, path, opts)
        if sm is None or outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError("cannot create " + path)
    else:
        sm = existing
        opts = unreal.GeometryScriptCopyMeshToAssetOptions()
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_recompute_tangents", True)
        opts.set_editor_property("apply_nanite_settings", True)
        opts.set_editor_property("new_nanite_settings", ns)
        opts.set_editor_property("replace_materials", True)
        opts.set_editor_property("new_materials", [mi])
        _, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm, sm, opts, unreal.GeometryScriptMeshWriteLOD())
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError("cannot update " + path)
    sm.set_material(0, mi)
    body = sm.get_editor_property("body_setup")
    if body is not None:
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    EAL.set_metadata_tag(sm, "AcSource", h + ("-n" if nanite else ""))
    EAL.set_metadata_tag(sm, "AcTriangles", str(tris))
    EAL.set_metadata_tag(sm, "AcMadeBy", "Tools/Editor/ac_modelkit.py")
    EAL.save_loaded_asset(sm)
    return tris, True


# --- checks (plain Python; and in the editor with the unreal part) ----------------------


def selftest(verbose=True):
    """The round trip the skill asks for, on the Longbow's `hull` entry, plus the maths.
    Raises on a failure; returns the report lines."""
    out = []
    doc = load_catalog()
    longbow = next(m for m in doc["models"] if m["name"] == "longbow_blue")
    hull = next(p for p in longbow["parts"] if p["name"] == "hull")
    worst = 0.0
    n = 0
    for piece in hull["rays"]:
        sk = piece["transform"]
        ue = sk_to_ue(sk)
        back = ue_to_sk(ue)
        worst = max(worst, max(abs(a - b) for a, b in zip(sk, back)))
        n += 1
        # the matrix as an (R, t) pair and back through ue_matrix
        R = tuple(tuple(ue[i * 4 + j] for j in range(3)) for i in range(3))
        again = ue_to_sk(ue_matrix(R, tuple(ue[12:15])))
        worst = max(worst, max(abs(a - b) for a, b in zip(sk, again)))
    out.append("hull rays: %d piece transforms SceneKit -> UE -> SceneKit, worst error %.2e" % (n, worst))
    assert worst < 1e-5
    # the part's own localTransform and euler values
    lt = hull["localTransform"]
    assert max(abs(a - b) for a, b in zip(ue_to_sk(sk_to_ue(lt)), lt)) < 1e-6
    # euler: build a SceneKit matrix from random angles and recover them
    import random
    rnd = random.Random(7)
    for _ in range(50):
        a, b, c = rnd.uniform(-1.4, 1.4), rnd.uniform(-1.4, 1.4), rnd.uniform(-3, 3)
        Rx = [[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]]
        Ry = [[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]]
        Rz = [[math.cos(c), -math.sin(c), 0], [math.sin(c), math.cos(c), 0], [0, 0, 1]]
        mm = lambda x, y: [[sum(x[i][k] * y[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
        R = mm(mm(Rx, Ry), Rz)  # column-vector
        # row-vector matrix = transpose, so row j holds column j of R: element (row, col) = R[col][row]
        m16 = [0.0] * 16
        for row in range(3):
            for col in range(3):
                m16[row * 4 + col] = R[col][row]
        m16[15] = 1.0
        e = sk_euler(m16)
        assert max(abs(e[0] - a), abs(e[1] - b), abs(e[2] - c)) < 1e-9, (a, b, c, e)
    out.append("sk_euler: 50 random rotations recovered")
    # UE rotations: rows_to_rpy inverts rot3, also for the axis bases
    for rpy in [(0, 0, 0), (20, 30, 10), (-45, 170, 80), (90, 0, 30)] + [(0, 0, 0)]:
        R = rot3(*rpy)
        R2 = rot3(*rows_to_rpy(R))
        assert max(abs(R[i][j] - R2[i][j]) for i in range(3) for j in range(3)) < 1e-9, rpy
    for ax, R in list(AXES.items()) + list(PLANES.items()):
        R2 = rot3(*rows_to_rpy(R))
        assert max(abs(R[i][j] - R2[i][j]) for i in range(3) for j in range(3)) < 1e-9, ax
    out.append("rot3 / rows_to_rpy: round trip for the axis and plane bases")
    # a placed prim keeps its world points
    prim = box((10, 20, 30), at=(5, 6, 7), rot=(10, 20, 30))
    Rw, tw = rot3(5, 40, 10), (100.0, -20.0, 30.0)
    placed = prim.placed(Rw, tw)
    for a, b in zip(prim.points(), placed.points()):
        back = m_apply(b, Rw, tw)
        assert max(abs(back[i] - a[i]) for i in range(3)) < 1e-9
    out.append("placed(): a prim's points survive the move into a part frame")
    # a part's local transform composes back to its world
    m = Model("selftest_model")
    a = m.part("a", at=(10, 0, 0), rot=(0, 30, 0))
    b = m.part("b", parent=a, at=(30, 5, 7), rot=(10, 50, 20))
    Rl, tl = b.local()
    Rw2 = m_mul(Rl, a.R)
    tw2 = v_add(m_apply(tl, a.R), a.at)
    assert max(abs(Rw2[i][j] - b.R[i][j]) for i in range(3) for j in range(3)) < 1e-9
    assert max(abs(tw2[i] - b.at[i]) for i in range(3)) < 1e-9
    out.append("Part.local(): parent chain composes back to the world rest")
    # the catalog is rewritten byte for byte
    with open(A.CATALOG_FILE) as f:
        text = f.read()
    assert json.dumps(json.loads(text), indent=1) == text
    out.append("catalog: json.dump(indent=1) reproduces the file byte for byte (%d bytes)" % len(text))
    if verbose:
        for line in out:
            log(line)
    return out


if __name__ == "__main__":
    selftest()
