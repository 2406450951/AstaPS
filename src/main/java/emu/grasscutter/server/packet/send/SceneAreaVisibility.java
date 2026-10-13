package emu.grasscutter.server.packet.send;

import static emu.grasscutter.config.Configuration.GAME_OPTIONS;

import emu.grasscutter.game.player.Player;
import java.util.Collection;
import java.util.List;
import java.util.stream.IntStream;

/** Scene area IDs displayed to the client, independent of persisted exploration progress. */
final class SceneAreaVisibility {
    private static final List<Integer> ALL_AREAS =
            IntStream.rangeClosed(1, 1000).boxed().toList();

    private SceneAreaVisibility() {}

    static Collection<Integer> forPlayer(Player player, int sceneId) {
        if (!GAME_OPTIONS.mapFogEnabled) return ALL_AREAS;

        var areas = player.getUnlockedSceneAreas(sceneId);
        // Keep the starting Mondstadt area visible even before its first scene-area snapshot.
        return areas.isEmpty() && sceneId == 3 ? List.of(1) : areas;
    }
}
