package emu.grasscutter.server.packet.recv;

import emu.grasscutter.data.GameData;
import emu.grasscutter.net.packet.*;
import emu.grasscutter.net.proto.UnlockTransPointReqOuterClass.UnlockTransPointReq;
import emu.grasscutter.server.game.GameSession;

@Opcodes(PacketOpcodes.UnlockTransPointReq)
public class HandlerUnlockTransPointReq extends PacketHandler {
    @Override
    public void handle(GameSession session, byte[] header, byte[] payload) throws Exception {
        UnlockTransPointReq req = UnlockTransPointReq.parseFrom(payload);
        var entry = GameData.getScenePointEntryById(req.getSceneId(), req.getPointId());
        boolean isStatue =
                emu.grasscutter.game.managers.StatueTalkQuests.isStatuePoint(
                        entry != null ? entry.getPointData() : null);
        // A Statue of the Seven is activated by quest 303 (or the starter quest 352),
        // not by the ordinary waypoint request.
        if (isStatue) return;

        boolean unlocked =
                session
                        .getPlayer()
                        .getProgressManager()
                        .unlockTransPoint(req.getSceneId(), req.getPointId(), isStatue);
        // Domain entrance activation uses an independent persisted receipt because
        // the handbook can pre-unlock its map point without a player interaction.
        boolean rewarded =
                session.getPlayer().getProgressManager()
                        .rewardDomainActivation(req.getSceneId(), req.getPointId());
        emu.grasscutter.Grasscutter.getLogger()
                .info(
                        "UnlockTransPointReq uid={} scene={} point={} statue={} unlocked={} domainRewarded={}",
                        session.getPlayer().getUid(),
                        req.getSceneId(),
                        req.getPointId(),
                        isStatue,
                        unlocked,
                        rewarded);

        // Current 7.1 clients update the live waypoint state from ScenePointUnlockNotify. Runtime
        // controls confirmed that a correctly encoded notify is sufficient for activation, map
        // usability, and teleport without an extra GetScenePointRsp refresh or an unconfirmed
        // UnlockTransPointRsp CmdId.
    }
}
