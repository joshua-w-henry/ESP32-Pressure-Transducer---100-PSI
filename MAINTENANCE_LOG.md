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

