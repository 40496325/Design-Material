#!/usr/bin/env python3
"""Generate the TX / RX wiring diagrams (docs/wiring/*.drawio) from the pin maps in docs/02_wiring.md.

Re-run after changing a pin assignment, then re-export the PNGs, e.g.
    drawio -x -f png -s 2 -o docs/wiring/wiring_rx_nodemcu32s.png docs/wiring/wiring_rx_nodemcu32s.drawio
"""
from __future__ import annotations

import html
import pathlib
import re

OUT = pathlib.Path(__file__).resolve().parent.parent / "docs" / "wiring"

WIRE = {  # signal -> (colour, dashed)
    "GND": ("#000000", False),
    "VCC": ("#D40000", False),
    "CE": ("#FF8000", False),
    "CSN": ("#B8A100", False),
    "SCK": ("#0050EF", False),
    "MOSI": ("#009900", False),
    "MISO": ("#8000FF", False),
    "IRQ": ("#808080", True),
}
# nRF24 module header, physical numbering (pin, signal)
HEADER = [(1, "GND"), (2, "VCC"), (3, "CE"), (4, "CSN"), (5, "SCK"), (6, "MOSI"), (7, "MISO"), (8, "IRQ")]


class Diagram:
    def __init__(self, name: str):
        self.name = name
        self.cells: list[str] = []
        self.n = 0

    def _id(self) -> str:
        self.n += 1
        return f"c{self.n}"

    def vertex(self, value: str, x: float, y: float, w: float, h: float, style: str) -> str:
        cid = self._id()
        self.cells.append(
            f'<mxCell id="{cid}" value="{html.escape(value, quote=True)}" style="{style}" vertex="1" parent="1">'
            f'<mxGeometry x="{x}" y="{y}" width="{w}" height="{h}" as="geometry"/></mxCell>'
        )
        return cid

    def edge(self, src: str, dst: str, style: str, value: str = "", points: list[tuple[float, float]] | None = None,
             src_pt: tuple[float, float] | None = None, dst_pt: tuple[float, float] | None = None) -> str:
        cid = self._id()
        attrs = f'id="{cid}" value="{html.escape(value, quote=True)}" style="{style}" edge="1" parent="1"'
        if src:
            attrs += f' source="{src}"'
        if dst:
            attrs += f' target="{dst}"'
        geo = '<mxGeometry relative="1" as="geometry">'
        if src_pt:
            geo += f'<mxPoint x="{src_pt[0]}" y="{src_pt[1]}" as="sourcePoint"/>'
        if dst_pt:
            geo += f'<mxPoint x="{dst_pt[0]}" y="{dst_pt[1]}" as="targetPoint"/>'
        if points:
            geo += "<Array as=\"points\">" + "".join(f'<mxPoint x="{px}" y="{py}"/>' for px, py in points) + "</Array>"
        geo += "</mxGeometry>"
        self.cells.append(f"<mxCell {attrs}>{geo}</mxCell>")
        return cid

    def xml(self) -> str:
        body = "".join(self.cells)
        page_id = re.sub(r"[^A-Za-z0-9]+", "_", self.name)
        return (
            f'<diagram name="{html.escape(self.name, quote=True)}" id="{page_id}">'
            '<mxGraphModel dx="1400" dy="900" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" arrows="1" '
            'fold="1" page="1" pageScale="1" pageWidth="1400" pageHeight="900" math="0" shadow="0">'
            '<root><mxCell id="0"/><mxCell id="1" parent="0"/>' + body + "</root></mxGraphModel></diagram>"
        )


BOX = "rounded=1;whiteSpace=wrap;html=1;strokeWidth=2;fontSize=13;"
PIN = "rounded=0;whiteSpace=wrap;html=1;strokeWidth=1;fontSize=11;fillColor=#FFFFFF;fontFamily=Courier New;"
TXT = "text;html=1;strokeColor=none;fillColor=none;align=left;verticalAlign=top;whiteSpace=wrap;fontSize=11;"
TITLE = "text;html=1;strokeColor=none;fillColor=none;align=left;verticalAlign=middle;whiteSpace=wrap;fontSize=20;fontStyle=1;"
CAP = "shape=mxgraph.electrical.capacitors.capacitor_1;html=1;strokeWidth=2;rotation=90;pointerEvents=1;"
DOT = "ellipse;fillColor=#000000;strokeColor=none;"


