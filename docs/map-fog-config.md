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

- `true` (default): only areas the player has unlocked are visible. Activating a Statue of the Seven still unlocks its region through the existing quest/scene-area logic.
- `false`: show every map area in the client without unlocking teleport points, granting rewards, or modifying the player's stored exploration progress.
- Changing the setting requires a server restart and a fresh scene-area response (relogin is sufficient).
- Configuration version 18 writes the new field into existing `config.json` files on upgrade. A missing field also defaults to `true`.

The visibility switch is applied consistently to `GetSceneAreaRsp` and `GetScenePointRsp`. Region-access restrictions and world-scene allow-lists are separate and unchanged.

**Reversible switching:** `false` only changes area IDs sent in the client response. It does not persist those IDs. Switching back to `true` restores the explored areas on the next login or area snapshot; this can be repeated.

**Existing saves:** Older server versions persisted all area IDs 1–1000 on map requests. When `mapFogEnabled` is `true` and the stored area list contains that entire range, the server reconstructs visible regions from the player's unlocked Statue of the Seven points (including their child areas) without altering saved data. Normal, non-polluted saves keep their area flags, including quest-specific unlocks. The starting Mondstadt region remains visible.

A fully unlocked area list cannot tell us whether it came from the old bug or an intentional bulk-unlock command. The recovery path treats it as legacy pollution for map visibility. If an old server also bulk-unlocked all scene **points**, the original individual statue activation history is not recoverable from those point flags alone.
