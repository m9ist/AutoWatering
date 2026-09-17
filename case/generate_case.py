"""Генератор STL корпуса для стека плат автополива.

Корпус — короб со стенками 1.2 мм и отдельная плоская крышка с внутренним бортиком.
Геометрия собирается из прямоугольных блоков (без CSG): каждая стенка режется сеткой
по границам своих окон, ячейки внутри окон не выводятся. Блоки перекрываются на EPS,
чтобы слайсер видел единое тело.

Система координат (внешняя, мм, Z=0 — низ корпуса):
  X — вдоль передней стенки (там клеммники датчиков), 0..OUTER_X
  Y — глубина, 0 — передняя стенка
  Z — вверх, 0 — низ пола

Запуск:  python case/generate_case.py
"""

import struct
from pathlib import Path

# --- параметры: правь здесь ---------------------------------------------------

BOARD_X = 200.0        # плата: сторона с клеммниками датчиков
BOARD_Y = 150.0        # плата: глубина
STACK_Z = 85.0         # полная высота стека: низ стоек .. самая высокая точка

CLEAR_XY = 3.0         # зазор от платы до стенки, с каждой стороны
CLEAR_Z = 3.0          # зазор над самой высокой точкой стека

WALL = 1.2             # стенки
FLOOR = 1.2            # пол
LID = 1.2              # крышка

LIP_H = 5.0            # высота бортика крышки (заходит внутрь короба)
LIP_CLEAR = 0.3        # зазор бортика по периметру, на сторону

# уровни плат внутри стека, от низа стоек
BOTTOM_BOARD_Z = 25.0  # стойки под нижней платой
BOARD_GAP = 22.0       # просвет между платами
BOARD_T = 1.6          # толщина платы

# паз под датчики: передняя стенка, от верхней кромки вниз
SENSOR_SLOT_W = 170.0  # ширина паза (16 клеммников ~160 мм + запас)
SENSOR_SLOT_BASE = 24.0  # высота цоколя под пазом, от внутреннего пола

# окна под USB-C. Центры отсчитываются от ЛЕВОГО края стенки при взгляде на неё
# снаружи — так же, как ты мерил бы линейкой, стоя перед этой стенкой.
USB_WALL = "back"      # back | left | right  (front занята пазом под датчики)
USB_BOTTOM_CENTER = 42.0   # type-C нижней платы (Mega)
USB_TOP_CENTER = 62.0      # type-C верхней платы (ESP)
USB_W = 16.0
USB_H = 12.0

EPS = 0.02             # перекрытие блоков

# --- производные размеры ------------------------------------------------------

INNER_X = BOARD_X + 2 * CLEAR_XY
INNER_Y = BOARD_Y + 2 * CLEAR_XY
INNER_Z = STACK_Z + CLEAR_Z

OUTER_X = INNER_X + 2 * WALL
OUTER_Y = INNER_Y + 2 * WALL
OUTER_Z = FLOOR + INNER_Z

CAV_X0, CAV_X1 = WALL, WALL + INNER_X
CAV_Y0, CAV_Y1 = WALL, WALL + INNER_Y
CAV_Z0, CAV_Z1 = FLOOR, FLOOR + INNER_Z

# уровни плат во внешних координатах (низ платы)
BOTTOM_BOARD_ABS = CAV_Z0 + BOTTOM_BOARD_Z
TOP_BOARD_ABS = BOTTOM_BOARD_ABS + BOARD_T + BOARD_GAP

SLOT_X0 = (OUTER_X - SENSOR_SLOT_W) / 2
SLOT_X1 = SLOT_X0 + SENSOR_SLOT_W
SLOT_Z0 = CAV_Z0 + SENSOR_SLOT_BASE

# --- примитивы ----------------------------------------------------------------