def wire_style(sig: str, exit_x: float, entry_x: float) -> str:
    col, dashed = WIRE[sig]
    s = (f"edgeStyle=orthogonalEdgeStyle;rounded=1;orthogonalLoop=1;jettySize=auto;html=1;endArrow=none;"
         f"strokeWidth=3;strokeColor={col};exitX={exit_x};exitY=0.5;entryX={entry_x};entryY=0.5;"
         f"fontSize=10;fontColor={col};labelBackgroundColor=#FFFFFF;")
    if dashed:
        s += "dashed=1;"
    return s


def build(name: str, title: str, board: str, board_sub: str, pins: dict[str, tuple[str, str]],
          power_src: str, power_note: str, notes: list[str], vcc_label: str, module_label: str,
          board_fill: str, ldo: str | None = None) -> Diagram:
    """pins: signal -> (board pin label, note)"""
    d = Diagram(name)
    d.vertex(title, 40, 20, 1100, 40, TITLE)
    d.vertex("REII checkpoint - nRF24L01+ (PA+LNA) link bring-up. Pin numbers are GPIO numbers as used in "
             "firmware/lib/linktest/board_pins.h; see docs/02_wiring.md.", 40, 60, 1100, 24, TXT)

    # ---- board -------------------------------------------------------------------------------
    bx, by, bw, bh = 120, 140, 300, 560
    d.vertex(f"<b>{board}</b><br>{board_sub}", bx, by, bw, bh,
             BOX + f"fillColor={board_fill};verticalAlign=top;spacingTop=8;fontSize=14;")
    # order the board-side pins to match the module header numbering so no wires cross
    order = [sig for _, sig in HEADER]
    pin_y0, pitch = by + 110, 52
    board_ports: dict[str, str] = {}
    for i, sig in enumerate(order):
        label, note = pins[sig]
        y = pin_y0 + i * pitch
        pid = d.vertex(label, bx + bw - 90, y, 90, 26, PIN)
        board_ports[sig] = pid
        if note:
            d.vertex(note, bx + 12, y - 2, bw - 110, 30, TXT + "fontSize=10;fontColor=#555555;align=right;")

    # ---- module ------------------------------------------------------------------------------
    mx, my, mw, mh = 860, 140, 320, 560
    d.vertex(f"<b>{module_label}</b><br>nRF24L01+ PA+LNA<br>external antenna", mx, my, mw, mh,
             BOX + "fillColor=#E1F5E1;verticalAlign=top;spacingTop=8;fontSize=14;align=left;spacingLeft=14;")
    ax = mx + mw - 30
    d.vertex("", ax - 7, my - 70, 14, 150, "rounded=1;arcSize=50;fillColor=#000000;strokeColor=#000000;")
    d.vertex("", ax - 12, my + 70, 24, 22, "rounded=0;fillColor=#B3B3B3;strokeColor=#000000;")
    d.vertex("2.4 GHz antenna on SMA / RP-SMA<br>fit BEFORE power-up (PA without load = damage)",
             mx + mw - 250, my + 95, 210, 34, TXT + "fontSize=10;align=right;")
    d.vertex("header J1 (2x4, 2.54 mm)<br>component side view", mx + 110, my + 300, 180, 30, TXT + "fontSize=10;")
    # physical 2x4 header drawing (odd pins left column, even pins right column)
    hx, hy = mx + 130, my + 335
    for pin, sig in HEADER:
        col = (pin - 1) % 2
        row = (pin - 1) // 2
        d.vertex(f"{pin} {sig}", hx + col * 80, hy + row * 30, 76, 26,
                 PIN + f"fontColor={WIRE[sig][0]};" + ("fontStyle=1;" if pin == 1 else ""))
    d.vertex("pin 1 (GND) = square pad", hx, hy + 125, 160, 20, TXT + "fontSize=9;fontColor=#555555;")

    # module-side pin ports (single column so the wires are readable)
    mod_ports: dict[str, str] = {}
    for i, (pin, sig) in enumerate(HEADER):
        y = pin_y0 + i * pitch
        pid = d.vertex(f"{pin}  {sig}", mx, y, 90, 26, PIN + f"fontColor={WIRE[sig][0]};fontStyle=1;")
        mod_ports[sig] = pid

    # ---- wires -------------------------------------------------------------------------------
    for sig in order:
        label = sig if sig not in ("VCC",) else vcc_label
        if sig == "VCC" and ldo:
            yv = pin_y0 + 1 * pitch
            reg = d.vertex(ldo, 540, yv - 12, 110, 50, BOX + "fillColor=#FFF2CC;fontSize=10;strokeWidth=1;")
            d.edge(board_ports[sig], reg, wire_style(sig, 1, 0).replace("#D40000", "#FF6666"), value="+5 V")
            d.edge(reg, mod_ports[sig], wire_style(sig, 1, 0), value=label)
            # regulator GND to the GND rail above it
            d.edge(reg, "", "endArrow=none;html=1;strokeWidth=3;rounded=0;strokeColor=#000000;exitX=0.5;exitY=0;",
                   dst_pt=(595, pin_y0 + 13))
            d.vertex("", 590, pin_y0 + 8, 10, 10, DOT)
        else:
            d.edge(board_ports[sig], mod_ports[sig], wire_style(sig, 1, 0), value=label)

    # ---- decoupling caps on VCC/GND near the module header -----------------------------------
    # wires run horizontally at pin_y + 13: GND is row 0, VCC row 1. Caps hang above the wire block
    # from a short VCC bus down to the GND rail; the VCC tap hops over the GND rail (jumpStyle=arc).
    y_gnd = pin_y0 + 0 * pitch + 13
    y_vcc = pin_y0 + 1 * pitch + 13
    y_bus = 160
    st = "endArrow=none;html=1;strokeWidth=3;rounded=0;"
    red, blk = WIRE["VCC"][0], WIRE["GND"][0]
    x_tap = 700
    d.edge("", "", st + f"strokeColor={red};jumpStyle=arc;jumpSize=12;", src_pt=(x_tap, y_vcc), dst_pt=(x_tap, y_bus))
    d.edge("", "", st + f"strokeColor={red};", src_pt=(x_tap, y_bus), dst_pt=(790, y_bus))
    d.vertex("", x_tap - 5, y_vcc - 5, 10, 10, DOT + f"fillColor={red};")
    for xc, val in ((730, "10 uF"), (790, "100 nF")):
        cy = (y_bus + y_gnd) / 2  # cap centre
        d.vertex("", xc - 30, cy - 15, 60, 30, CAP + "strokeColor=#000000;")
        d.vertex(val, xc + 14, cy - 10, 60, 20, TXT + "fontSize=10;fontStyle=1;")
        d.edge("", "", st + f"strokeColor={red};", src_pt=(xc, y_bus), dst_pt=(xc, cy - 30))
        d.edge("", "", st + f"strokeColor={blk};", src_pt=(xc, cy + 30), dst_pt=(xc, y_gnd))
        d.vertex("", xc - 5, y_gnd - 5, 10, 10, DOT)
    d.vertex("decoupling AT the module header:<br>10 uF (electrolytic / tantalum)<br>+ 100 nF (ceramic)",
             520, y_bus - 20, 170, 60, TXT + "fontSize=10;align=right;fontStyle=2;")

    # ---- power source ------------------------------------------------------------------------
    px, py = 120, 730
    p = d.vertex(power_src, px, py, 300, 60, BOX + "fillColor=#F5F5F5;fontSize=12;")
    d.edge(p, "", "endArrow=block;html=1;strokeWidth=2;rounded=1;"
           "edgeStyle=orthogonalEdgeStyle;exitX=0.5;exitY=0;entryX=0.5;entryY=1;", value="USB 5 V",
           dst_pt=(bx + bw / 2, by + bh))
    d.vertex(power_note, px + 320, py, 380, 80, TXT + "fontSize=10;")

    # ---- notes -------------------------------------------------------------------------------
    d.vertex("<b>Notes</b><br>" + "<br>".join(f"&#8226; {n}" for n in notes), 860, 730, 520, 170,
             TXT + "fontSize=10;")
    return d


