# Blossom Lua resource coverage audit (Global 7.1 resource snapshot)

Snapshot: 2026-10-24. AstaPS implementation head before this audit: `b1e486de4f5ca1ec5eaf8b1fa30e57828ec30388`. Resource source: `RinoPaw/AstaPS-Resource@c0213668889ed05cbeb86660f424c3daf18bfacd`, files `ExcelBinOutput/BlossomGroupsExcelConfigData.json` and `Scripts/Scene/3/scene3_group<groupId>.lua`.

This is a **resource coverage audit**, not independent original-client verification. The resource repository's Lua files cannot be promoted to authoritative Global 7.1 game data without EXE/metadata evidence from Genshin-Reverse.

## Scope and categories

The resource contains 673 BlossomGroups rows. Exactly 262 have `refreshTypeVec = [1,3]`, and each of these has one `newGroupVec` target.

* **Lua exists**: the target group has a file in the resource's Scene scripts.
* **Gold/blue source**: that Lua actually declares Gadget `70360056` / `70360057`.
* **Native event structure**: a Lua with both blossom Gadgets, monster entries, reward Gadget `70210106`, `EVENT_SELECT_OPTION`, `EVENT_BLOSSOM_PROGRESS_FINISH`, and a `CreateBlossomChestByGroupId` call. This is a static structural check; suite registration and gameplay are not verified.
* **Incomplete encounter**: contains a matching blossom Gadget but lacks native select/progress triggers. The native-only server excludes it.

| City ID | Candidate rows | Lua exists | Gold Gadget | Blue Gadget | Native structure |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 35 | 32 | 32 | 32 | 32 |
| 2 | 64 | 56 | 56 | 56 | 56 |
| 3 | 22 | 22 | 22 | 22 | 0 |
| 4 | 45 | 36 | 33 | 33 | 0 |
| 5 | 27 | 2 | 0 | 2 | 0 |
| 6 | 41 | 7 | 3 | 4 | 0 |
| 7 | 19 | 5 | 2 | 3 | 0 |
| 8 | 9 | 1 | 0 | 1 | 0 |
| **Total** | **262** | **161** | **148** | **153** | **88** |

The 161 Lua files partition into **88 with native event structure**, **55 with both Gadgets but no native events**, **15 with one blossom Gadget**, and **3 without either blossom Gadget**. The other 101 candidate rows have no matching Lua file. Thus **158** rows have at least one matching source-backed blossom Gadget. None of the non-native groups in this snapshot defines the relevant select/progress triggers.

## Notable incomplete groups

* City 4: `133314204` has two monsters but no Gadgets; `133314313` and `133314317` have no monsters, Gadgets, or triggers. All three Lua files exist, but neither blossom kind can resolve them.
* City 5: `133403311` and `133412176` each contain only the blue blossom Gadget. No gold source-backed Gadget exists in this city's 27 candidates.
* City 6: gold-only groups `133501642`, `133508423`, `133520343`; blue-only groups `133502486`, `133501732`, `133504636`, `133520340`.
* City 7: gold-only `133601247`, `133611487`; blue-only `133601253`, `133601147`, `133601150`.
* City 8: only `133708033` is present and it contains a blue blossom Gadget.
* City 3: all 22 Lua files contain both blossom Gadgets, monsters, and the `70210106` chest Gadget, but no trigger declarations. City 4's 33 gadget-bearing files have the same missing-trigger limitation.

At level 60, ignoring area unlock requirements, following `nextCampIdVec` from each kind's current `BlossomGraph.CityGraph.initialCamps` reaches every source-backed candidate for that city and kind. This statement covers graph reachability only, not spawning or gameplay.

## Risk to test before an upstream PR

`SceneScriptManager.ensureBlossomGroup` calls `refreshGroup` with the authored initial suite. For incomplete Lua where the initial suite contains monsters (notably cities 3 and 4), this can spawn those monsters before worktop interaction. Native-only selection now rejects incomplete groups before they are loaded. There is no generated random combat fallback.

Next work: validate the original Lua event route in game and recover missing original definitions through Genshin-Reverse EXE/metadata research. Do not generate guessed missing gadgets, triggers, monsters, or coordinates.