def box(x0, y0, z0, x1, y1, z1):
    """12 треугольников замкнутого параллелепипеда, нормали наружу."""
    p = [
        (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
        (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1),
    ]
    quads = [
        (0, 3, 2, 1),  # низ
        (4, 5, 6, 7),  # верх
        (0, 1, 5, 4),  # -Y
        (2, 3, 7, 6),  # +Y
        (0, 4, 7, 3),  # -X
        (1, 2, 6, 5),  # +X
    ]
    tris = []
    for a, b, c, d in quads:
        tris.append((p[a], p[b], p[c]))
        tris.append((p[a], p[c], p[d]))
    return tris


def normal(t):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = (nx * nx + ny * ny + nz * nz) ** 0.5
    return (nx / length, ny / length, nz / length) if length else (0.0, 0.0, 0.0)


def write_stl(path, tris, name="mesh"):
    with open(path, "wb") as f:
        f.write(name.encode("ascii")[:80].ljust(80, b"\0"))
        f.write(struct.pack("<I", len(tris)))
        for t in tris:
            f.write(struct.pack("<3f", *normal(t)))
            for v in t:
                f.write(struct.pack("<3f", *v))
            f.write(struct.pack("<H", 0))


def plate_with_windows(u0, u1, v0, v1, windows, to_box):
    """Плита в 2D-координатах (u, v) с прямоугольными окнами.

    windows: список (wu0, wu1, wv0, wv1). to_box(u0, u1, v0, v1) -> координаты 3D-бокса.
    Плита режется сеткой по границам окон; ячейки внутри окон пропускаются.
    """
    us = sorted({u0, u1} | {u for w in windows for u in w[0:2] if u0 < u < u1})
    vs = sorted({v0, v1} | {v for w in windows for v in w[2:4] if v0 < v < v1})
    tris = []
    for i in range(len(us) - 1):
        for j in range(len(vs) - 1):
            cu0, cu1, cv0, cv1 = us[i], us[i + 1], vs[j], vs[j + 1]
            cu, cv = (cu0 + cu1) / 2, (cv0 + cv1) / 2
            if any(w[0] < cu < w[1] and w[2] < cv < w[3] for w in windows):
                continue
            tris += box(*to_box(cu0 - EPS, cu1 + EPS, cv0 - EPS, cv1 + EPS))
    return tris


# --- корпус -------------------------------------------------------------------


# стенка -> (длина стенки, mapper из (u, z) в 3D-бокс)
WALLS = {
    "front": (OUTER_X, lambda a, b, c, d: (a, 0, c, b, WALL, d)),
    "back": (OUTER_X, lambda a, b, c, d: (a, OUTER_Y - WALL, c, b, OUTER_Y, d)),
    "left": (OUTER_Y, lambda a, b, c, d: (0, a, c, WALL, b, d)),
    "right": (OUTER_Y, lambda a, b, c, d: (OUTER_X - WALL, a, c, OUTER_X, b, d)),
}


def usb_windows():
    """Окна под type-C нижней и верхней платы, в координатах «снаружи стенки»."""
    return [(c - USB_W / 2, c + USB_W / 2, z - 1.0, z - 1.0 + USB_H)
            for c, z in ((USB_BOTTOM_CENTER, BOTTOM_BOARD_ABS),
                         (USB_TOP_CENTER, TOP_BOARD_ABS))]


def mirror_u(wall, windows):
    """Перевод координат «слева-направо снаружи» в координаты модели.

    На стенках back и left взгляд снаружи направлен против оси (X и Y соответственно),
    поэтому отсчёт зеркалится. На front и right оси совпадают со взглядом.
    """
    if wall not in ("back", "left"):
        return windows
    length = WALLS[wall][0]
    return [(length - u1, length - u0, v0, v1) for u0, u1, v0, v1 in windows]


def windows_for(wall):
    """Окна конкретной стенки: паз под датчики спереди, type-C на USB_WALL."""
    windows = []
    if wall == "front":
        windows.append((SLOT_X0, SLOT_X1, SLOT_Z0, CAV_Z1 + 1))
    if wall == USB_WALL:
        windows += mirror_u(wall, usb_windows())
    return windows


def build_body():
    tris = box(0, 0, 0, OUTER_X, OUTER_Y, FLOOR)  # пол
    for wall, (length, to_box) in WALLS.items():
        tris += plate_with_windows(0, length, FLOOR, CAV_Z1, windows_for(wall), to_box)
    return tris


def build_lid():
    """Крышка: пластина + внутренний бортик. Печатать как есть (бортик вверх)."""
    tris = box(0, 0, 0, OUTER_X, OUTER_Y, LID)

    lx0, lx1 = CAV_X0 + LIP_CLEAR, CAV_X1 - LIP_CLEAR
    ly0, ly1 = CAV_Y0 + LIP_CLEAR, CAV_Y1 - LIP_CLEAR
    z0, z1 = LID - EPS, LID + LIP_H

    # задний и боковые участки бортика
    tris += box(lx0, ly1 - WALL, z0, lx1, ly1, z1)
    tris += box(lx0, ly0, z0, lx0 + WALL, ly1, z1)
    tris += box(lx1 - WALL, ly0, z0, lx1, ly1, z1)
    # передний участок прерывается на ширине паза, чтобы не пережать провода
    tris += box(lx0, ly0, z0, SLOT_X0, ly0 + WALL, z1)
    tris += box(SLOT_X1, ly0, z0, lx1, ly0 + WALL, z1)

    return tris


# --- превью -------------------------------------------------------------------


def render_drawing(path):
    """Проверочный чертёж: стенки снаружи, вырезы и уровни плат стека."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Rectangle

    material = dict(facecolor="#cfd8d3", edgecolor="#2f4f42", linewidth=1.4)
    cut = dict(facecolor="white", edgecolor="#b3452b", linewidth=1.4, hatch="//")

    fig, axes = plt.subplots(1, 3, figsize=(16, 5.2))

    # --- передняя стенка ---
    ax = axes[0]
    ax.add_patch(Rectangle((0, 0), OUTER_X, OUTER_Z, **material))
    ax.add_patch(Rectangle((SLOT_X0, SLOT_Z0), SENSOR_SLOT_W, OUTER_Z - SLOT_Z0, **cut))
    ax.annotate(f"паз под датчики {SENSOR_SLOT_W:.0f} x {OUTER_Z - SLOT_Z0:.0f}",
                ((SLOT_X0 + SLOT_X1) / 2, (SLOT_Z0 + OUTER_Z) / 2),
                ha="center", va="center", fontsize=9)
    ax.annotate(f"цоколь {SENSOR_SLOT_BASE:.0f}", (OUTER_X / 2, SLOT_Z0 / 2),
                ha="center", va="center", fontsize=8)
    ax.set_title(f"Передняя стенка снаружи ({OUTER_X:.1f} x {OUTER_Z:.1f})")
    ax.set_xlim(-10, OUTER_X + 10)
    ax.set_ylim(-10, OUTER_Z + 20)

    # --- стенка с type-C ---
    ax = axes[1]
    usb_wall_len = WALLS[USB_WALL][0]
    ax.add_patch(Rectangle((0, 0), usb_wall_len, OUTER_Z, **material))
    for label, (u0, _, z0, z1) in zip(("USB-C нижней платы", "USB-C верхней платы"),
                                      usb_windows()):
        ax.add_patch(Rectangle((u0, z0), USB_W, z1 - z0, **cut))
        ax.annotate(f"{label}\n{USB_W:.0f} x {USB_H:.0f}, Z {z0:.1f}",
                    (u0 + USB_W * 1.6, (z0 + z1) / 2), ha="left", va="center", fontsize=8)
    ax.set_title(f"Стенка «{USB_WALL}» — вид снаружи ({usb_wall_len:.1f} x {OUTER_Z:.1f})")
    ax.set_xlim(-10, usb_wall_len + 70)
    ax.set_ylim(-10, OUTER_Z + 20)

    # --- разрез: что где по высоте ---
    ax = axes[2]
    ax.add_patch(Rectangle((0, 0), OUTER_Y, FLOOR, **material))
    ax.add_patch(Rectangle((0, 0), WALL, OUTER_Z, **material))
    ax.add_patch(Rectangle((OUTER_Y - WALL, 0), WALL, OUTER_Z, **material))
    ax.add_patch(Rectangle((0, OUTER_Z + 1.0), OUTER_Y, LID,
                           facecolor="#dfe6e2", edgecolor="#2f4f42", linewidth=1.2))
    for z, label in ((BOTTOM_BOARD_ABS, "нижняя плата"),
                     (TOP_BOARD_ABS, "верхняя плата")):
        ax.add_patch(Rectangle((CAV_Y0 + CLEAR_XY, z), BOARD_Y, BOARD_T,
                               facecolor="#3f7d5f", edgecolor="#1d3a2c"))
        ax.annotate(f"{label} Z={z:.1f}", (CAV_Y0 + CLEAR_XY, z + 3), fontsize=8)
    ax.annotate(f"верх стека Z={CAV_Z0 + STACK_Z:.1f}", (CAV_Y0 + 4, CAV_Z0 + STACK_Z),
                fontsize=8, color="#b3452b")
    ax.axhline(CAV_Z0 + STACK_Z, color="#b3452b", linestyle="--", linewidth=0.9)
    ax.annotate(f"крышка {LID:.1f} + бортик {LIP_H:.0f}", (2, OUTER_Z + LID + 3), fontsize=8)
    ax.set_title(f"Разрез по глубине (высота {OUTER_Z:.1f})")
    ax.set_xlim(-10, OUTER_Y + 10)
    ax.set_ylim(-10, OUTER_Z + 30)

    for ax in axes:
        ax.set_aspect("equal")
        ax.set_xlabel("мм")
        ax.grid(alpha=0.25, linewidth=0.4)

    fig.tight_layout()
    fig.savefig(path, dpi=110)


def main():
    out = Path(__file__).parent
    body, lid = build_body(), build_lid()

    write_stl(out / "aw_case_body.stl", body, "aw_case_body")
    write_stl(out / "aw_case_lid.stl", lid, "aw_case_lid")
    render_drawing(out / "aw_case_drawing.png")

    print(f"внутренняя полость : {INNER_X:.1f} x {INNER_Y:.1f} x {INNER_Z:.1f} мм")
    print(f"внешний габарит    : {OUTER_X:.1f} x {OUTER_Y:.1f} x {OUTER_Z:.1f} мм")
    print(f"крышка             : {OUTER_X:.1f} x {OUTER_Y:.1f} x {LID + LIP_H:.1f} мм")
    print(f"паз под датчики    : X {SLOT_X0:.1f}..{SLOT_X1:.1f}, Z от {SLOT_Z0:.1f} до верха")
    for label, (u0, u1, z0, _) in zip(("USB-C нижней платы", "USB-C верхней платы"),
                                      usb_windows()):
        print(f"{label:<19}: стенка «{USB_WALL}», {u0:.1f}..{u1:.1f} от левого края "
              f"снаружи, Z от {z0:.1f}")
    print(f"треугольников      : корпус {len(body)}, крышка {len(lid)}")


if __name__ == "__main__":
    main()
