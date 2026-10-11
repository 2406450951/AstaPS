package emu.grasscutter.game.player;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.data.GameData;
import emu.grasscutter.data.binout.AbilityData;
import emu.grasscutter.data.binout.AbilityMixinData;
import emu.grasscutter.game.world.Position;
import emu.grasscutter.game.world.WorldAreaIndex;
import emu.grasscutter.net.proto.AbilityInvokeEntryOuterClass.AbilityInvokeEntry;
import emu.grasscutter.server.packet.send.PacketServerGlobalValueChangeNotify;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Snezhnaya's Frigid (严寒) meter: the team abilities that run it, where they apply, and the server side
 * of the two values it shows.
 *
 * <p>Officially the Snezhnaya level-1 areas (80100..80500, areaID1 55..59 in scene 3) grant the
 * {@code DynamicAbility_IceCreature_V7_0_Team} ability group, and the Frigid team abilities come with
 * it. The rest of that group drives Ice Creatures, which need protocol this server does not have yet,
 * so only the Frigid part is granted here - the same set as the official
 * {@code DynamicAbility_SnezhnayaFrigid_Only_V7_0_Team} group.
 *
 * <p>Two server global values drive it. {@code SGV_SnezhnayaEnergy} is the Kresnik energy - the warmth
 * the team spends in cold areas - and only once it runs out does {@code SGV_SnezhnayaFrigid} build
 * (0..100, the team loses HP from 99.5 up). The client works out where it is and how cold that is, and
 * reports each tick as an ABILITY_MIXIN_CHANGE_SNEZHNAYA_FRIGID_VAR invoke with an empty payload: the
 * local id alone names the change-Frigid mixin (KOANNHKDPII) whose config says what to do. A live
 * capture showed {@code SnezhnayaFrigid_FrigidDoTick_Lv1} (speed 1) once a second in a low-cold area and
 * {@code SnezhnayaFrigid_Basic_Team_Clear} (speed -999). The server applies those and sends the values
 * back.
 */
public final class SnezhnayaFrigid {
    public static final String FRIGID_GV = "SGV_SnezhnayaFrigid";
    public static final String ENERGY_GV = "SGV_SnezhnayaEnergy";

    /**
     * Full Kresnik energy. No resource states the cap; sending 100 showed the warmth bar at 20%, so the
     * client draws it out of 500.
     */
    public static final float MAX_ENERGY = 500f;

    public static final float MAX_FRIGID = 100f;

    /** How long a tick's priority level holds off lower levels; ticks arrive about once a second. */
    private static final long PRIORITY_HOLD_MS = 1500;

    /** DynamicAbility_SnezhnayaFrigid_Only_V7_0_Team. */
    static final List<String> TEAM_ABILITIES =
            List.of(
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_Basic",
                    "Snezhnaya_ZDNervePlant_TeamAbility",
                    "Snezhnaya_StarConductor_TeamAbility",
                    "Snezhnaya_PlayerGlasses_Eff");

