# Water Supply — Maintenance Log

Dated, human-readable field maintenance events. Machine-readable records for eventual **Wayne Maintenance Application** import are in [maintenance/events.jsonl](maintenance/events.jsonl). This is a maintenance record, not a recommendation for a fixed replacement interval.

## 2026-11-15 — Domestic 4.5-inch × 20-inch sediment cartridge replaced

**Event ID:** `WS-MNT-20261115-001`  
**Status:** Completed (reported by owner)  
**System / location:** Water Supply / well room / selectable domestic cartridge-filter branch

**Service:** Replaced the **4.5-inch-diameter × 20-inch-long domestic whole-house sediment cartridge**. In the confirmed plumbing sequence, it is **after the Rusco and upstream large sediment filter**, in the selectable cartridge-filter leg that rejoins the direct bypass leg **before the downstream pressure transducer and Flow Meter 1**. This is not necessarily the other, larger upstream sediment prefilter.

**Installed replacement cartridge (from order/listing screenshot):**
- **Brand:** GOOTHO
- **Style/SKU:** `GBW-20-PPF-X-6` (six-pack listing)
- **Type/media:** 5-micron dual-gradient spun melt-blown polypropylene sediment cartridge
- **Size:** 20-inch length × 4.5-inch diameter
- **Listing:** Six-pack shown at **$79.99**; last-purchased indicator **November 12, 2026**. Displayed listing price is not independently verified as the user's paid invoice total; estimated price per cartridge is $13.33 if divided evenly.
- **Quantity installed in this event:** 1. Approximate remaining quantity from this six-pack is 5, **only if no other cartridge from the pack has been used**.

**After-service Home Assistant snapshot, approximately 17:52 MST, November 15:**

| Field | Observed value |
|---|---:|
| Upstream pressure | 52.34 PSI |
| Downstream pressure | 52.06 PSI |
| Total upstream–downstream pressure difference | 0.28 PSI |
| Domestic Flow Meter 1 instantaneous flow | 0 GPM |
| Domestic Flow Meter 1 accumulated volume | 670.44 US gal |
| Domestic Flow Meter 1 accumulated pulses | 749,797 |

**Interpretation:** Pressure and totalizer values are reference snapshots only. With **zero flow**, 0.28 PSI is not a meaningful measure of the cartridge's clean-flow restriction. The transducers also span other filters and fittings. A future baseline should compare upstream–downstream differential **at similar measured flow rates and matching valve configuration**, ideally after the filter is fully wetted/air-purged.

**Future service interval:** Not yet established. Use observed pressure drop under controlled flow, sediment loading, and water quality; do not automatically adopt cartridge-label month guidance as a tested system-specific interval.

### Post-replacement flow/pressure performance baseline — 2026-11-15 19:32–20:32 MST

Source: Home Assistant export `3b24ec7b-7390-4b92-bc3a-f63a2052b9ae.csv` covering **19:32:18–20:32:18 MST**. Includes upstream/downstream PSI and domestic Flow Meter 1 GPM. Pairing of nearest upstream/downstream pressure observations within 0.4 s produced 3,425 matched pressure pairs out of 3,519 upstream observations. Flow-rate state was carried forward between Home Assistant change-only updates.

| Domestic Flow 1 rate | Matched observations (approx.) | Median measured upstream − downstream |
|---|---:|---:|
| 0 GPM | 3,009 | **0.14 PSI** |
| 1–2 GPM | 23 | **0.50 PSI** |
| 3–4 GPM | 383 | **1.94 PSI** |
| 4–5 GPM | 59 | **2.10 PSI** |
| 5–7 GPM | 32 | **4.02 PSI** |

- **Observed max domestic flow:** 6.223 GPM. The near-6 GPM flowing differential was approximately 4 PSI, including the **Rusco, larger sediment filter, the selected 4.5×20 filter leg, and intervening piping**; it is not the cartridge-only loss.
- **Pump behavior during large draw:** At ~20:29:26 MST, upstream reached minimum **36.97 PSI** while domestic flow was about **3.43 GPM**, then pressure increased during continued flow—consistent with a normal pump cut-in near 37 PSI, **not independently corroborated by a motor-current sensor**. Upstream high within export **55.57 PSI**, but the final pump cut-out is not contained in the ending segment.
- **Caution:** The 0-flow differential mostly reflects sensor offset/noise, not hydraulic resistance. Flow rates and filter-bypass valve state should be matched before comparing with future clogged-filter observations. Previous 2026-11-13 differential (~2.3 PSI at ~3.5 GPM) was under a different/uncertain domestic path and is not a clean before–after reference. Baseline is provisional but useful.
- This checkpoint is logged separately from the replacement so future Wayne Maintenance reporting can distinguish **service event** from **performance observation**.

