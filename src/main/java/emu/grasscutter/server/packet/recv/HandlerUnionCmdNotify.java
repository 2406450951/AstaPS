package emu.grasscutter.server.packet.recv;

import static emu.grasscutter.config.Configuration.*;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.Grasscutter.ServerDebugMode;
import emu.grasscutter.game.systems.ReliquaryDustSystem;
import emu.grasscutter.net.packet.*;
import emu.grasscutter.net.proto.UnionCmdNotifyOuterClass.UnionCmdNotify;
import emu.grasscutter.net.proto.UnionCmdOuterClass.UnionCmd;
import emu.grasscutter.server.game.GameSession;

@Opcodes(PacketOpcodes.UnionCmdNotify)
public class HandlerUnionCmdNotify extends PacketHandler {
    @Override
    public void handle(GameSession session, byte[] header, byte[] payload) throws Exception {
        UnionCmdNotify req = UnionCmdNotify.parseFrom(payload);

        // If this batch touches ReliquaryDust, log every messageId — missing sibling acks
        // can leave the reshape UI waiting forever while only 7273 is handled.
        boolean dustBatch = false;
        StringBuilder batch = new StringBuilder();
        for (UnionCmd cmd : req.getCmdListList()) {
            int id = cmd.getMessageId();
            batch.append(id).append('(').append(cmd.getBody().size()).append(") ");
            if (id == ReliquaryDustSystem.OPCODE_DUST_SELECT_REQ
                    || id == ReliquaryDustSystem.OPCODE_DUST_REQ
                    || id == ReliquaryDustSystem.OPCODE_DUST_COMPANION_REQ
                    || id == ReliquaryDustSystem.OPCODE_DUST_CONFIRM_REQ_A
                    || id == ReliquaryDustSystem.OPCODE_DUST_CONFIRM_REQ_B) {
                dustBatch = true;
            }
        }
        if (dustBatch) {
            Grasscutter.getLogger()
                    .info(
                            "ReliquaryDust UnionCmd batch uid={} cmds=[{}]",
                            session.getPlayer() != null ? session.getPlayer().getUid() : 0,
                            batch.toString().trim());
        }

        for (UnionCmd cmd : req.getCmdListList()) {
            int cmdOpcode = cmd.getMessageId();
            byte[] cmdPayload = cmd.getBody().toByteArray();
            if (GAME_INFO.logPackets == ServerDebugMode.WHITELIST
                    && SERVER.debugWhitelist.contains(cmd.getMessageId())) {
                session.logPacket("RECV in Union", cmdOpcode, cmdPayload);
            } else if (GAME_INFO.logPackets == ServerDebugMode.BLACKLIST
                    && !SERVER.debugBlacklist.contains(cmd.getMessageId())) {
                session.logPacket("RECV in Union", cmdOpcode, cmdPayload);
            }

            session
                    .getServer()
                    .getPacketHandler()
                    .handle(session, cmd.getMessageId(), header, cmd.getBody().toByteArray());
        }

        session.getPlayer().getCombatInvokeHandler().update(session.getPlayer());
        session.getPlayer().getAbilityInvokeHandler().update(session.getPlayer());
    }
}