    /** Every ability carrying change-Frigid mixins, for when an invoke's ability id cannot be resolved. */
    private static final List<String> FRIGID_ABILITIES =
            List.of(
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_Basic",
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_Lv2",
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_Lv3",
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_HighPriority_Lv1",
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_HighPriority_Lv2",
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_HighPriority_TPS",
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_WarmArea");

    private static final int OVERWORLD_SCENE = 3;
    private static final int FIRST_AREA = 55;
    private static final int LAST_AREA = 59;

    /** Per-player warmth and Frigid, for this server session. */
    private static final Map<Integer, State> STATES = new ConcurrentHashMap<>();

    private SnezhnayaFrigid() {}

    /** Whether this position is in the Snezhnaya overworld. */
    public static boolean contains(int sceneId, Position position) {
        if (sceneId != OVERWORLD_SCENE || position == null) return false;
        var index = WorldAreaIndex.forScene(sceneId);
        if (index.isEmpty()) {
            // No outline data: fall back to the bounding box of areas 55..59.
            return position.getX() >= 6174f && position.getX() <= 10982f
                    && position.getZ() >= 5161f && position.getZ() <= 9367f;
        }
        int area = index.level1AreaAt(position.getX(), position.getZ());
        return area >= FIRST_AREA && area <= LAST_AREA;
    }

    public static boolean isIn(Player player) {
        return player.getScene() != null && contains(player.getScene().getId(), player.getPosition());
    }

    /** Whether the Frigid team abilities are currently granted. */
    public static boolean isGranted(Player player) {
        return player.getTeamManager() != null
                && TeamAbilityToggle.isOn(player, TEAM_ABILITIES.get(0));
    }

    /** The player's warmth and Frigid; a player new to Snezhnaya this session starts fully warm. */
    public static State stateOf(Player player) {
        return STATES.computeIfAbsent(player.getUid(), uid -> new State());
    }

    /**
     * Grants or withdraws the abilities for the player's current position without notifying the client,
     * for scene entry: the team control block in the enter-scene packets then already carries them.
     */
    public static void syncForSceneEntry(Player player) {
        if (player.getTeamManager() == null) return;
        boolean in = isIn(player);
        if (in == isGranted(player)) return;
        logChange(player, in, "scene entry");
        TeamAbilityToggle.set(player, TEAM_ABILITIES, in, false);
    }

    /** Applies a seamless border crossing: updates the team abilities and, on entry, resends both values. */
    public static void refresh(Player player) {
        if (player.getTeamManager() == null || player.getSession() == null) return;
        var pos = player.getPosition();
        // A move can briefly report the origin before the real position arrives; that is not a border
        // crossing, and treating it as one withdrew and regranted the abilities within a frame.
        if (pos == null || (pos.getX() == 0f && pos.getY() == 0f && pos.getZ() == 0f)) return;
        boolean in = isIn(player);
        if (in == isGranted(player)) return;

        logChange(player, in, "crossing");
        TeamAbilityToggle.set(player, TEAM_ABILITIES, in, true);
        if (in) sendValues(player, stateOf(player));
    }

    /** Handles an ABILITY_MIXIN_CHANGE_SNEZHNAYA_FRIGID_VAR invoke: one tick of the meter. */
    public static void onInvoke(Player player, AbilityInvokeEntry invoke) {
        var head = invoke.getHead();
        AbilityMixinData mixin = resolveMixin(player, head.getInstancedAbilityId(), head.getLocalId());
        if (mixin == null) {
            Grasscutter.getLogger()
                    .debug(
                            "[SnezhnayaFrigid] no change-Frigid mixin for ability={} localId={}",
                            head.getInstancedAbilityId(),
                            head.getLocalId());
            return;
        }

        var state = stateOf(player);
        boolean changed;
        synchronized (state) {
            changed =
                    state.apply(
                            mixin.speed.get(),
                            mixin.costType,
                            mixin.priority,
                            mixin.frigidOnly,
                            System.currentTimeMillis());
        }
        if (changed) sendValues(player, state);
    }

    private static AbilityMixinData resolveMixin(Player player, int instancedAbilityId, int localId) {
        // The invoke names the ability by its position in the team control block we sent.
        String name = null;
        if (player.getTeamManager() != null) {
            for (var embryo : player.getTeamManager().getAbilityControlBlock().getAbilityEmbryoListList()) {
                if (embryo.getAbilityId() == instancedAbilityId) {
                    name = GameData.getAbilityHashes().get(embryo.getAbilityNameHash());
                    break;
                }
            }
        }
        var mixin = mixinIn(name, localId);
        if (mixin != null) return mixin;
        for (String candidate : FRIGID_ABILITIES) {
            mixin = mixinIn(candidate, localId);
            if (mixin != null) return mixin;
        }
        return null;
    }

    private static AbilityMixinData mixinIn(String abilityName, int localId) {
        if (abilityName == null) return null;
        AbilityData data = GameData.getAbilityData(abilityName);
        if (data == null) return null;
        data.initialize();
        return data.localIdToMixin.get(localId);
    }

    private static void sendValues(Player player, State state) {
        var team = player.getTeamManager() == null ? null : player.getTeamManager().getEntity();
        if (team == null) return;
        float energy, frigid;
        synchronized (state) {
            energy = state.energy;
            frigid = state.frigid;
        }
        player.sendPacket(new PacketServerGlobalValueChangeNotify(team.getId(), ENERGY_GV, energy));
        player.sendPacket(new PacketServerGlobalValueChangeNotify(team.getId(), FRIGID_GV, frigid));
    }

    private static void logChange(Player player, boolean in, String reason) {
        if (!Grasscutter.getLogger().isDebugEnabled()) return;
        var scene = player.getScene();
        var pos = player.getPosition();
        int area =
                scene == null || pos == null
                        ? 0
                        : WorldAreaIndex.forScene(scene.getId()).level1AreaAt(pos.getX(), pos.getZ());
        Grasscutter.getLogger()
                .debug(
                        "[SnezhnayaFrigid] {} Frigid abilities on {}: uid={} scene={} pos={} area={}",
                        in ? "grant" : "withdraw",
                        reason,
                        player.getUid(),
                        scene == null ? 0 : scene.getId(),
                        pos,
                        area);
    }

    /** "Level_3" -> 3; anything else counts as the lowest level, 0. */
    static int priorityLevel(String priority) {
        if (priority == null || !priority.startsWith("Level_")) return 0;
        try {
            return Integer.parseInt(priority.substring("Level_".length()));
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /** One player's warmth and Frigid. */
    public static final class State {
        float energy = MAX_ENERGY;
        float frigid = 0f;
        private int heldPriority = -1;
        private long heldUntil = 0;

        public float getEnergy() {
            return energy;
        }

        public float getFrigid() {
            return frigid;
        }

        /**
         * Applies one tick of a change-Frigid mixin.
         *
         * <ul>
         *   <li>A tick below the priority level currently held is dropped, so a warm area's or a
         *       high-priority zone's tick wins over the base cold tick reported alongside it.
         *   <li>"Set" sets the Frigid meter when the mixin is flagged {@code frigidOnly}, the warmth
         *       otherwise; "Add" adds to the warmth (the Kresnik-crystal top-up).
         *   <li>Otherwise a positive speed spends warmth first and builds Frigid with what warmth could not
         *       cover; a negative speed, or one flagged {@code frigidOnly}, moves the Frigid meter
         *       directly (warm areas, the -999 clears).
         * </ul>
         *
         * @return whether either value changed
         */
        boolean apply(float speed, String costType, String priority, boolean frigidOnly, long nowMs) {
            int level = priorityLevel(priority);
            if (nowMs < this.heldUntil && level < this.heldPriority) return false;
            this.heldPriority = level;
            this.heldUntil = nowMs + PRIORITY_HOLD_MS;

            float oldEnergy = this.energy, oldFrigid = this.frigid;
            if ("Set".equals(costType)) {
                if (frigidOnly) this.frigid = clamp(speed, MAX_FRIGID);
                else this.energy = clamp(speed, MAX_ENERGY);
            } else if ("Add".equals(costType)) {
                this.energy = clamp(this.energy + speed, MAX_ENERGY);
            } else if (frigidOnly || speed < 0) {
                this.frigid = clamp(this.frigid + speed, MAX_FRIGID);
            } else {
                float spent = Math.min(this.energy, speed);
                this.energy -= spent;
                this.frigid = clamp(this.frigid + (speed - spent), MAX_FRIGID);
            }
            return this.energy != oldEnergy || this.frigid != oldFrigid;
        }

        private static float clamp(float value, float max) {
            return Math.max(0f, Math.min(max, value));
        }
    }
}
