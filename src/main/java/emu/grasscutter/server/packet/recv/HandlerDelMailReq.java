package emu.grasscutter.server.packet.recv;

import emu.grasscutter.net.packet.Opcodes;
import emu.grasscutter.net.packet.PacketHandler;
import emu.grasscutter.net.packet.PacketOpcodes;
import emu.grasscutter.net.proto.DelMailReqOuterClass.DelMailReq;
import emu.grasscutter.server.game.GameSession;

import java.util.ArrayList;
import java.util.List;

@Opcodes(PacketOpcodes.DelMailReq)
public class HandlerDelMailReq extends PacketHandler {
    /*
     * The bundled proto definitions predate the 7.1 client, so the generated
     * accessors can come back empty even when the client did send mail ids.
     * Fall back to a plain protobuf wire-format walk and collect every varint
     * (standalone or packed) as a candidate mail id.
     */
    private static List<Integer> parseMailIdsFromRaw(byte[] payload) {
        List<Integer> ids = new ArrayList<>();
        int i = 0;
        while (payload != null && i < payload.length) {
            int tag = payload[i++] & 0xFF;
            int wire = tag & 0x07;
            if (wire == 0) {
                long value = 0;
                int shift = 0;
                while (i < payload.length) {
                    int b = payload[i++] & 0xFF;
                    value |= (long) (b & 0x7F) << shift;
                    if ((b & 0x80) == 0) break;
                    shift += 7;
                }
                ids.add((int) value);
            } else if (wire == 2) {
                if (i >= payload.length) break;
                int len = payload[i++] & 0xFF;
                int end = Math.min(i + len, payload.length);
                while (i < end) {
                    long value = 0;
                    int shift = 0;
                    while (i < end) {
                        int b = payload[i++] & 0xFF;
                        value |= (long) (b & 0x7F) << shift;
                        if ((b & 0x80) == 0) break;
                        shift += 7;
                    }
                    ids.add((int) value);
                }
                i = end;
            } else if (wire == 5) {
                i += 4;
            } else if (wire == 1) {
                i += 8;
            } else {
                break;
            }
        }
        return ids;
    }


    @Override
    public void handle(
            GameSession session,
            byte[] header,
            byte[] payload)
            throws Exception {

        DelMailReq req =
                DelMailReq.parseFrom(payload);

        var player =
                session.getPlayer();

        var mailHandler =
                player.getMailHandler();
        List<Integer> clientMailIds =
                new ArrayList<>(
                        req.getMailIdListList());
        if (clientMailIds.isEmpty()) {
            clientMailIds.addAll(
                    parseMailIdsFromRaw(payload));
        }
        /*
         * The client sends positive IDs:
         *
         * client ID 1 -> internal list index 0
         * client ID 2 -> internal list index 1
         *
         * MailHandler.deleteMail(List) expects internal indexes.
         */
        List<Integer> internalIndexes =
                new ArrayList<>();

        for (int clientMailId : clientMailIds) {

            int internalIndex =
                    mailHandler.toInternalMailIndex(
                            clientMailId);

            /*
             * Ignore invalid IDs instead of allowing an invalid
             * List.get(index) operation.
             */
            if (internalIndex < 0) {
                continue;
            }

            /*
             * Avoid processing the same mail twice if the client
             * unexpectedly sends a duplicate ID.
             */
            if (!internalIndexes.contains(
                    internalIndex)) {

                internalIndexes.add(
                        internalIndex);
            }
        }

        /*
         * MailHandler performs the deletion and sends:
         *
         * - PacketDelMailRsp
         * - PacketMailChangeNotify
         *
         * Do not send those packets again from this handler.
         */
        mailHandler.deleteMail(
                internalIndexes);
    }
}