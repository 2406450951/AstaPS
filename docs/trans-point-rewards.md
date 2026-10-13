# AstaPS first-unlock rewards

`config/rewards/trans-point-rewards.toml` is a server-owned configuration, generated
on the first startup. Existing configuration is never overwritten. Malformed or
unsupported configuration stops startup; there is no native reward-table fallback.
There is no legacy-path migration: only this path is read.

## Supported selectors

- `["kind=waypoint"]`: ordinary teleport points
- `["kind=statue"]`: Statues of The Seven
- `["kind=domain"]`: native reward-eligible dungeon entrances
- `["7:*"]`: every eligible point in scene 7
- `["3:6"]`: one exact scene/point pair

Priority: exact point > scene > kind. Exactly one table applies; missing reward fields
mean zero, not inherited values. `adventureExp`, `primogems`, and `mora` are
non-negative integers; `items` is an array of `{ id = ..., count = ... }`.

The bundled template is `src/main/resources/trans-point-rewards.toml`.
The local generated config directory is gitignored. This intentionally does **not** load
`ExcelBinOutput/TransPointRewardConfigData.json` or look up the old `rewardId`
in `RewardExcelConfigData.json`.

## Native first-unlock boundaries

For waypoints and statues, rewards follow the existing permanent first-unlock state
change. Releasing a quest lock does not pay a second reward. Pre-unlocked points,
portal/unknown point subtypes, and hidden ordinary waypoints are ineligible.

For domains, the native `DungeonEntryExcelConfigData.rewardDataId` field is used
**only** to decide if an entrance is eligible (currently 40 entries). The amount
always comes from TOML. The handbook automatically unlocks domain map entries,
so **first successful dungeon entry** is the reliable implemented claim event,
not `GetDungeonEntryExploreConditionReq` (a condition query). Separate
`rewardedDungeonEntries` player state persists the claim and prevents replay.
A domain whose entry does not declare `rewardDataId` awards nothing.

Existing player accounts lack this new independent receipt; if they have previously
entered a domain before migration, that historic visit cannot reliably be reconstructed
from the handbook-unlock state. Their first successful entry after upgrading may pay once.

Domain completion loot, resin, tree claims, and repeated challenges are unaffected.
