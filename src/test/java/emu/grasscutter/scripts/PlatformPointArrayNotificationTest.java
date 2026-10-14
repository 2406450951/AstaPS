package emu.grasscutter.scripts;

import static org.junit.jupiter.api.Assertions.*;

import emu.grasscutter.game.entity.EntityGadget;
import emu.grasscutter.game.entity.EntityMonster;
import emu.grasscutter.game.entity.GameEntity;
import emu.grasscutter.game.entity.gadget.platform.PointArrayRoute;
import emu.grasscutter.game.world.Position;
import emu.grasscutter.game.world.Scene;
import emu.grasscutter.net.packet.BasePacket;
import emu.grasscutter.net.packet.PacketOpcodes;
import emu.grasscutter.net.proto.PlatformChangeRouteNotifyOuterClass;
import emu.grasscutter.net.proto.SceneEntityAppearNotifyOuterClass;
import emu.grasscutter.net.proto.SceneEntityDisappearNotifyOuterClass;
import emu.grasscutter.net.proto.SceneEntityInfoOuterClass;
import emu.grasscutter.net.proto.SceneGadgetInfoOuterClass;
import emu.grasscutter.net.proto.VisionTypeOuterClass.VisionType;
import emu.grasscutter.scripts.data.SceneGadget;
import emu.grasscutter.scripts.data.SceneGroup;
import java.lang.reflect.Field;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import javax.tools.ToolProvider;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.luaj.vm2.LuaTable;
import sun.misc.Unsafe;

class PlatformPointArrayNotificationTest {
    @TempDir Path temporary;

    // Bypass constructors that would boot scene/server services. No player, DB or transport is used.
    private static <T> T empty(Class<T> type) throws Exception {
        Field field = Unsafe.class.getDeclaredField("theUnsafe");
        field.setAccessible(true);
        return type.cast(((Unsafe) field.get(null)).allocateInstance(type));
    }

    private static void field(Object object, String name, Object value) throws Exception {
        for (Class<?> type = object.getClass(); type != null; type = type.getSuperclass()) {
            try {
                Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                field.set(object, value);
                return;
            } catch (NoSuchFieldException ignored) {
                // Try the entity's superclass.
            }
        }
        throw new NoSuchFieldException(name);
    }

    static class TestScene extends Scene {
        GameEntity entity;
        List<BasePacket> attempted;
        List<BasePacket> deliverable;

        TestScene() {
            super(null, null);
        }

        @Override
        public int getId() {
            return 3;
        }

        @Override
        public int getSceneTime() {
            return 1000;
        }

        @Override
        public GameEntity getEntityByConfigId(int config, int group) {
            return config == 174002 && group == 133003174 ? entity : null;
        }
        @Override
        public void broadcastPacket(BasePacket packet) {
            attempted.add(packet);
            // Model GameSession.send's nonpositive-opcode discard, without invoking transport.
            if (packet.getOpcode() > 0) deliverable.add(packet);
        }
        void clear() {
            attempted.clear();
            deliverable.clear();
        }
    }

    static class TestManager extends SceneScriptManager {
        TestScene scene;
        TestManager() {
            super(null);
        }

        @Override
        public Scene getScene() {
            return scene;
        }
    }

    static class TestGadget extends EntityGadget {
        TestGadget() {
            super(null, 0, null);
        }

        @Override
        public SceneEntityInfoOuterClass.SceneEntityInfo toProto() {
            return SceneEntityInfoOuterClass.SceneEntityInfo.newBuilder().setEntityId(getId())
                    .setGadget(SceneGadgetInfoOuterClass.SceneGadgetInfo.newBuilder()
                            .setPlatform(getPlatformInfo())).build();
        }
    }

