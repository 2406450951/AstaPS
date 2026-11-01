# Mondstadt section 1009 blossom Group gaps (7.1 resource audit)

Status: **PARTIALLY RECOVERABLE METADATA; SCRIPT RECONSTRUCTION BLOCKED**.

This is a pinned **AstaPS resource** audit. None of the observations below authenticates
original Global 7.1 client EXE/metadata source semantics or original server Lua.
Do not manufacture a complete Group Lua from an adjacent template.

## Provenance

- AstaPS-Resource upstream and fork `main` at
  `c0213668889ed05cbeb86660f424c3daf18bfacd`.
- `ExcelBinOutput/BlossomGroupsExcelConfigData.json`
- `Scripts/Scene/3/scene3_block300{1,2,3,4,7,9}.lua`
- `Scripts/Scene/3/scene3_group<groupId>.lua` for 32 extant Mondstadt blossom Groups
- `BinOutput/LevelDesign/Monsters/9536653822034812227.json`
- `BinOutput/LevelDesign/Gadgets/scene3_block3009_levelgadgets.json`
- Independently browsed AstaPS-Resource `main`, existing research branches,
  and upstream file-path commit history for the three missing scripts.

## Registration and execution gap

The 35 Mondstadt rows with `cityId=1`, `refreshTypeVec=[1,3]` reference
35 different Groups. The 32 Groups in blocks 3001 (5), 3002 (5), 3003 (13),
3004 (5), and 3007 (4) all have Group Lua **and** a
`dynamic_load = true` entry in their corresponding `scene3_block*.lua`.

The three section-1009 Groups are **not registered** in
`scene3_block3009.lua`, and their Group Lua files are absent. This is a
two-layer omission; adding a Lua file alone would not reproduce the known-good
resource shape. All three Excel rows also have no `decorateGroupVec`.

| Camp | Group | finish_progress | LevelDesign monster config IDs | LevelDesign gadget config ID with Blossom_Chest_Icon | nextCampIdVec |
| --- | --- | ---: | --- | ---: | --- |
| 101009001 | 133009067 | 2 | 67010, 67011 | 67009 | 101009002 |
| 101009002 | 133009068 | 6 | 68004..68009 | 68003 | 101009003 |
| 101009003 | 133009069 | 4 | 69004..69007 | 69003 | 101001001, 101004001, 101005001 |

The source rows contain both a Group-like numeric reference and the listed
local config numbers. Each monster-row count equals that Camp's
`finish_progress`. The gadget marker is tagged `Blossom_Chest_Icon`.

All 32 present Group Lua scripts have the shared
`GROUP_LOAD -> GROUP_REFRESH -> SELECT_OPTION -> monster progress ->
BLOSSOM_PROGRESS_FINISH -> BLOSSOM_CHEST_DIE` life cycle, two blossom worktop
Gadget definitions (`70360056` / `70360057`), and one reward marker
(`70210106`); their individual entity types, positions, rotations,
config IDs, wave placement, and Suite membership vary. A uniform event skeleton
therefore does **not** reconstruct these unknown parameters.

## What can and cannot be reconstructed

**Directly supported by current imported resource records**:
Camp graph position in the `nextCampIdVec` topology, two supported refresh
types, `finish_progress`, the twelve listed monster config references, and
three gadget icon marker config references.

**Still unknown**:
the three Group's origin/world positions; worktop and reward entity positions;
each monster's `monster_id`, relative position and rotation; original worktop
`config_id` values; Group Suite membership, spawning waves and triggers;
whether the Groups were intentionally excluded from the Block's runtime index;
whether the historical references remain live in Global 7.1.

Across the 32 known Groups, worktop IDs often precede the reward-marker config
ID, but extrapolating such IDs for the three missing Groups is a **hypothesis**
and must not be promoted into deployable resources. The LevelDesign records
listed above do not carry enough information to verify it.

The exact-original Global 7.1 client Sophon manifest/asset-index audit in
`RinoPaw/Genshin-Reverse/scripts/probe-7.1-client-scene-asset-container.py`
records no standalone shipped `*.lua` file names and explicitly marks
scene-instance content parsing as unresolved. That inventory cannot establish
original server-side Group Lua or whether these three Groups remain active.

## Integration decision

Keep the three missing Groups in the original Excel graph as source data.
AstaPS must not select them for executable encounters until it has a complete,
validated original Group execution chain. The current
`BlossomManager.resolveCampGadget` requires an authored matching worktop.
The current resource snapshot omits the incomplete Lua Groups; the runtime
does not independently validate the complete trigger chain. Do not publish
guessed monsters, geometry, or scripted events.

A future reconstruction needs provenance-backed source rows for scene/block
registration, transform/placement, monster types, Suite membership, and triggers.
It must then pass native-script loading, complete fight/reward lifecycle tests,
and full CI before player testing or upstream submission.
