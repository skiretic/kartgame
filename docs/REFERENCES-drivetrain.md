<!--
Fragment for docs/REFERENCES.md — merge as a new section after "Powertrain —
issue #116". Written by the M3b drivetrain agent (issues #36-#40); it does not
belong in the tree as a separate file once merged.
-->

## Drivetrain — issues #36-#40

`src/core/engine.h`, `gearbox.h`, `clutch.h` and `drivetrain.h` are built from
these. The rule from ARCHITECTURE.md §5 item 10 applies to numbers as much as to
shapes: nothing below was recalled, and where a number could not be found it says
so in the same breath as what was assumed instead.

There are no photographs in this section. A gear ratio is not a shape, and the
authority for one is a parts catalog with a tooth count on it.

### What was read

| Ref | Subject | Source |
| --- | --- | --- |
| D1 | **TM KZ R1/R2/R3 gearbox, every gear with its tooth count** | Kartshop, [Gear Box, TM KZ R1 and 10C](https://kartshop.com/shop/gear-box-796c1.html) |
| D2 | TM part 40454, "Gear 6th Mainshaft, Z27, KZ10" — the one gear D1 does not list | Kartshop, [product page](https://kartshop.com/shop/gear-6th-mainshaft-31213p.html) |
| D3 | **TM clutch gear, 75 teeth (part 40385)**, and the KZ10C clutch parts list | Direct-Karting, [KZ10C Clutch](https://direct-karting.com/en/kz10c-clutch) |
| D4 | TM primary drive gear Z18 (part 40318), KZ10C/KZ-R1 | Direct-Karting, [Primary gear TM Z18](https://direct-karting.com/en/primargear-tm-z18-kz10c-kzr1); TM Racing, [part 40318](https://www.tmracingonline.com/tm-40318/) |
| D5 | TM engine sprockets, 14-21 teeth, 428 chain | Direct-Karting, [KZ Engine Sprockets](https://direct-karting.com/en/kz-sprockets-drive) |
| D6 | KZ10C primary shaft and gear shaft part lists, cross-check on D1's naming | Prespo, [gear shaft KZ 10 C](https://www.prespo-kartshop.com/engines-tm/spare-parts-kz-10-c/gear-shaft-kz-10-c/); Direct-Karting, [KZ10C Primary Shaft](https://direct-karting.com/en/kz10c-primary-shaft) |
| D7 | KZ10C clutch part list — dry multi-plate, coated and steel discs, springs | Prespo, [clutch KZ 10 C](https://www.prespo-kartshop.com/engines-tm/spare-parts-kz-10-c/clutch-kz-10-c/) |
| D8 | **KZ dyno figures in the field**: 49.8 bhp measured, ~53 hp claimed for factory engines, and "a KZ in CIK configuration is reasonably about 45 cv" | KartPulse, [KZ Dyno Figures](https://forums.kartpulse.com/t/kz-dyno-figures/13544) |
| D9 | **"With gears the KZ can work with a narrower powerband, say 2500 RPM"**, against 9,000-12,000 rpm for a single-speed kart | KartPulse, [Old Super RoK Info](https://forums.kartpulse.com/t/old-super-rok-info/12434) |
| D10 | **A TM KZ10 "limited to 14,000 rpm"**, plus a real 20/20 sprocket setup and what another karter says it must be doing | KartPulse, [Carburation for TM KZ10 144cc Fun 56 Upgrade](https://forums.kartpulse.com/t/carburation-for-tm-kz10-144cc-fun-56-upgrade/11035) |
| D11 | **KZ1R worked between 11,500 and 14,500 rpm in the high gears, extendable to 15,000**; and an explicit refusal to publish a power scale on a dyno curve | Vroomkart, [TM KZ1R 125, preparazione base vs full, Galiffa Tuning](https://www.vroomkart.it/news/41205/tm-kz1r-125-preparazione-base-vs-preparazione-full-by-galiffa-tuning) |
| D12 | TM 125 KZ10C: 54 mm bore, 54 mm stroke, 30 mm carburetor, six-speed, 20 kg complete | Sodikart, [TM 125 KZ10 C](https://www.sodikart.com/en-gb/karts/engines/tm/125-kz10-c-9.html) |
| D13 | Shifter final drive practice: engine sprocket 16-19, axle sprocket 21-30, worked examples 19/23 = 1.210, 18/22 = 1.222, 17/21 = 1.235, largest sprint axle gear 28 | Bob's 4 Cycle Karting, [Shifter kart rear axle sprocket](https://4cycle.com/karting/threads/shifter-kart-rear-axle-sprocket.102670/) |

Read through search extracts only, and used only as corroboration — the sites
return 403 to a scripted fetch, the same way tkart.it does:

- IAME Screamer 4 KZ: 124.59 cc, 54.00 mm bore, 54.40 mm stroke, reed valve,
  Dell'Orto VHSH 30, six-speed, **five-disc dry clutch**. `iamekarting.com`.
- Honda CR125R, a 125 cc reed-valve two-stroke with published ratios (2.357,
  1.867, 1.579, 1.333, 1.130 in five speeds), as a sanity check that a
  close-ratio 125 gearbox looks like the one below. `motorbikecatalog.com`.

### What the references settled

1. **The whole ratio chain is tooth counts.** From D1, D2 and D4:

   | | 1st | 2nd | 3rd | 4th | 5th | 6th |
   | --- | --- | --- | --- | --- | --- | --- |
   | Mainshaft | 13 | 16 | 18 | 22 | 22 | 27 |
   | Countershaft | 33 | 29 | 27 | 27 | 23 | 25 |
   | Ratio | 2.538 | 1.813 | 1.500 | 1.227 | 1.045 | 0.926 |

   with a primary reduction of **75/18 = 4.167** (D3, D4). Sixth is an
   overdrive, which looks wrong and is not: a 4.167 primary has to be given back
   somewhere. D1 and the KZ10B listings agree gear for gear, and D2 supplies the
   sixth mainshaft gear D1 omits — under the same part number, 40454, that
   Direct-Karting lists as the KZ10-C sixth primary-shaft gear.

2. **Final drive is 18/25 here, and it is a choice, not a specification.** D5
   gives 14-21 teeth on the engine and D13 gives 21-30 on the axle, so the pair
   is a track-by-track setting rather than part of the engine. 18/25 = 1.389 is
   shorter than D13's road-race examples (1.21-1.24) and inside its sprint range;
   it is the value that puts sixth gear at the rev limiter inside
   `kz_reference.h`'s 135-145 km/h, and `Gearbox` exposes both sprockets for
   exactly that reason.

3. **The rev limiter sits between 14,000 and 15,000 rpm.** D10 has a TM KZ10
   limited to 14,000; D11 has a KZ1R worked to 14,500 and extendable to 15,000.
   `engine.h` uses a soft cut at 14,300 tapering to a hard cut at 14,800.

4. **Peak power is 45 hp and the field measures more.** D8 is the honest picture:
   49.8 bhp on one dyno, 55 hp claimed on others, ~53 hp believed genuine for a
   factory engine, and a French poster's summary that a KZ in CIK configuration
   is reasonably about 45 cv given ten separate regulatory restrictions.
   `kz_reference.h` already says 45 hp and this section is the reason to leave it
   there: it is the conservative, class-legal figure, and every larger number in
   D8 is explicitly relative to one dyno.

5. **The powerband is about 2,500 rpm wide.** D9, from an engine builder,
   contrasted directly with the 9,000-12,000 rpm a single-speed kart has to pull
   over. The curve in `engine.h` holds 90% of peak torque from 10,740 to 13,400
   rpm — a 2,660 rpm band — and 80% from 9,995 to 13,915, which is
   `kz_reference.h`'s usable range almost exactly.

6. **The clutch is a dry multi-plate pack on the gearbox input shaft**, not a
   centrifugal clutch (D3, D7, and the IAME extract's five-disc dry clutch). It
   is released by a hand lever, and the parts list is a stack of alternating
   coated and steel discs with coil springs and a pressure plate — which is why
   `clutch.h` models Coulomb friction with a capacity and a lever position rather
   than an rpm-dependent engagement.

### The independent check that mattered most

D10 is not a specification, and it is the strongest evidence in this section. A
driver posts that he runs 20-tooth sprockets front and rear; another karter tells
him "if you're running 20T front and back, you can't be anything like 14,000 in
G6. You should be close to 160 km/h in G5 at that RPM."

Nothing in `gearbox.h` was fitted to that. Setting both sprockets to 20 and
asking for fifth gear at 14,000 rpm returns **166.6 km/h**, and his observed
130 km/h comes back as **9,676 rpm in sixth** — which is the other half of what
he was told. Two numbers, from a chain of four independently sourced ratios and a
tire radius, landing on a stranger's arithmetic. `test_gearbox.cpp` keeps it as a
test case.

### What could not be sourced, and what was assumed instead

Three numbers. Each is flagged in the header that uses it as well as here.

1. **A dyno trace with numbers on both axes.** There is none to find. D11 states
   outright that it publishes KZ curves *without* a power scale, on the grounds
   that dynos disagree, and D8 is a thread of people agreeing that cross-dyno
   comparison is meaningless. So the intermediate points of `WOT_CURVE` are
   interpolated between hard constraints — peak power at
   `kz_reference.h`'s rpm and value, a powerband no wider than
   `kz_reference.h`'s, and a curve weak enough below 9,000 rpm to match D9 — and
   the shape between them is a two-stroke's characteristic knee and cliff. It is
   not measured data and must not be quoted as any.

2. **Crankshaft rotational inertia.** No manufacturer publishes it and no dealer
   listing gives a crank mass. `engine.h` uses **0.0055 kg·m²**, estimated
   geometrically: two full-circle steel webs treated as 110 mm discs 22 mm thick
   give 1.6 kg and 2.5e-3 kg·m² each, and the rod, piston and ignition rotor add
   the rest. The web dimensions are themselves inferred from the 54.4 mm stroke
   and a KZ crankcase, so this is an estimate resting on an estimate. It is the
   number most worth replacing with a measurement, because the drivetrain
   reflects it to the axle through the square of the total ratio — 1.19 kg·m² in
   first gear, which is 63 kg of apparent mass at the tire.

3. **Clutch torque capacity.** Not published, and not derivable from the parts
   list without a friction coefficient and a spring rate. `clutch.h` uses
   **45 N·m**, which is 1.7x the modeled peak crank torque — the usual sizing
   margin for a clutch that must not slip in service. The consequence of it being
   wrong is visible rather than hidden: it sets how much clutch travel a launch
   needs and how hard a botched clutchless upshift hits the axle.

One more thing is chosen rather than sourced, though it is calibrated against an
outcome rather than invented: the **closed-throttle drag slope** in `engine.h`,
0.00462 N·m·s, which puts engine braking at 7.9 N·m at 12,000 rpm. Two-stroke
closed-throttle losses are not published for any kart engine. It was set so that
second gear at 60 km/h decelerates the kart at 0.29 g, which is the fraction of
`kz_reference.h`'s 1.5-2.0 g braking that makes ARCHITECTURE.md §6.3's claim —
that lifting can shape corner entry more than the brakes — true rather than
decorative.
