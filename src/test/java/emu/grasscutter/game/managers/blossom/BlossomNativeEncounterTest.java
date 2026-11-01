package emu.grasscutter.game.managers.blossom;

import static org.junit.jupiter.api.Assertions.*;
import emu.grasscutter.game.managers.blossom.BlossomGraph.*;
import emu.grasscutter.game.managers.blossom.BlossomManager.*;
import emu.grasscutter.scripts.data.SceneGadget;
import emu.grasscutter.scripts.serializer.LuaSerializer;
import java.util.List;
import java.util.Set;
import org.junit.jupiter.api.Test;
import org.luaj.vm2.*;

class BlossomNativeEncounterTest {
    private static BlossomActivity encounter() {
        var graph = new CityGraph(1, List.of(), Set.of(1, 3));
        var camp = new CampNode(101001001, graph);
        camp.blossom = new Blossom(Blossom.Type.REVELATION, 0);
        return new BlossomActivity(null, camp);
    }

    private static SceneGadget reward(boolean enabled) {
        var lua = new LuaTable();
        lua.set("config_id", LuaValue.valueOf(1006));
        lua.set("is_blossom_chest", LuaValue.valueOf(enabled));
        return new LuaSerializer().toObject(SceneGadget.class, lua);
    }

    @Test
    void nativeLuaCompletionMakesRewardClaimable() {
        var activity = encounter();
        assertEquals(1, activity.scheduleState());
        activity.beginNative(2);
        assertEquals(2, activity.scheduleState());
        assertFalse(activity.addNativeProgress());
        assertTrue(activity.addNativeProgress());
        assertFalse(activity.addNativeProgress());
        var marker = reward(true);
        assertTrue(marker.is_blossom_chest);
        assertTrue(activity.completeFromLua(marker));
        assertEquals(3, activity.scheduleState());
        assertTrue(activity.rewardReady());
        assertFalse(activity.completeFromLua(marker));
    }

    @Test
    void unmarkedLuaGadgetCannotCompleteEncounter() {
        var activity = encounter();
        activity.beginNative(1);
        assertTrue(activity.addNativeProgress());
        assertFalse(activity.completeFromLua(reward(false)));
        assertEquals(2, activity.scheduleState());
        assertFalse(activity.rewardReady());
    }
}