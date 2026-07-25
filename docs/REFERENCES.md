# Visual references

`ARCHITECTURE.md` §5 item 3 puts measured reality above anything hand-authored,
and issue #116 is the worked example of what happens without it: a powertrain
modeled from memory passed its own acceptance criteria and still did not look
like an engine. This file records what was actually looked at, so a later
session can check a shape against the same source rather than against a
recollection of it.

A reference is only listed here once it has been *looked at*. A search result
that was never opened is not a reference.

## Powertrain — issue #116

### Photographs

| Ref | Subject | Source |
| --- | --- | --- |
| R1 | Honda CR125 shifter engine on a kart, right side | Wikimedia Commons, [`Shifter Kart Engine.jpg`](https://commons.wikimedia.org/wiki/File:Shifter_Kart_Engine.jpg) |
| R2 | **Vortex KZ engine installed on a kart**, three-quarter rear-right, 3264 × 2448 | Wikimedia Commons, [`Vortex kart engine (13274903104).jpg`](https://commons.wikimedia.org/wiki/File:Vortex_kart_engine_(13274903104).jpg) |
| R3 | Air-cooled 100 cc kart engine, side | Wikimedia Commons, [`Kosmic TS28.JPG`](https://commons.wikimedia.org/wiki/File:Kosmic_TS28.JPG) |
| R4 | **Kart radiator mounted on a CRG, front three-quarter, with the 55° annotated** | TKART, *Tricks and secrets for the correct mounting of a radiator*, `radiatore-inclinazione-1.jpg` |
| R5 | **The same radiator face-on**, phone held against it to check the angle | TKART, same article, `radiatore-inclinazione-2.jpg` |

R4 and R5 were supplied by the project owner after two attempts at the radiator
were built from R2 plus prose and both came out wrong. They are not redistributed
here — tkart.it returns 403 to scripted fetches and the images are theirs — but
they are the authority for everything in the radiator section below, and the next
session should ask for them again rather than re-derive from text.

R2 is the primary reference and is worth re-fetching at full resolution: one
frame carries the cylinder, head, spark plug, clutch cover, reed block,
carburetor, exhaust springs, and the radiator, all on an installed KZ engine of
exactly the class this project models.

### Written sources

- New-Line Racing radiator range, core sizes — Fastech-Racing,
  <https://fastech-racing.com/new-line-radiators/>. Kart radiator cores are
  **17 in × 9.5–11.4 in**, i.e. **432 mm fore-aft × 241–290 mm tall**.
- Radiator mounting angle and bracket practice — TKART, *Tricks and secrets for
  the correct mounting of a radiator*, and *New-Line Racing 2020 radiators*
  (both read through search extracts; tkart.it returns 403 to scripted
  fetches). **Reference angle 55° to the horizontal** at 20–30 °C ambient, 45°
  at 10–20 °C, up to 60° above 30 °C, adjusted in 5° steps. Two brackets, and
  **the front bracket is the shorter of the two**.
- Dell'Orto VHSH 30 — 30 mm bore, 35 mm engine spigot, 64 mm air-filter spigot.
  SIP Scootershop listing and the Dell'Orto VHSH handbook,
  <https://www.fastech-racing.com/VHSH_Manual.pdf>.
- Exhaust attachment — Fastech-Racing TM KZ exhaust parts: the chamber is held
  to the cylinder by an **elbow, a flange and springs**.
- KZ2 class construction — 125 cc, water-cooled cylinder, head *and* crankcase,
  reed-valve induction, six-speed gearbox.

### What the references settled

1. **A KZ cylinder has no cooling fins.** It is a water-cooled sand casting: a
   round jacket on a square base flange. The finned barrel in R3 is a 100 cc
   air-cooled engine — a different class. See ADR-0028.
2. **The cylinder and head are bodies of revolution**, not boxes. The head is a
   disc with a six-nut bolt circle and a spark plug standing proud of a boss at
   its center.
3. **The clutch cover is an openwork casting**, a lattice of webs and windows
   with the clutch pressure plate visible through it, not a flat disc.
4. **The float bowl is rectangular and the carburetor body is round** — the
   opposite way round from how the module had it.
5. **A radiator core is a fine mesh**, not a slab with ribs, and the tanks are
   visibly separate sections standing proud of both faces. A New-Line core is
   **dual-pass**: a baffle inside the high tank shows from outside as a welded
   rib splitting the face into a wide inboard section and a narrow outboard one,
   and it is most of why one of these reads as a kart radiator.
6. The radiator hangs off the seat's right wing on two brackets through
   **coil-spring silentblocks**. The filler is a **raised neck**, tall enough to
   pour a bottle into, not a flat cap.
7. **The core sits in the plane a second seat's back would occupy**, immediately
   outboard of the driver's — big fin face pointing forward, reclined by the
   same angle the seat is. R4 annotates 55° to the horizontal and
   `seat_back_angle` is 35° from vertical: one angle, not two. See ADR-0029.

### What was got wrong twice before R4 and R5 arrived

Worth recording, because both wrong versions were built *from* references and
still failed — having a photograph is not the same as having read it.

- **First version**: core leaned sideways about the kart's fore-and-aft axis,
  432 mm running fore-and-aft, face pointing outboard. A long low panel lying
  over the sidepod.
- **Second version**: same wrong axis, extents swapped, so a tall narrow panel
  still leaning sideways.
- **Correct**: rake about the kart's **lateral** axis, face forward. The tell
  that both were wrong was available without any measurement — the big fin face
  was not pointing where the air comes from.

The prose source said "55° with respect to the horizontal", which is true and
was not enough: it does not say which axis, and three axes are consistent with
it. A sentence naming the axis, or one look at R4, would have settled it before
any geometry was written.
