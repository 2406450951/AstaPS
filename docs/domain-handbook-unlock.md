# Domain handbook and discovery state (7.1)

AstaPS uses separate rules for the Adventurer Handbook, world-map discovery, and first-activation rewards.

- `DungeonEntryExcelConfigData.isShowInAdvHandbook` controls **listing** only. An absent flag means false.
- `DungeonEntryExcelConfigData.isDefaultOpen` describes the native entry's initial availability. It is not proof that the player discovered the point, and it does not grant teleport unlocks.
- `Player.unlockedScenePoints` records world-map discovery and can only change through normal unlock operations, quests, or explicit admin actions.
- `GetDailyDungeonEntryInfoRsp` reports `isQuickOpen` only for a genuinely unlocked, not force-locked point. Dungeon `isPassed` reflects recorded dungeon completion instead of being hardcoded.
- `DomainHandbookHelper` may synchronize known state to the client but never adds handbook entries to `unlockedScenePoints`.

Reward selection remains server-owned: `config/rewards/trans-point-rewards.toml`. The `rewardedDungeonEntries` receipt guards each first activation independently.

## Existing player data

Older server revisions inserted handbook entries into the permanent `unlockedScenePoints` sets, often later saved by unrelated player actions. There is no provenance in those sets to distinguish these synthetic unlocks from genuine activations. This change stops new synthetic unlocks but **does not delete existing player discoveries**. Do not blindly clear old records; inspect specific accounts or verify the behavior with a fresh account.

The handbook remains an index of native entries, including entries which are currently undiscovered. Opening the handbook alone does not award rewards or bypass entrance conditions.
