# Runtime configuration layout

The server operator owns **config/**. The project and resource pack must not
silently overwrite local policy. `config.json` remains at the root for compatibility:
global network, database, service options, and existing global tuning still live there.

Only one gameplay config currently exists:

```text
AstaPS/
├── config.json                          # Existing global settings
├── config/
│   ├── rewards/
│   │   └── trans-point-rewards.toml     # Implemented
│   ├── drops/
│   │   ├── chests.toml                 # Future, not yet created
│   │   ├── monsters.toml               # Future, not yet created
│   │   └── domains.toml                # Future, not yet created
│   ├── artifacts/
│   │   └── rolls.toml                  # Future, not yet created
│   └── domains/
│       └── schedules.toml              # Future, not yet created
├── data/                                # Project base data; legacy mixed ownership
└── resources/                           # Game asset extracts
```

## Ownership and precedence

- `resources/`: client-extracted point, dungeon, item, quest, and Excel data.
  Native IDs and scene topology may be used to identify a target. They do not
  override an operator's gameplay policy.
- `data/`: versioned project-maintained base datasets, often large JSON tables.
  Existing `ChestDrop.json`, `MonsterDrop.json`, and `DungeonDrop.json`
  remain untouched until their respective systems are redesigned.
- `config/`: server-owner policy written in TOML when concise enough to maintain
  by hand. Large, machine-generated data should not be converted to huge TOML
  arrays merely for consistency.
- Database and existing claimed-state files: runtime player/claim state, never
  treated as configurable reward definitions.

Each subsystem must document exactly one policy authority and a single precedence
scheme. Reject malformed or conflicting operator files; do not silently return to
stale native rewards. Configured values may refer to native IDs, but they are not
overwritten by native asset changes.

## Future subsystem boundaries

- `rewards/trans-point-rewards.toml`: first discovery/activation rewards for
  waypoints, Statues of The Seven and reward-eligible domain entrances.
- `drops/chests.toml`, `drops/monsters.toml`, `drops/domains.toml`:
  loot pool policy, drop probabilities and counts. These do not define
  the first-entry domain reward or domain unlock rules.
- `artifacts/rolls.toml`: generation of artifact main stats, substats,
  initial rolls, and upgrade weights; independent of the dungeon's chance to
  produce an artifact.
- `domains/schedules.toml`: domain availability by day/time and reset-time
  interpretation; independent of domain pool/drop rates. Keep native dungeon
  identity and client-visible schedules consistent with server admission.

Do not introduce a global generic selector engine or shared hot-reload behavior
until another subsystem needs them. Each feature owns validation and tests.

## Deployment and migration

Bundled defaults belong to `src/main/resources/`, while the runtime files under
`config/` are gitignored. First startup generates a missing file. Updates never
overwrite existing operator values. Unknown keys or invalid values cause explicit
load errors, and schema changes must have documented migration tests.

The initial transport rewards implementation originally used root-level
`trans-point-rewards.toml`. Its startup loader now moves that file to
`config/rewards/trans-point-rewards.toml` if the destination is absent.
If both exist, startup reports a conflict instead of choosing one silently.
