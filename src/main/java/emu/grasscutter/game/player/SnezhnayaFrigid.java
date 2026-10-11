package emu.grasscutter.game.player;

import emu.grasscutter.game.world.Position;
import emu.grasscutter.game.world.WorldAreaIndex;
import emu.grasscutter.server.packet.send.PacketServerGlobalValueChangeNotify;
import java.util.List;

/**
 * Snezhnaya's Frigid (严寒) meter: the team abilities that run it, and where they apply.
 *
 * <p>Officially the Snezhnaya level-1 areas (80100..80500, areaID1 55..59 in scene 3) grant the
 * {@code DynamicAbility_IceCreature_V7_0_Team} ability group, and the Frigid team abilities come with
 * it. The rest of that group drives Ice Creatures, which need protocol this server does not have yet,
 * so only the Frigid part is granted here - the same set as the official
 * {@code DynamicAbility_SnezhnayaFrigid_Only_V7_0_Team} group.
 *
 * <p>Two server global values drive it. {@code SGV_SnezhnayaEnergy} is the Kresnik energy - the warmth
 * the team spends in cold areas - and only once it runs out does {@code SGV_SnezhnayaFrigid} start to
 * build (0..100, the team loses HP from 99.5 up). Neither is simulated yet: the Frigid levels come from
 * Snezhnaya's weather areas, whose outlines only the client has. The server hands out full warmth and
 * no Frigid, and logs the client's ABILITY_MIXIN_CHANGE_SNEZHNAYA_FRIGID_VAR invokes to work out the rest.
 */
public final class SnezhnayaFrigid {
    public static final String FRIGID_GV = "SGV_SnezhnayaFrigid";
    public static final String ENERGY_GV = "SGV_SnezhnayaEnergy";

    /**
     * Full Kresnik energy. No resource states the cap; 100 matches the Frigid meter's scale and the
     * thresholds the abilities test the energy against (a camping stove needs at least 50, the warmth
     * search tops it up at 40 or below).
     */
    public static final float MAX_ENERGY = 100f;

    /** DynamicAbility_SnezhnayaFrigid_Only_V7_0_Team. */
    static final List<String> TEAM_ABILITIES =
            List.of(
                    "SceneObj_Area_Zd_SnezhnayaFrigid_TeamAbility_Basic",
                    "Snezhnaya_ZDNervePlant_TeamAbility",
                    "Snezhnaya_StarConductor_TeamAbility",
                    "Snezhnaya_PlayerGlasses_Eff");

    private static final int OVERWORLD_SCENE = 3;
    private static final int FIRST_AREA = 55;
    private static final int LAST_AREA = 59;

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

    /**
     * Grants or withdraws the abilities for the player's current position without notifying the client,
     * for scene entry: the team control block in the enter-scene packets then already carries them.
     */
    public static void syncForSceneEntry(Player player) {
        if (player.getTeamManager() == null) return;
        boolean in = isIn(player);
        if (in != isGranted(player)) TeamAbilityToggle.set(player, TEAM_ABILITIES, in, false);
    }

    /** Applies a seamless border crossing: updates the team abilities and, on entry, fills the warmth. */
    public static void refresh(Player player) {
        if (player.getTeamManager() == null || player.getSession() == null) return;
        boolean in = isIn(player);
        if (in == isGranted(player)) return;

        TeamAbilityToggle.set(player, TEAM_ABILITIES, in, true);
        var team = player.getTeamManager().getEntity();
        if (in && team != null) {
            player.sendPacket(new PacketServerGlobalValueChangeNotify(team.getId(), ENERGY_GV, MAX_ENERGY));
            player.sendPacket(new PacketServerGlobalValueChangeNotify(team.getId(), FRIGID_GV, 0f));
        }
    }
}