    @ParameterizedTest
    @ValueSource(ints = {0, -1, 60000})
    void changedArrayPublishesWithoutChangingExistingStateContracts(int opcode) throws Exception {
        assertEquals(0, PacketOpcodes.PlatformChangeRouteNotify,
                "The real unknown opcode must remain zero; synthetic IDs are not protocol mappings");
        try (URLClassLoader fixture = opcode == 0 ? null : opcodeFixture(opcode)) {
            Object lib = fixture == null ? new ScriptLib()
                    : fixture.loadClass(ScriptLib.class.getName()).getConstructor().newInstance();
            TestScene scene = empty(TestScene.class);
            scene.attempted = new ArrayList<>();
            scene.deliverable = new ArrayList<>();
            TestManager manager = empty(TestManager.class);
            manager.scene = scene;
            TestGadget gadget = empty(TestGadget.class);
            field(gadget, "scene", scene);
            field(gadget, "position", new Position(1, 2, 3));
            gadget.setId(0x04000001);
            gadget.setConfigId(174002);
            gadget.setGroupId(133003174);
            scene.entity = gadget;
            SceneGadget meta = new SceneGadget();
            meta.rot = new Position(10, 20, 30);
            meta.start_route = true;
            meta.is_use_point_array = true;
            gadget.setMetaGadget(meta);
            PointArrayRoute route = new PointArrayRoute(meta);
            route.setPointArrayId(40);
            route.setCurrentPoint(9);
            route.setStartSceneTime(777);
            route.setStopSceneTime(888);
            gadget.setRouteConfig(route);
            byte[] before = gadget.getPlatformInfo().build().toByteArray();
            lib.getClass().getMethod("setSceneScriptManager", SceneScriptManager.class).invoke(lib, manager);
            SceneGroup group = new SceneGroup();
            group.id = 133003174;
            lib.getClass().getMethod("setCurrentGroup", SceneGroup.class).invoke(lib, group);
            int[] points = {3, 8, 12};
            int[] snapshot = points.clone();
            LuaTable options = new LuaTable();
            options.set("route_type", 3);
            options.set("sentinel", 73);

            assertEquals(1, setArray(lib, 999, 41, points, options));
            assertTrue(scene.attempted.isEmpty());
            assertEquals(40, route.getPointArrayId());
            scene.entity = empty(EntityMonster.class);
            assertEquals(2, setArray(lib, 174002, 41, points, options));
            assertTrue(scene.attempted.isEmpty());
            scene.entity = gadget;
            assertEquals(-1, setArray(lib, 174002, 40, points, options));
            assertTrue(scene.attempted.isEmpty(), "Identical array IDs must not produce false-positive notifications");
            assertSame(route, gadget.getRouteConfig());
            assertArrayEquals(before, gadget.getPlatformInfo().build().toByteArray());

            assertEquals(-1, setArray(lib, 174002, 41, points, options));
            assertSame(route, gadget.getRouteConfig());
            assertEquals(41, route.getPointArrayId());
            assertEquals(9, route.getCurrentPoint());
            assertTrue(route.isStarted() && route.isActive());
            assertSame(meta.rot, route.getStartRot());
            assertEquals(777, route.getStartSceneTime());
            assertEquals(888, route.getStopSceneTime());
            assertArrayEquals(snapshot, points);
            assertEquals(2, options.keys().length);
            assertEquals(3, options.get("route_type").toint());
            assertEquals(73, options.get("sentinel").toint());
            assertArrayEquals(before, gadget.getPlatformInfo().build().toByteArray(),
                    "This fix must not invent point-array serializer fields");
            assertNotification(scene, gadget, opcode);

            scene.clear();
            assertEquals(-1, setArray(lib, 174002, 41, points, options));
            assertTrue(scene.attempted.isEmpty(), "Repeated calls after transition must remain silent");
            assertEquals(-1, setArray(lib, 174002, 41, new int[] {99}, new LuaTable()));
            assertTrue(scene.attempted.isEmpty(), "Existing same-ID behavior ignores different point lists/options");
            gadget.setRouteConfig(null);
            assertEquals(-1, setArray(lib, 174002, 42, points, options));
            PointArrayRoute converted = assertInstanceOf(PointArrayRoute.class, gadget.getRouteConfig());
            assertEquals(42, converted.getPointArrayId());
            assertTrue(converted.isStarted() && converted.isActive());
            assertSame(meta.rot, converted.getStartRot());
            assertNotification(scene, gadget, opcode);
        }
    }

