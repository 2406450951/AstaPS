# AstaPS first-unlock rewards

`config/rewards/trans-point-rewards.toml` is a server-owned configuration, generated
on the first startup. Existing configuration is never overwritten. Malformed or
unsupported configuration stops startup; there is no native reward-table fallback.
There is no legacy-path migration: only this path is read.

## Supported selectors

- `["kind=waypoint"]`: ordinary teleport points
- `["kind=statue"]`: Statues of The Seven
- `["kind=domain"]`: dungeon entrances activated by the player
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

For domains, the client `UnlockTransPointReq` for a native `DungeonEntry`
point is the activation event, including story entrances without
`DungeonEntryExcelConfigData.rewardDataId` (scene 3 points 1 and 20).
Native `rewardDataId` no longer decides eligibility or amounts.
The handbook can pre-unlock domain map points for navigation, so a separate
persisted `rewardedDungeonEntries` activation receipt prevents duplicate claims
without mistaking handbook synchronization for player discovery.
Hidden and natively pre-unlocked domain points are ineligible.
Entering or re-entering a dungeon never grants discovery rewards.

For existing player accounts, previously activated domain points have no
historical claim receipt from before this implementation. Their first new
activation request may pay once; no retroactive mass award is performed.

Domain completion loot, resin, tree claims, and repeated challenges are unaffected.
