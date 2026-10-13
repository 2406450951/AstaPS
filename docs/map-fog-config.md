# Map fog configuration (7.1)

Map fog is enabled by default. In `config.json`:

```json
{
  "server": {
    "game": {
      "gameOptions": {
        "mapFogEnabled": true
      }
    }
  }
}
```

- `true` (default): show only the areas recorded as explored for this player. Activating a Statue of the Seven unlocks its region through the existing quest/scene-area logic.
- `false`: show every map area on the client without unlocking teleport points, granting rewards, or modifying the player's stored exploration progress.
- The switch is reversible: alternating `true` and `false` restores the same explored regions when fog is enabled again, as long as the underlying exploration state is unchanged.
- Changes to `config.json` take effect after a server restart and a fresh scene-area response (relogin is sufficient).
- Configuration version 18 writes the new field into existing `config.json` files on upgrade. A missing setting also defaults to `true`.

The setting applies consistently to `GetSceneAreaRsp` and `GetScenePointRsp`. Region-access restrictions and world-scene allow-lists are unchanged.

This feature uses the current player's recorded exploration state as-is. It does not migrate, repair, or attempt to infer progress from old saves.
