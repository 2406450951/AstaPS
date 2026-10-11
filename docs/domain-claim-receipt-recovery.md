# Domain reward claim receipts (7.1)

This branch records resin claim attempts with `RewardGrantJournal`.
It does **not** introduce an atomic transaction across payment, inventory and
receipt collections.

The journal identity is `DOMAIN : UID : dungeonId.runUuid`. A
`DungeonManager` creates its UUID at construction and rotates it in
`restartDungeon()`. It does not depend on a client-supplied interaction ID
or the dungeon ID alone. Two independent successful challenges cannot
accidentally share a receipt.

The claim is synchronized on `DungeonManager` and inventory. It:

1. Validates the reward source, materializes the full batch and preflights bag
   capacity and payment availability.
2. Creates a `PREPARED` receipt before payment.
3. Consumes resin or the selected alternate resource.
4. Marks the player rewarded for the current run and advances the journal to
   `CONFIRMED`.
5. Adds the items.
6. Marks the receipt `COMPLETED` after successful delivery.

The check in step 1 is a preflight: if funds change before step 3, the
receipt may remain `PREPARED` even without payment. Conversely, a crash may
occur after an irreversible operation but before its next journal write.
Do not interpret the stage as conclusive proof of a charge.

## Manual reconciliation

Run this query against the game MongoDB database:

```javascript
db.reward_grant_receipts.find(
  { source: "DOMAIN", stage: { $in: ["PREPARED", "CONFIRMED"] } },
  { _id: 1, uid: 1, operationId: 1, stage: 1, rewards: 1, createdAt: 1 }
).sort({ createdAt: 1 })
```

Inspect the player's inventory, resin/material/primogem balances, claim
logs and dungeon identity before making corrections. Receipt reward arrays
contain intended item quantities, not an automatic replay instruction.
Some partial grants may have persisted; never blindly refund or duplicate
the listed rewards. Clearing a receipt is not a substitute for inspection.

The in-memory `rewardedPlayers` guard still prevents repeated claims
within one dungeon run. `restartDungeon()` creates a new run identity.
Repeated entry and crash recovery across restarts still require client
testing; a process crash does not resume the prior DungeonManager.
Native 7.1 drop rates are unaffected by this change.

A clean `payCost() == false` now marks its attempt `ABORTED` and rotates
the run's claim token. This permits another attempt without reopening the
same receipt. An exception or unknown database result must **not** be
aborted. Expected `debits` are audit data, not verified charges.

## Reward source validation (reviewed in PR #120)

An existing native drop root is not sufficient evidence that its loot belongs to
this dungeon. The server traverses all reachable positive-weight leaves and
compares the appropriate artifact/material families with a validated compatibility
pool (or with preview metadata when no such pool exists). An old set under a
reused root, such as legacy artifact families instead of 4706's 475/485 families,
invalidates the native source. A structurally and semantically validated
`DungeonDrop.json` pool is then used if present. Some other proxies still lack
independent drop-rate verification; successful validation does not prove exact
official probabilities.

Reward previews **never** produce payable items. When neither source passes
validation, the claim is rejected before creating a receipt or deducting resin;
the log records the dungeon ID and rejected sources. Test coverage includes
the known 4706 mismatch and the existing malformed upstream proxy pools.

The scene restart entry point and the grant commit share the DungeonManager
monitor. The old manager's claim marker and run token are cleared or rotated
under that lock; stale requests cannot pay after a scene switch. Client tests
are still required for replay and scene lifecycle behavior.