def write(path: pathlib.Path, diagrams: list[Diagram]) -> None:
    xml = ('<mxfile host="Electron" type="device">' + "".join(d.xml() for d in diagrams) + "</mxfile>")
    path.write_text(xml)
    print("wrote", path)


COMMON_NOTES = [
    ("nRF24 VCC is 3.3 V ONLY (1.9-3.6 V abs. max) - never 5 V. Signals are 5 V tolerant on the 3.3 V-powered chip, "
     "but all boards here are 3.3 V logic anyway."),
    "Keep SPI leads short (&lt; 10 cm) on the breadboard; the firmware runs SPI at 4 MHz for this reason.",
    "IRQ (dashed) is optional - the firmware polls; leave it unconnected if you are short of pins.",
    "Check with a multimeter: 3.3 V across module header pins 1-2 under load, before the first pio run -t upload.",
]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    rx = build(
        name="RX - NodeMCU-32S",
        title="Receiver / ground-station stand-in: NodeMCU-32S  +  nRF24L01+ PA+LNA",
        board="NodeMCU-32S",
        board_sub="ESP32-WROOM-32, roles: rx / scanner<br>(USB-tethered to the PC, 115200 baud)",
        pins={
            "GND": ("GND", ""),
            "VCC": ("3.3V", "on-board LDO, ~600 mA"),
            "CE": ("P4", "GPIO4"),
            "CSN": ("P5", "GPIO5 - VSPI CS0"),
            "SCK": ("P18", "GPIO18 - VSPI CLK"),
            "MOSI": ("P23", "GPIO23 - VSPI D"),
            "MISO": ("P19", "GPIO19 - VSPI Q"),
            "IRQ": ("P17", "GPIO17 (optional)"),
        },
        power_src="<b>PC USB port</b><br>micro-USB - power + serial (CP2102)",
        power_note="The PC runs tools/run_test.py / channel_analysis.py against this board. "
                   "Use a powered hub or a rear-panel port: the PA+LNA module draws ~115-130 mA bursts on TX at PA_MAX.",
        notes=["Avoid GPIO6-11 (flash), GPIO34-39 (input only), GPIO0/2/12/15 (strapping)."] + COMMON_NOTES,
        vcc_label="VCC 3.3 V",
        module_label="nRF24 module #1 (RX)",
        board_fill="#DAE8FC",
    )

    tx = build(
        name="TX - ESP32-C3",
        title="Transmitter / mobile node: ESP32-C3 dev board  +  nRF24L01+ PA+LNA",
        board="ESP32-C3 dev board",
        board_sub="role: tx<br>(battery powered during the distance sweep)",
        pins={
            "GND": ("GND", ""),
            "VCC": ("3V3", "on-board LDO"),
            "CE": ("GPIO10", ""),
            "CSN": ("GPIO7", "FSPICS0"),
            "SCK": ("GPIO4", "FSPICLK"),
            "MOSI": ("GPIO6", "FSPID"),
            "MISO": ("GPIO5", "FSPIQ"),
            "IRQ": ("GPIO3", "optional"),
        },
        power_src="<b>USB power bank</b><br>(or PC USB for bench / serial BEACON mode)",
        power_note="Some power banks switch off below ~50 mA; the TX idles in Standby-I between bursts. "
                   "If the bank cuts out, add a small keep-alive load or use a bank without auto-off.",
        notes=["Avoid GPIO2/8/9 (strapping), GPIO12-17 (module flash), GPIO18/19 (USB-JTAG), GPIO20/21 (UART0)."]
        + COMMON_NOTES,
        vcc_label="VCC 3.3 V",
        module_label="nRF24 module #2 (TX)",
        board_fill="#FFE6CC",
    )

    nano = build(
        name="TX fallback - Arduino Nano",
        title="Transmitter (fallback): Arduino Nano  +  nRF24L01+ PA+LNA via external 3.3 V regulator",
        board="Arduino Nano",
        board_sub="ATmega328P, 5 V logic board - role: tx<br>(fallback only)",
        pins={
            "GND": ("GND", ""),
            "VCC": ("+5V", "pin 27 - NOT the 3V3 pin"),
            "CE": ("D9", ""),
            "CSN": ("D10", "hardware SS"),
            "SCK": ("D13", "hardware SPI"),
            "MOSI": ("D11", "hardware SPI"),
            "MISO": ("D12", "hardware SPI"),
            "IRQ": ("D2", "optional (INT0)"),
        },
        power_src="<b>USB power bank / PC USB</b><br>mini-USB, 5 V",
        power_note="Do NOT use the Nano's own 3V3 pin: it comes from the FT232/CH340 USB chip and is limited to ~50 mA - "
                   "the PA+LNA module needs &gt; 100 mA bursts. Put the LDO output (3.3 V) on module pin 2.",
        notes=[("Nano drives 5 V logic into the nRF24 SPI inputs; the nRF24L01+ inputs are 5 V tolerant when VDD = 3.3 V "
                "(datasheet), so no level shifter is required on the breadboard - but the STM32/ESP32 production "
                "path will be 3.3 V anyway."),
               "Fixed SPI pins on the Nano: D13 SCK, D11 MOSI, D12 MISO."] + COMMON_NOTES[1:],
        vcc_label="3.3 V",
        module_label="nRF24 module #2 (TX)",
        board_fill="#E1D5E7",
        ldo="<b>3.3 V LDO</b><br>AMS1117-3.3 / LD1117V33<br>IN  -  GND  -  OUT",
    )

    write(OUT / "wiring_rx_nodemcu32s.drawio", [rx])
    write(OUT / "wiring_tx_esp32c3.drawio", [tx, nano])


if __name__ == "__main__":
    main()
