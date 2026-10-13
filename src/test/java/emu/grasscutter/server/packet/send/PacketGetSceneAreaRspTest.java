package emu.grasscutter.server.packet.send;

import static org.junit.jupiter.api.Assertions.*;

import emu.grasscutter.game.player.Player;
import emu.grasscutter.net.proto.GetSceneAreaRspOuterClass.GetSceneAreaRsp;
import java.util.Set;
import org.junit.jupiter.api.Test;

final class PacketGetSceneAreaRspTest {
    @Test
    void aFreshPlayerKeepsMapAreasLocked() throws Exception {
        var player = new Player();
        var response = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 3).getData());

        assertEquals(3, response.getSceneId());
        assertTrue(response.getAreaIdListList().isEmpty());
        assertTrue(player.getUnlockedSceneAreas(3).isEmpty());
    }

    @Test
    void onlyUnlockedAreasOfTheRequestedSceneAreReturned() throws Exception {
        var player = new Player();
        player.getUnlockedSceneAreas(3).addAll(Set.of(1, 3, 7));
        player.getUnlockedSceneAreas(4).add(25);

        var mondstadt = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 3).getData());
        var otherScene = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 4).getData());

        assertEquals(Set.of(1, 3, 7), Set.copyOf(mondstadt.getAreaIdListList()));
        assertEquals(Set.of(25), Set.copyOf(otherScene.getAreaIdListList()));
        assertEquals(Set.of(1, 3, 7), player.getUnlockedSceneAreas(3));
        assertEquals(Set.of(25), player.getUnlockedSceneAreas(4));
    }
}
