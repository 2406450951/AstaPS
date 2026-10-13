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

**Existing saves:** Earlier servers may have stored every area ID as unlocked (1–1000) when answering map requests. Turning fog on cannot infer or reverse that old unlock history. Use a new character to validate fresh map fog; this change intentionally does not rewrite existing character records.
