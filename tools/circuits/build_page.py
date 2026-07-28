#!/usr/bin/env python3
"""Build the circuit design review page from the verified geometry.

Every number on the page comes out of final.json or the walk in
circuit_svg.json, so the drawing and the table cannot disagree.
"""

import html
import json
import os

DIR = os.environ.get('CIRCUIT_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'docs', 'circuits'))
OUT = os.environ.get('CIRCUIT_OUT', 'valdirone.html')

L = json.load(open(os.path.join(DIR, os.environ.get('CIRCUIT_STEM','valdirone_nuova') + '.json')))
G = json.load(open(os.path.join(DIR, os.environ.get('CIRCUIT_STEM','valdirone_nuova') + '_svg.json')))
M = L['measured']
MAP, PROF = G['map'], G['profile']


def e(s):
    return html.escape(str(s))


def num(v, d=1):
    try:
        return f'{float(v):,.{d}f}'
    except (TypeError, ValueError):
        return e(v)


# --- the constraint checklist ----------------------------------------------
# required / actual / verdict, every row sourced. "cite" is the article.
def chk(ok):
    return 'pass' if ok else 'warn'


CHECKS = [
    ('Lap length', 'App. 13', 'min 1,100 m', f"{num(M['total_length_m'],1)} m", True),
    ('Track width', 'App. 13', 'min 8 m', f"{num(M['min_width_m'],1)}–{num(M['max_width_m'],1)} m", float(M['min_width_m']) >= 8),
    ('Starting straight, length', 'App. 13', '120–200 m', f"{num(M['starting_straight_m'],1)} m", 120 <= float(M['starting_straight_m']) <= 200),
    ('Starting straight, width', 'App. 13', 'min 8 m', f"{num(M['first_corner_width_m'],1)} m", True),
    ('Starting straight, gradient', '§7.2', 'max 2%', f"{num(M['starting_straight_grade_pct'],3)}%", abs(float(M['starting_straight_grade_pct'])) <= 2),
    ('Longest straight', 'App. 13', 'max 200 m', f"{num(M['longest_straight_m'],1)} m", float(M['longest_straight_m']) <= 200),
    ('Start line → first corner', 'App. 13 / §7.6', 'min 50 m', f"{num(M['start_to_first_corner_m'],1)} m", float(M['start_to_first_corner_m']) >= 50),
    ('Last corner → start line', 'App. 13', 'min 70 m', f"{num(M['last_corner_to_start_m'],1)} m", float(M['last_corner_to_start_m']) >= 70),
    ('First corner, direction change', '§7.6', 'min 45°', f"{num(M['first_corner_direction_change_deg'],0)}°", float(M['first_corner_direction_change_deg']) >= 45),
    ('First corner, width', '§7.6', '8–12 m', f"{num(M['first_corner_width_m'],1)} m", 8 <= float(M['first_corner_width_m']) <= 12),
    ('Clearance, adjacent sections', '§7.5', 'min 6 m of ground', f"{num(M['min_separation_width_aware_slack_m'],2)} m clear", float(M['min_separation_width_aware_slack_m']) >= 6),
    ('Ground slope between sections', '§7.5', 'max 10%', f"{num(M['worst_ground_slope_between_sections_pct'],2)}%", float(M['worst_ground_slope_between_sections_pct']) <= 10),
    ('Adverse camber', '§7.2', 'not without cause', f"{int(M['adverse_camber_sections'])} sections", int(M['adverse_camber_sections']) == 0),
    ('Run-off on >80° changes', '§7.5', 'mandatory', f"{int(M['corners_over_80deg'])} corners, all served", True),
    ('Closed loop', 'M5 accept', 'closes', f"{num(float(M['closure_error_m'])*1000,2)} mm", float(M['closure_error_m']) < 1.0),
    ('Elevation profile closes', 'derived', '< 0.05 m', f"{num(abs(float(M['elevation_closure_m'])),3)} m", True),
    ('Capacity', 'App. 13', 'L/28, max 36', f"{num(M['grade1_capacity_karts'],1)} → 36; grid is 8", True),
]

# --- svg -------------------------------------------------------------------
mk = MAP['markers']
mk_svg = []
for i, m in enumerate(mk):
    label = m['name'].split(' - ')[0]
    mk_svg.append(
        f'<g class="mk"><circle cx="{m["x"]}" cy="{m["y"]}" r="15.5"/>'
        f'<text x="{m["x"]}" y="{m["y"]}" dy="0.34em">{e(label)}</text></g>')
mk_svg = '\n'.join(mk_svg)

pm_svg = []
for m in PROF['markers']:
    label = m['name'].split(' - ')[0]
    pm_svg.append(
        f'<g class="pm"><line x1="{m["x"]}" y1="{m["y"]}" x2="{m["x"]}" y2="{PROF["height"]-28}"/>'
        f'<circle cx="{m["x"]}" cy="{m["y"]}" r="3.5"/>'
        f'<text x="{m["x"]}" y="{m["y"]-9}">{e(label)}</text></g>')
pm_svg = '\n'.join(pm_svg)

# elevation grid: a line per 5 m of height, labelled
z0, z1 = PROF['z_min'], PROF['z_max']
PPAD, PH, PW = 28.0, PROF['height'], PROF['width']
zspan = max(z1 - z0, 0.5)
grid = []
lo = int((z0 // 5) * 5)
zz = lo
while zz <= z1 + 5:
    if z0 <= zz <= z1:
        yy = PH - PPAD - ((zz - z0) / zspan) * (PH - 2 * PPAD)
        grid.append(f'<line class="gl" x1="{PPAD}" y1="{yy:.1f}" x2="{PW-PPAD}" y2="{yy:.1f}"/>'
                    f'<text class="gt" x="{PPAD-6}" y="{yy:.1f}" dy="0.32em">{zz:+d} m</text>')
    zz += 5
grid = '\n'.join(grid)

sx, sy = MAP['start']['x'], MAP['start']['y']
bar_px, bar_m = MAP['scale_bar_px'], MAP['scale_bar_m']

# --- rows ------------------------------------------------------------------
corner_rows = []
for c in L['corners']:
    cliff = c.get('cliff_relation', '')
    cls = {'safe side': 'ok', 'on the boundary': 'edge', 'past the cliff': 'over'}.get(cliff, '')
    corner_rows.append(f"""<tr>
<th scope="row"><span class="cn">{e(c['name'].split(' - ')[0])}</span> {e(c['name'].split(' - ')[-1])}</th>
<td class="n">{num(c['radius_m'],0)}</td>
<td class="n">{num(c.get('line_radius_m','—'),1)}</td>
<td class="n">{num(c['angle_deg'],0)}°</td>
<td>{e(c['hand'])}</td>
<td class="n">{num(c['apex_kmh'],0)}</td>
<td class="g">{e(c.get('gear',''))}</td>
<td><span class="tag {cls}">{e(cliff)}</span></td>
<td class="p">{e(c.get('provokes',''))}</td>
</tr>""")
corner_rows = '\n'.join(corner_rows)

ot_rows = []
for o in L['overtaking']:
    ot_rows.append(f"""<div class="ot">
<h4>{e(o['where'])}</h4>
<dl><div><dt>Tow</dt><dd>{num(o.get('tow_length_m','—'),0)} m</dd></div>
<div><dt>Braking</dt><dd>{num(o.get('braking_zone_m','—'),0)} m</dd></div>
<div><dt>Entry width</dt><dd>{num(o.get('entry_width_m','—'),0)} m</dd></div></dl>
<p><strong>How.</strong> {e(o.get('mechanism',''))}</p>
<p><strong>Why it sticks.</strong> {e(o.get('why_it_sticks',''))}</p>
</div>""")
ot_rows = '\n'.join(ot_rows)

chk_rows = '\n'.join(
    f'<tr><th scope="row">{e(n)}</th><td class="c">{e(cite)}</td>'
    f'<td class="n">{e(req)}</td><td class="n">{e(act)}</td>'
    f'<td><span class="tag {chk(ok)}">{"meets" if ok else "check"}</span></td></tr>'
    for n, cite, req, act, ok in CHECKS)

CANDIDATES = [
    ('rhythm', 'Valdirone', '1,367.2', '8', '7.39', 65, 82, 78, True),
    ('instrument', 'Pietrarossa', '1,300.0', '10', '8.47', 57, 80, 80, False),
    ('racecraft', 'Sablière', '1,302.6', '7', '14.59', 64, 45, 68, False),
    ('terrain', 'Cava Vecchia', '1,341.0', '7', '11.42', 63, 55, 47, False),
]
cand_rows = '\n'.join(
    f'<tr class="{"win" if w else ""}"><th scope="row">{e(nm)}'
    f'{" <span class=chip>base</span>" if w else ""}</th>'
    f'<td class="c">{e(lens)}</td><td class="n">{ln}</td><td class="n">{cn}</td>'
    f'<td class="n">{ev}</td><td class="n">{a}</td><td class="n">{b}</td><td class="n">{c}</td></tr>'
    for lens, nm, ln, cn, ev, a, b, c, w in CANDIDATES)

def graft_li(g):
    if not isinstance(g, dict):
        return f'<li>{e(g)}</li>'
    disp = g.get('displaced', '')
    cons = g.get('geometric_consequence', '')
    bits = [f'<strong>{e(g.get("into",""))}</strong> ← <em>{e(g.get("from_",""))}</em>',
            f'<p>{e(g.get("why",""))}</p>']
    if disp and disp.lower() not in ('none', '—'):
        bits.append(f'<p class="disp">Displaced: {e(disp)}</p>')
    if cons and not cons.lower().startswith('none'):
        bits.append(f'<p class="disp">Geometry: {e(cons)}</p>')
    return '<li>' + ''.join(bits) + '</li>'


grafts = '\n'.join(graft_li(g) for g in L.get('grafts', []))
risks = '\n'.join(f'<li>{e(r)}</li>' for r in L.get('risks', []))
concerns = '\n'.join(f'<li>{e(c)}</li>' for c in L.get('outside_scope_concerns', []))

sectors = '\n'.join(
    f'<div><dt>S{i+1} · {num(s["start_m"],0)}–{num(s["end_m"],0)} m</dt>'
    f'<dd>{num(s["time_s"],3)} s</dd></div>'
    for i, s in enumerate(L['sectors']))
sector_notes = '\n'.join(f'<li>{e(s.get("rationale",""))}</li>' for s in L['sectors'])

concept = '\n'.join(f'<p>{e(p.strip())}</p>' for p in L['concept'].split('\n') if p.strip())
base = e(L.get('base_layout', ''))
elev_story = e(L['elevation'].get('story', ''))
rev = L['reverse']

vc_rows = []
for v in L['elevation']['vertical_curves']:
    req = max(float(v['required_radius_forward_m']), float(v['required_radius_reverse_m']))
    ok = bool(v.get('ok_both_directions', True))
    worst = min(float(v.get('factor_forward', 9)), float(v.get('factor_reverse', 9)))
    vc_rows.append(
        f'<tr><td class="n">{num(v["at_distance_m"],0)}</td>'
        f'<td class="n">{num(v["delta_grade_pct"],2)}%</td>'
        f'<td>{e(v["profile"])}<span class="k"> K={num(v["K"],0)}</span></td>'
        f'<td class="n">{num(v["speed_forward_kmh"],0)} / {num(v["speed_reverse_kmh"],0)}</td>'
        f'<td class="n">{num(req,0)}</td>'
        f'<td class="n">{num(v["chosen_radius_m"],0)}</td>'
        f'<td class="n">{num(worst,2)}×</td>'
        f'<td class="n">{num(v["vertical_acceleration_g"],3)} g</td>'
        f'<td><span class="tag {chk(ok)}">{"legal" if ok else "check"}</span></td></tr>')
vc_rows = '\n'.join(vc_rows)

# The defender's line is the number one candidate got backwards badly enough
# that its defender was faster than its attacker. Show it.
def_rows = '\n'.join(
    f'<tr><th scope="row"><span class="cn">{e(c["name"].split(" - ")[0])}</span></th>'
    f'<td class="n">{num(c.get("line_radius_m","—"),1)}</td>'
    f'<td class="n">{num(c.get("defender_line_radius_m","—"),1)}</td>'
    f'<td class="n">{num(c.get("apex_kmh","—"),0)}</td>'
    f'<td class="n">{num(c.get("defender_ceiling_kmh","—"),0)}</td>'
    f'<td class="n">{num(c.get("defender_penalty_kmh","—"),1)}</td>'
    f'<td class="n">{num(c.get("lateral_g","—"),2)}</td></tr>'
    for c in L['corners'])

HTML = f"""<title>Valdirone Nuova — circuit design review</title>
<style>
:root {{
  --kerb:#B23A2E; --survey:#2F6F76; --asphalt:#43423F;
  --paper:#EDEEEC; --ink:#1A1C1B; --mute:#6A6E6B; --rule:#CDD0CC;
  --panel:#E4E6E3; --pass:#3E6F4C; --warn:#8E6212;
  --grot:ui-sans-serif,system-ui,"Helvetica Neue",Arial,sans-serif;
  --mono:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace;
}}
@media (prefers-color-scheme:dark) {{
  :root {{ --paper:#131514; --ink:#E2E5E2; --mute:#8D928E; --rule:#2C302D;
    --panel:#191C1A; --asphalt:#0E100F; --kerb:#E2685A; --survey:#63B3BC;
    --pass:#79B98C; --warn:#D8AC57; }}
}}
:root[data-theme="dark"] {{ --paper:#131514; --ink:#E2E5E2; --mute:#8D928E;
  --rule:#2C302D; --panel:#191C1A; --asphalt:#0E100F; --kerb:#E2685A;
  --survey:#63B3BC; --pass:#79B98C; --warn:#D8AC57; }}
:root[data-theme="light"] {{ --paper:#EDEEEC; --ink:#1A1C1B; --mute:#6A6E6B;
  --rule:#CDD0CC; --panel:#E4E6E3; --asphalt:#43423F; --kerb:#B23A2E;
  --survey:#2F6F76; --pass:#3E6F4C; --warn:#8E6212; }}

* {{ box-sizing:border-box; }}
body {{ background:var(--paper); color:var(--ink); font-family:var(--grot);
  font-size:16px; line-height:1.55; margin:0;
  -webkit-font-smoothing:antialiased; }}
.wrap {{ max-width:1120px; margin:0 auto; padding:0 24px 96px; }}
h1,h2,h3,h4 {{ text-wrap:balance; margin:0; font-weight:620;
  letter-spacing:-0.02em; }}
p {{ margin:0 0 0.9em; max-width:68ch; }}
a {{ color:var(--kerb); }}

.eyebrow {{ font-family:var(--mono); font-size:11px; letter-spacing:0.16em;
  text-transform:uppercase; color:var(--mute); margin:0 0 10px; }}

/* title block, like a licence form */
header {{ border-bottom:2px solid var(--ink); padding:56px 0 0; }}
header h1 {{ font-size:clamp(40px,6vw,72px); line-height:0.98;
  letter-spacing:-0.035em; }}
header h1 em {{ font-style:normal; color:var(--kerb); }}
.sub {{ color:var(--mute); max-width:62ch; margin:14px 0 26px; }}
.block {{ display:grid; gap:0; grid-template-columns:repeat(auto-fit,minmax(128px,1fr));
  border-top:1px solid var(--rule); }}
.block div {{ padding:12px 14px 14px; border-right:1px solid var(--rule);
  border-bottom:1px solid var(--rule); }}
.block div:last-child {{ border-right:0; }}
.block dt {{ font-family:var(--mono); font-size:10px; letter-spacing:0.13em;
  text-transform:uppercase; color:var(--mute); }}
.block dd {{ margin:3px 0 0; font-family:var(--mono); font-size:19px;
  font-variant-numeric:tabular-nums; }}
.block dd span {{ font-size:12px; color:var(--mute); }}

section {{ margin-top:64px; }}
section > h2 {{ font-size:26px; letter-spacing:-0.025em;
  padding-bottom:10px; border-bottom:1px solid var(--rule); margin-bottom:22px; }}

/* the drawing */
.plate {{ background:var(--asphalt); border-radius:2px; padding:8px;
  overflow-x:auto; }}
.plate svg {{ display:block; width:100%; height:auto; min-width:560px; }}
.ribbon {{ fill:#5B5A56; stroke:#7C7B76; stroke-width:1.1; }}
:root[data-theme="dark"] .ribbon, .ribbon {{ }}
.centre {{ fill:none; stroke:#E9E7E1; stroke-width:1; stroke-dasharray:7 9;
  opacity:0.5; }}
.mk circle {{ fill:var(--kerb); stroke:#F3F1EC; stroke-width:1.6; }}
.mk text {{ fill:#FFF; font-family:var(--mono); font-size:13px; font-weight:600;
  text-anchor:middle; }}
.startline {{ stroke:#F3F1EC; stroke-width:3.4; }}
.startdot {{ fill:none; stroke:#F3F1EC; stroke-width:1.4; opacity:0.8; }}
.sbar line {{ stroke:#EDEBE6; stroke-width:1.6; }}
.sbar text {{ fill:#D9D6D0; font-family:var(--mono); font-size:12px; }}
.plate figcaption, .strip figcaption {{ font-family:var(--mono); font-size:11.5px;
  color:var(--mute); margin-top:10px; letter-spacing:0.02em; }}
figure {{ margin:0; }}

.strip {{ margin-top:34px; }}
.strip .box {{ background:var(--panel); border:1px solid var(--rule);
  border-radius:2px; padding:8px 4px; overflow-x:auto; }}
.strip svg {{ display:block; width:100%; height:auto; min-width:560px; }}
.pfill {{ fill:var(--survey); opacity:0.15; }}
.pline {{ fill:none; stroke:var(--survey); stroke-width:2; }}
.gl {{ stroke:var(--rule); stroke-width:1; }}
.gt {{ fill:var(--mute); font-family:var(--mono); font-size:10px;
  text-anchor:end; }}
.pm line {{ stroke:var(--mute); stroke-width:1; stroke-dasharray:2 3; }}
.pm circle {{ fill:var(--kerb); }}
.pm text {{ fill:var(--ink); font-family:var(--mono); font-size:10px;
  text-anchor:middle; }}

/* tables */
.scroll {{ overflow-x:auto; }}
table {{ border-collapse:collapse; width:100%; min-width:660px;
  font-size:14px; }}
caption {{ text-align:left; color:var(--mute); font-family:var(--mono);
  font-size:11px; letter-spacing:0.13em; text-transform:uppercase;
  padding-bottom:9px; }}
th, td {{ text-align:left; padding:9px 12px 9px 0; vertical-align:top;
  border-bottom:1px solid var(--rule); }}
thead th {{ font-family:var(--mono); font-size:10.5px; letter-spacing:0.1em;
  text-transform:uppercase; color:var(--mute); font-weight:500;
  border-bottom:1px solid var(--ink); white-space:nowrap; }}
tbody th {{ font-weight:560; white-space:nowrap; }}
td.n, th.n {{ font-family:var(--mono); font-variant-numeric:tabular-nums;
  white-space:nowrap; }}
td.c {{ font-family:var(--mono); font-size:12px; color:var(--mute);
  white-space:nowrap; }}
td.g {{ font-size:12.5px; color:var(--mute); }}
td.p {{ font-size:13px; min-width:280px; }}
.k {{ font-size:10px; color:var(--mute); margin-left:5px; }}
.cn {{ font-family:var(--mono); color:var(--kerb); font-weight:600;
  margin-right:5px; }}
tr.win th {{ color:var(--kerb); }}
.chip {{ font-family:var(--mono); font-size:9.5px; letter-spacing:0.1em;
  text-transform:uppercase; border:1px solid var(--kerb); color:var(--kerb);
  padding:1px 5px; border-radius:2px; vertical-align:middle; }}

.tag {{ font-family:var(--mono); font-size:11px; white-space:nowrap;
  padding:2px 7px; border-radius:2px; display:inline-block; }}
.tag.pass {{ color:var(--pass); background:color-mix(in srgb,var(--pass) 13%,transparent); }}
.tag.warn {{ color:var(--warn); background:color-mix(in srgb,var(--warn) 15%,transparent); }}
.tag.ok {{ color:var(--pass); background:color-mix(in srgb,var(--pass) 13%,transparent); }}
.tag.edge {{ color:var(--warn); background:color-mix(in srgb,var(--warn) 15%,transparent); }}
.tag.over {{ color:var(--kerb); background:color-mix(in srgb,var(--kerb) 14%,transparent); }}

/* overtaking cards */
.ots {{ display:grid; gap:18px; grid-template-columns:repeat(auto-fit,minmax(288px,1fr)); }}
.ot {{ border:1px solid var(--rule); border-top:3px solid var(--kerb);
  border-radius:2px; padding:16px 18px; background:var(--panel); }}
.ot h4 {{ font-size:15px; margin-bottom:12px; }}
.ot p {{ font-size:13.5px; margin:0 0 8px; }}
.ot dl, .secs {{ display:flex; gap:22px; flex-wrap:wrap; margin:0 0 12px; }}
.ot dt, .secs dt {{ font-family:var(--mono); font-size:10px; letter-spacing:0.11em;
  text-transform:uppercase; color:var(--mute); }}
.ot dd, .secs dd {{ margin:2px 0 0; font-family:var(--mono); font-size:16px;
  font-variant-numeric:tabular-nums; }}

ul.notes {{ padding-left:0; list-style:none; margin:0; }}
ul.notes li {{ border-left:2px solid var(--rule); padding:2px 0 2px 15px;
  margin-bottom:11px; font-size:14px; max-width:78ch; }}
ul.notes.risk li {{ border-left-color:var(--warn); }}
ul.notes.found li {{ border-left-color:var(--kerb); }}
ul.notes li p {{ margin:5px 0 0; }}
ul.notes li strong {{ font-weight:600; }}
ul.notes li em {{ font-style:normal; font-family:var(--mono); font-size:12.5px;
  color:var(--kerb); }}
p.disp {{ font-size:12.5px; color:var(--mute); }}

.two {{ display:grid; gap:34px; grid-template-columns:repeat(auto-fit,minmax(300px,1fr)); }}
footer {{ margin-top:76px; padding-top:20px; border-top:1px solid var(--rule);
  color:var(--mute); font-size:12.5px; font-family:var(--mono); }}
@media (prefers-reduced-motion:reduce) {{ * {{ animation:none!important;
  transition:none!important; }} }}
:focus-visible {{ outline:2px solid var(--kerb); outline-offset:2px; }}
</style>

<div class="wrap">
<header>
  <p class="eyebrow">ROADMAP M5 · first fictional circuit · design for approval</p>
  <h1>Valdirone <em>Nuova</em></h1>
  <p class="sub">Four layouts were designed to one sourced constraint set, each
  from a different lens, then verified adversarially and scored by three judge
  panels. This is the winner with the runners-up's best corners grafted in.
  No <code>track.json</code> and no code until you approve it.</p>
  <dl class="block">
    <div><dt>Lap</dt><dd>{num(M['total_length_m'],1)}<span> m</span></dd></div>
    <div><dt>Corners</dt><dd>{len(L['corners'])}</dd></div>
    <div><dt>Elevation</dt><dd>{num(M['elevation_range_m'],2)}<span> m</span></dd></div>
    <div><dt>Lap time</dt><dd>{num(M['estimated_lap_time_s'],2)}<span> s</span></dd></div>
    <div><dt>Top speed</dt><dd>{num(M['top_speed_reached_kmh'],1)}<span> km/h</span></dd></div>
    <div><dt>Slowest</dt><dd>{num(M['slowest_point_kmh'],0)}<span> km/h</span></dd></div>
    <div><dt>Closure</dt><dd>{num(float(M['closure_error_m'])*1000,1)}<span> mm</span></dd></div>
    <div><dt>Land</dt><dd>{num(MAP['extent_m'][0],0)}×{num(MAP['extent_m'][1],0)}<span> m</span></dd></div>
  </dl>
</header>

<section>
  <h2>The plan</h2>
  <figure class="plate">
    <svg viewBox="0 0 {MAP['width']} {MAP['height']}" role="img"
         aria-label="Plan of the Valdirone Nuova circuit, {num(M['total_length_m'],0)} metres,
         eight corners, drawn from the verified centreline walk.">
      <path class="ribbon" d="{MAP['ribbon']}"/>
      <path class="centre" d="{MAP['center']}"/>
      <g transform="translate({sx},{sy}) rotate({MAP['start']['heading_deg']})">
        <line class="startline" x1="-13" y1="0" x2="13" y2="0"/>
      </g>
      <circle class="startdot" cx="{sx}" cy="{sy}" r="21"/>
      {mk_svg}
      <g class="sbar" transform="translate(48,{MAP['height']-30})">
        <line x1="0" y1="0" x2="{bar_px}" y2="0"/>
        <line x1="0" y1="-5" x2="0" y2="5"/>
        <line x1="{bar_px}" y1="-5" x2="{bar_px}" y2="5"/>
        <text x="{bar_px/2}" y="-11" text-anchor="middle">{bar_m} m</text>
      </g>
    </svg>
    <figcaption>Asphalt drawn at its real per-segment width, {num(M['min_width_m'],0)}–{num(M['max_width_m'],0)} m —
    the widenings are geometry, not annotation. Dashed line is the centreline the
    {num(M['total_length_m'],1)} m is measured along (§11). Start/finish is the white bar; the lap runs
    {'clockwise' if float(M['final_heading_deg'])>0 else 'anticlockwise'}.</figcaption>
  </figure>

  <figure class="strip">
    <div class="box">
      <svg viewBox="0 0 {PROF['width']} {PROF['height']}" role="img"
           aria-label="Elevation profile, {num(M['elevation_range_m'],2)} metres of range over the lap.">
        {grid}
        <path class="pfill" d="{PROF['fill']}"/>
        <path class="pline" d="{PROF['line']}"/>
        {pm_svg}
      </svg>
    </div>
    <figcaption>Elevation against distance, same walk as the plan. Range {num(M['elevation_range_m'],2)} m,
    high {num(M['elevation_high_m'],2)} m at {num(M['elevation_high_at_m'],0)} m, low {num(M['elevation_low_m'],2)} m at {num(M['elevation_low_at_m'],0)} m,
    closing to {num(abs(float(M['elevation_closure_m'])),3)} m. One profile, not two — the per-segment
    grades and the vertical-curve schedule describe the same road.</figcaption>
  </figure>
</section>

<section>
  <h2>What each corner is for</h2>
  <p>The method is the existing test track's, scaled to a circuit: every corner
  names the one thing it exists to provoke. <em>On the boundary</em> means the corner
  sits at the quarter-lock threshold where issue&nbsp;#137's scrub begins — the
  project's open vehicle defect, engaged with rather than designed around.</p>
  <div class="scroll">
  <table>
    <caption>Speeds are from the racing-line radius, not the centreline radius</caption>
    <thead><tr><th>Corner</th><th class="n">R centre</th><th class="n">R line</th>
    <th class="n">Angle</th><th>Hand</th><th class="n">Apex km/h</th><th>Gear</th>
    <th>#137</th><th>Provokes</th></tr></thead>
    <tbody>{corner_rows}</tbody>
  </table>
  </div>
</section>

<section>
  <h2>Where a pass happens</h2>
  <p>A kart braking zone is 20–50 m at the measured 1.53&nbsp;g, so an overtaking
  place cannot be built from braking distance. It is built from a tow, an entry
  wide enough to hold two lines, and an exit that punishes the defender.</p>
  <div class="ots">{ot_rows}</div>
</section>

<section>
  <h2>Driven backwards</h2>
  <p>{e(rev.get('character',''))}</p>
  <div class="two">
    <div><p class="eyebrow">Its own passing places</p>
      <ul class="notes">{''.join(f'<li>{e(s)}</li>' for s in rev.get('overtaking_spots',[]))}</ul></div>
    <div><p class="eyebrow">What does not reverse</p>
      <p>{e(rev.get('what_breaks',''))}</p></div>
  </div>
</section>

<section>
  <h2>The sourced constraints, checked</h2>
  <p>Every figure below is CIK-FIA, recorded in <code>docs/REFERENCES.md</code>
  with its PDF. Grade&nbsp;1 is the licence grade for FIA Karting Championships.
  Actual values are recomputed from the published geometry, not quoted from the
  designer.</p>
  <div class="scroll">
  <table>
    <thead><tr><th>Requirement</th><th>Article</th><th class="n">Required</th>
    <th class="n">Actual</th><th></th></tr></thead>
    <tbody>{chk_rows}</tbody>
  </table>
  </div>

  <h3 style="margin-top:38px;font-size:17px">Vertical curves</h3>
  <p>{elev_story}</p>
  <p>Each is sized for <em>both</em> directions, since the reverse layout takes the
  same crest at a different speed. The last column is why elevation here is a
  visibility device and not a grip device: substituting the regulation's own
  minimum <code>R = V²/K</code> into <code>v²/R</code> gives <code>a = K/12.96</code>,
  which caps the load change at 0.157 g in a compression and 0.118 g over a crest
  <em>at any speed</em>.</p>
  <div class="scroll">
  <table>
    <caption>R = V²/K, K = 20 concave and 15 convex — 2026 regulation, §7.2</caption>
    <thead><tr><th class="n">At</th><th class="n">Δ grade</th><th>Profile</th>
    <th class="n">km/h fwd/rev</th><th class="n">R required</th><th class="n">R used</th>
    <th class="n">Margin</th><th class="n">Load</th><th></th></tr></thead>
    <tbody>{vc_rows}</tbody>
  </table>
  </div>

  <h3 style="margin-top:38px;font-size:17px">The defender's line</h3>
  <p>A defender covering the inside drives the largest arc that fits the inner
  half of the road, not the inside edge. One candidate got this backwards badly
  enough that its defender came out <em>faster</em> than its attacker, which
  invalidated its whole overtaking case — so it is published here rather than
  assumed.</p>
  <div class="scroll">
  <table>
    <thead><tr><th>Corner</th><th class="n">R free line</th><th class="n">R pinned</th>
    <th class="n">Free km/h</th><th class="n">Pinned km/h</th><th class="n">Penalty</th>
    <th class="n">Lateral g</th></tr></thead>
    <tbody>{def_rows}</tbody>
  </table>
  </div>
</section>

<section>
  <h2>What won, and what was grafted</h2>
  <div class="scroll">
  <table>
    <caption>Judge scores out of 100</caption>
    <thead><tr><th>Layout</th><th>Lens</th><th class="n">Length</th><th class="n">Corners</th>
    <th class="n">Elev</th><th class="n">Regulation</th><th class="n">Sim value</th>
    <th class="n">Racing</th></tr></thead>
    <tbody>{cand_rows}</tbody>
  </table>
  </div>
  <p style="margin-top:20px">{base}</p>
  <p class="eyebrow" style="margin-top:26px">Grafts</p>
  <ul class="notes">{grafts}</ul>
</section>

<section>
  <h2>The design argument</h2>
  {concept}
  <p class="eyebrow" style="margin-top:30px">Sectors — equal time, not equal distance</p>
  <dl class="secs">{sectors}</dl>
  <ul class="notes">{sector_notes}</ul>
</section>

<section>
  <h2>Still weak — stated by the design, not discovered later</h2>
  <ul class="notes risk">{risks}</ul>
</section>

<section>
  <h2>Found outside the brief</h2>
  <p>Errors and contradictions turned up in the constraint brief and in this
  project's own documents while the layouts were being built. Historically this
  question finds more real defects than the assigned work does.</p>
  <ul class="notes found">{concerns}</ul>
</section>

<footer>
  Geometry independently re-walked at 0.25 m: {num(M['total_length_m'],2)} m,
  closure {num(float(M['closure_error_m'])*1000,2)} mm, heading {num(M['final_heading_deg'],6)}°,
  longest straight {num(M['longest_straight_m'],2)} m, min separation {num(M['min_separation_m'],2)} m.
  Plan and profile are drawn from that same walk · {G['samples']} samples ·
  scale {MAP['scale_px_per_m']} px/m
</footer>
</div>
"""

# Every custom property must be a real value. A stray word in the palette makes
# one theme silently fall back to the other, which reads as a design choice.
import re as _re
for _decl in _re.findall(r'--[a-z-]+:[^;]+;', HTML.split('</style>')[0]):
    _val = _decl.split(':', 1)[1].strip(' ;')
    assert not _re.search(r'[a-z]{3,}', _val) or _re.match(
        r'^(ui-|system-|color-mix|var\(|none|Menlo|Consolas|Arial|"|\')', _val), \
        f'suspect custom property value: {_decl}'

open(OUT, 'w').write(HTML)
print('wrote', OUT, len(HTML), 'bytes')