    private static void assertNotification(TestScene scene, TestGadget gadget, int opcode) throws Exception {
        List<Integer> expected = opcode > 0 ? List.of(opcode)
                : List.of(PacketOpcodes.SceneEntityDisappearNotify, PacketOpcodes.SceneEntityAppearNotify);
        assertEquals(expected, scene.attempted.stream().map(BasePacket::getOpcode).toList(),
                "Changed point-array notification must survive the nonpositive-opcode discard boundary");
        assertEquals(expected, scene.deliverable.stream().map(BasePacket::getOpcode).toList());
        if (opcode > 0) {
            var change = PlatformChangeRouteNotifyOuterClass.PlatformChangeRouteNotify
                    .parseFrom(scene.deliverable.get(0).getData());
            assertEquals(gadget.getId(), change.getEntityId());
            assertEquals(1000, change.getSceneTime());
            assertArrayEquals(gadget.getPlatformInfo().build().toByteArray(), change.getPlatform().toByteArray());
        } else {
            var disappear = SceneEntityDisappearNotifyOuterClass.SceneEntityDisappearNotify
                    .parseFrom(scene.deliverable.get(0).getData());
            var appear = SceneEntityAppearNotifyOuterClass.SceneEntityAppearNotify
                    .parseFrom(scene.deliverable.get(1).getData());
            assertEquals(VisionType.VisionType_VISION_REFRESH, disappear.getDisappearType());
            assertEquals(List.of(gadget.getId()), disappear.getEntityListList());
            assertEquals(VisionType.VisionType_VISION_REFRESH, appear.getAppearType());
            assertEquals(0, appear.getParam());
            assertEquals(1, appear.getEntityListCount());
            assertEquals(gadget.getId(), appear.getEntityList(0).getEntityId());
            assertArrayEquals(gadget.getPlatformInfo().build().toByteArray(),
                    appear.getEntityList(0).getGadget().getPlatform().toByteArray());
        }
    }

    private static int setArray(Object lib, int config, int array, int[] points, LuaTable options) throws Exception {
        return (int) lib.getClass().getMethod("SetPlatformPointArray", int.class, int.class, int[].class, LuaTable.class)
                .invoke(lib, config, array, points, options);
    }

    private Path inferredLocalsFixture(Path original) throws Exception {
        Path fixture = temporary.resolve(original.getFileName());
        Files.writeString(fixture, Files.readString(original).replace("import lombok.val;", "")
                .replaceAll("\\bval\\b", "var"));
        return fixture;
    }

    // Constants are inlined by javac. Compile the real method and packet against an isolated
    // opcode fixture rather than mutate production constants or introduce a production test hook.
    private URLClassLoader opcodeFixture(int opcode) throws Exception {
        Path source = Path.of("src/main/java");
        Path constants = source.resolve("emu/grasscutter/net/packet/PacketOpcodes.java");
        String original = Files.readString(constants);
        String declaration = "public static final int PlatformChangeRouteNotify = 0;";
        assertTrue(original.contains(declaration));
        Path fixtureSource = temporary.resolve("PacketOpcodes.java");
        Files.writeString(fixtureSource, original.replace(declaration,
                "public static final int PlatformChangeRouteNotify = " + opcode + ";"));
        var classpath = new LinkedHashSet<String>();
        classpath.addAll(Arrays.asList(System.getProperty("java.class.path").split(java.io.File.pathSeparator)));
        for (ClassLoader loader = getClass().getClassLoader(); loader != null; loader = loader.getParent()) {
            if (loader instanceof URLClassLoader urls) {
                for (URL url : urls.getURLs()) classpath.add(Path.of(url.toURI()).toString());
            }
        }
        // These two classes use Lombok only for local-variable inference. Java 21's var
        // is equivalent here and avoids relying on an annotation processor at test runtime.
        Path scriptSource = inferredLocalsFixture(source.resolve("emu/grasscutter/scripts/ScriptLib.java"));
        Path packetSource = inferredLocalsFixture(source.resolve("emu/grasscutter/server/packet/send/PacketPlatformChangeRouteNotify.java"));
        String cp = String.join(java.io.File.pathSeparator, classpath);
        assertEquals(0, ToolProvider.getSystemJavaCompiler().run(null, null, null,
                "-proc:none", "-classpath", cp, "-d", temporary.toString(),
                fixtureSource.toString(), scriptSource.toString(), packetSource.toString()));
        List<String> isolated = List.of(ScriptLib.class.getName(), PacketOpcodes.class.getName(),
                "emu.grasscutter.server.packet.send.PacketPlatformChangeRouteNotify");
        return new URLClassLoader(new URL[] {temporary.toUri().toURL()}, getClass().getClassLoader()) {
            @Override
            protected synchronized Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
                if (!isolated.contains(name)) return super.loadClass(name, resolve);
                Class<?> loaded = findLoadedClass(name);
                if (loaded == null) loaded = findClass(name);
                if (resolve) resolveClass(loaded);
                return loaded;
            }
        };
    }
}
