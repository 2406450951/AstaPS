package emu.grasscutter.game.world;

import static org.junit.jupiter.api.Assertions.assertEquals;

import emu.grasscutter.ServerResourceFixture;
import emu.grasscutter.data.excels.scene.SceneData;
import emu.grasscutter.game.systems.TestPlayers;
import emu.grasscutter.utils.JsonUtils;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

@ExtendWith(ServerResourceFixture.class)
class SceneTimeTest {
    private static Scene scene(AtomicLong nanos, int uid) {
        var player = new TestPlayers.TestPlayer(TestPlayers.server(), uid);
        return new Scene(new World(player),
                JsonUtils.decode("{\"id\":3}", SceneData.class), nanos::get);
    }

    @Test
    void sceneTimeAdvancesWhileWorldClockIsLockedOrChanged() {
        var nanos = new AtomicLong();
        var scene = scene(nanos, 77871);
        assertEquals(0, scene.getSceneTime());

        nanos.addAndGet(1_500_000_000L);
        assertEquals(1500, scene.getSceneTime());

        scene.getWorld().lockTime(true);
        nanos.addAndGet(2_000_000_000L);
        assertEquals(3500, scene.getSceneTime());

        scene.getWorld().changeTime(987654L);
        assertEquals(3500, scene.getSceneTime());
    }

    @Test
    void scenePauseFreezesTimeAndResumesWithoutCatchUp() {
        var nanos = new AtomicLong();
        var scene = scene(nanos, 77872);
        nanos.addAndGet(1_000_000_000L);
        assertEquals(1000, scene.getSceneTime());
        scene.setPaused(true);
        nanos.addAndGet(30_000_000_000L);
        assertEquals(1000, scene.getSceneTime());
        scene.setPaused(false);
        nanos.addAndGet(250_000_000L);
        assertEquals(1250, scene.getSceneTime());
    }
}
