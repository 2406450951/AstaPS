# Ley-line encounters: original Lua only (Global 7.1)

Java selects the persistent camp cursor and settles rewards. Original Scene Group
Lua defines every eligible encounter: monsters, gadgets, Suites and event
triggers. Selecting worktop option 187 dispatches the group's
EVENT_SELECT_OPTION exactly once. Lua progresses its monster waves and invokes
CreateBlossomChestByGroupId on EVENT_BLOSSOM_PROGRESS_FINISH.

There is no generated blossom Lua, Java fallback combat encounter, or
`Spawns.json` alternative. The selected camp references the original
`SceneGadget` directly. The resource repository owns Lua content; Java only
requires an authored worktop matching the configured gadget identity.

## Native camp selection

- `BlossomManager.Blossom.Type` maps the two supported 7.1 kinds (wealth
  and revelation) to refresh type, worktop Gadget ID, reward Gadget ID
  and Loot CSS reward kind. Other kinds need explicit implementation.
- `BlossomRefreshExcelConfigData.json` supplies one row for each country/kind.
  Its `refreshCount` controls the maximum number of simultaneous camps.
  Adventure Rank requirements gate this country's available kinds.
- `BlossomGroupsExcelConfigData.json` supplies arbitrary country IDs, candidate
  camps, their supported native types and the directed successor graph.
- The original selected Lua worktop's `area_id` must be in the host player's
  unlocked scene areas. An unlocked region therefore permits its own camps;
  unlocking one region does not implicitly permit other regions' camp IDs.
- Each refresh ID has independently saved slots `0..refreshCount-1`.
  A slot retains its own camp ID, incarnation, pending reward and consumed
  state across restart or daily reset. A reward receipt includes the slot.
- Occupied camp IDs and colocated Lua Gadgets cannot be selected twice within
  the same country, regardless of blossom kind. Countries sharing one scene
  still have independent occupancy sets.

The selection algorithm preserves saved camps, then shuffles section heads
when filling missing slots. After a claim it follows native `nextCampIdVec`
edges in breadth-first order, skipping occupied or unusable camps. Both
selection policies are provisional; original 7.1 server behavior is
unverified. Missing original Group files yield no camp.

The current `RinoPaw/AstaPS-Resource/main` removed incomplete blossom Scene
Group Lua in commit `5450d134`. Its remaining 88 executable blossom Group
scripts have the expected event structure. The server does not attempt to
evaluate Lua trigger completeness during camp selection.

Java retains only encounter progress accounting, entity/summon cleanup,
the single reward claim ledger, resin charging and saved camp rotation.
Monster IDs, spawn positions and stages come from source Lua.

The original Global 7.1 starting-camp algorithm has not been independently
recovered for any city. The current implementation chooses a random eligible
section head when filling each available slot.
The snapshot's nextCampIdVec contains multiple edges for some camps
(6 Mondstadt, 7 Liyue, 2 Sumeru). Breadth-first conflict resolution is
a provisional local policy; the original client's branching rule remains unverified.

## Native Lua ownership checkpoint (2026-10-24)

- The 7.1 resource snapshot `BlossomRefreshExcelConfigData.dropVec`
  points to the same nine base quantity distributions as bundled
  `blossoms.lcss`. Directly inspecting all 18 root DropTable nodes and seven
  nested nodes confirms the item IDs, quantity ranges, 100 Adventure EXP and
  Companionship EXP at all tiers. All 20 configured WL0..9 comparisons match
  a capped tier-8 source projection. The original client's world-level mapping,
  random draw implementation and source-file authenticity are independent,
  still-unverified questions. LootCSS deliberately stays the server payout
  authority with a journaled claim.
- `BlossomGroupsExcelConfigData.newGroupVec` owns Group references and
  `nextCampIdVec` the directed camp graph. `RefreshBlossomGroup`
  replaces the active Suite and dispatches GROUP_REFRESH; authored Lua
  chooses a worktop with `GetBlossomRefreshTypeByGroupId`.
- The authored reward `configId`, Group and position now flow from
  `CreateBlossomChestByGroupId` to the claimable server reward entity.
  The claimable gadget type is still selected by the blossom kind.
- One `BlossomActivity` collection tracks fighting, reward-ready and
  claimable phases; the Lua schedule (1 before encounter, 2 fighting,
  3 completed) is derived from this phase and not stored separately.
- The original `EVENT_BLOSSOM_CHEST_DIE` refreshes the same Lua Group.
  Java's successful-claim path instead advances to another native camp
  and unloads the completed Group. Calling both would re-activate
  the old camp. This difference remains explicit pending original
  server-side lifecycle evidence.
- `CreateGadget` now reports failures; native blossom worktop creation
  failures also abort their Lua trigger. Existing identical entities
  count as successful creation.

## Single worktop creation path

The selected camp registers its original SceneGroup, then lets its authored
GROUP_LOAD / GROUP_REFRESH trigger call CreateGadget. The Scene entity-created
hook attaches the selected camp identity and equips the worktop option handler.
There is no parallel Java-created fallback worktop. Script-owned monsters in
inactive BlossomGroups are suppressed. Runnable native Lua resources are
maintained in AstaPS-Resource; Java does not validate their trigger topology.

## Restart and receipt identity

The host's persisted BlossomRunState stores camp ID, monotonically increasing
incarnation, pending reward and exhausted-camp status. Reloading a pending reward
reconstructs the claimable entity from the original Lua reward marker; no duplicate
interactive worktop is spawned. An interrupted fight starts again from the author's
original Lua encounter; monster-progress snapshots are intentionally not persisted.

The reward journal key includes host UID, refresh ID, slot, camp incarnation,
original Group/config identity, claimant UID and durable unpaid-attempt counter.
Explicitly unpaid attempts receive a fresh counter. A completed receipt from
before restart closes the claim without paying or granting rewards again.
Ambiguous CONFIRMED/PREPARED receipts still require existing journal inspection;
there is no automatic replay of uncertain payments.

## Event ordering

The native camp's GROUP_LOAD, GROUP_REFRESH, monster-death and reward callbacks
share a per-Group serial event queue on the existing script executor. Distinct
Groups may progress independently. This prevents cross-thread reordering of
dependent Lua stage events without changing unrelated scripts.
