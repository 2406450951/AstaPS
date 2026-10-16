# Nearby daily-commission encounters

Commission groups should not be simulated kilometres away from a player. Remote enemies can receive movement updates and die without legitimate nearby combat; rejecting a distant kill does not prevent the encounter from being lost.

## Lifecycle

The world host owns the board. Scene ticks resolve each unfinished task's authored `centerPosition` through the loaded scene-3 dummy points and use its `enterDistance`/`exitDistance` as three-dimensional server-policy boundaries. Any fully loaded visitor in the same scene/world can retain the host's encounter. Missing or invalid resource coordinates retain the board and fail activation closed rather than substituting an origin or rerolling tasks.

Only groups activated by this lifecycle are owned. Existing quest/challenge or unrelated preloaded groups are not adopted or removed. Leaving the range removes the owned population nonlethally: it does not call monster death handlers, increment tasks or grant rewards. Delayed creation and wave-repair callbacks cannot recreate a dormant owned encounter. The existing minimum-rank and nearby-death guards remain in place.

A bounded, versioned snapshot on the existing group instance records surviving entity configurations, poses, monster HP, gadget states, active triggers/invocation counts and regions. Resume restores that population without replaying initial-suite/group-load scripts, including later-wave enemies created directly by Lua. Existing scene/world group saves persist the snapshot; proximity adds no board/quota/player save or cross-document crash-atomicity guarantee.

Explicit completion/reset invalidates both active and cached snapshots before unregister can queue persistence. A refused or throwing unregister restores the previous snapshot and ownership. Ordinary proximity suspension retains its snapshot instead. Zero-HP dying monsters are excluded.

This is not complete serialization of native AI, ability modifiers or arbitrary script timers, nor a claim about exact proprietary streaming boundaries. Other timer-driven encounter families require qualification.

## Validation

`DailyTaskGroupLifecycleTest` exercises real manager/scene loader and unloader paths with in-memory entity-construction, inventory, persistence and transport boundaries. It covers distance hysteresis, vertical distance, delayed callbacks, later waves, trigger/HP preservation, malformed snapshots, new-day isolation, world/guest/quest ownership and one-time completion rewards. `DailyTaskUnregisterStateTest` covers snapshot-before-save ordering, aliasing and failure rollback. Eligibility regressions preserve upstream's Adventure Rank 12 guard and explicit stored Random preference.

Local client checks include distant no-input noncompletion, legitimate protected combat, cold partial-wave restoration, and a completed task's single payment/cold persistence. These checks used a composed local deployment with other safeguards; they are not an upstream clean-checkout client acceptance claim. Tests on this PR use synthetic in-memory state and require no accounts, database, resource pack or client files.

The passive cooldown lookup fix is a companion change: cooldown bookkeeping must not reload a group just unloaded by completion. Region-selector semantics and natural quest eligibility are separate, unresolved concerns.
