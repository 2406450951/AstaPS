package emu.grasscutter.scripts.serializer;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import emu.grasscutter.scripts.data.SceneGadget;
import org.junit.jupiter.api.Test;
import org.luaj.vm2.LuaTable;
import org.luaj.vm2.LuaValue;

class LuaSerializerBooleanFieldTest {
    @Test
    void parsesIsPrefixedBooleanAndRegularBooleanFields() {
        var lua = new LuaTable();
        lua.set("is_blossom_chest", LuaValue.TRUE);
        lua.set("is_use_point_array", LuaValue.TRUE);
        lua.set("start_route", LuaValue.FALSE);

        var gadget = new LuaSerializer().toObject(SceneGadget.class, lua);
        assertTrue(gadget.is_blossom_chest);
        assertTrue(gadget.is_use_point_array);
        assertFalse(gadget.start_route);
    }
}