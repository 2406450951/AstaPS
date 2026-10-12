package emu.grasscutter.server.packet.recv;

import emu.grasscutter.net.packet.*;
import emu.grasscutter.net.proto.GadgetInteractReqOuterClass.GadgetInteractReq;
import emu.grasscutter.server.game.GameSession;

@Opcodes(PacketOpcodes.GadgetInteractReq)
public class HandlerGadgetInteractReq extends PacketHandler {

    @Override
    public void handle(GameSession session, byte[] header, byte[] payload) throws Exception {
        GadgetInteractReq req = GadgetInteractReq.parseFrom(payload);
        var entity = session.getPlayer().getScene().getEntityById(req.getGadgetEntityId());
        if (entity instanceof emu.grasscutter.game.entity.EntityGadget gadget
                && (gadget.getContent() instanceof emu.grasscutter.game.entity.gadget.GadgetLiyueDustBell
                    || gadget.getContent() instanceof emu.grasscutter.game.entity.gadget.GadgetLiyueGuizhong
                    || gadget.getContent() instanceof emu.grasscutter.game.entity.gadget.GadgetLiyueJadeMaterials)) {
            session.getPlayer().interactWith(req.getGadgetEntityId(), req);
            return;
        }

        session.getPlayer().interactWith(req.getGadgetEntityId(), req);
    }
}
