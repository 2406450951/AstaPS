package emu.grasscutter.server.packet.send;

import static emu.grasscutter.config.Configuration.GAME_OPTIONS;

import emu.grasscutter.data.GameData;
import emu.grasscutter.game.managers.StatueTalkQuests;
import emu.grasscutter.game.player.Player;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.stream.IntStream;

/** Map visibility is derived for the client; it must not mutate saved exploration progress. */
final class SceneAreaVisibility {
    private static final List<Integer> ALL_AREAS =
            IntStream.rangeClosed(1, 1000).boxed().toList();

    private SceneAreaVisibility() {}

    static Collection<Integer> forPlayer(Player player, int sceneId) {
        if (!GAME_OPTIONS.mapFogEnabled) return ALL_AREAS;

        var saved = player.getUnlockedSceneAreas(sceneId);
        // Old GetSceneAreaRsp added 1..1000 to every requested scene and persisted the result.
        // Recover visibility from actual statue unlocks rather than displaying that legacy state.
        if (saved.containsAll(ALL_AREAS)) return fromUnlockedStatues(player, sceneId);
        return saved.isEmpty() && sceneId == 3 ? List.of(1) : saved;
    }

    private static Collection<Integer> fromUnlockedStatues(Player player, int sceneId) {
        var revealed = new LinkedHashSet<Integer>();
        if (sceneId == 3) revealed.add(1); // Starting Mondstadt region.

        for (int pointId : player.getUnlockedScenePoints(sceneId)) {
            if (player.isScenePointForceLocked(sceneId, pointId)) continue;
            var point = GameData.getScenePointEntryById(sceneId, pointId);
            if (point == null || point.getPointData() == null) continue;
            var data = point.getPointData();
            if (!StatueTalkQuests.isStatuePoint(data) || data.getAreaId() <= 0) continue;

            int areaId = data.getAreaId();
            revealed.add(areaId);
            for (var area : GameData.getWorldAreaDataMap().values()) {
                if (area.getSceneId() != sceneId) continue;
                if (area.getParentArea() == areaId && area.getChildArea() > 0) {
                    revealed.add(area.getChildArea());
                }
                if (area.getChildArea() == areaId && area.getParentArea() > 0) {
                    revealed.add(area.getParentArea());
                }
            }
        }
        return revealed;
    }
}
