package emu.grasscutter.server.packet.recv;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertThrows;

import emu.grasscutter.net.proto.ClientScriptEventNotifyOuterClass.ClientScriptEventNotify;
import emu.grasscutter.scripts.constants.EventType;

import org.junit.jupiter.api.Test;

class ClientScriptEventNotifyTest {
    @Test
    void emptyNearPlatformParametersAreRejectedBeforeSceneAccess() {
        byte[] payload =
                ClientScriptEventNotify.newBuilder()
                        .setEventType(EventType.EVENT_AVATAR_NEAR_PLATFORM)
                        .build()
                        .toByteArray();

        assertDoesNotThrow(
                () -> new HandlerClientScriptEventNotify().handle(null, new byte[0], payload));
    }

    @Test
    void emptyParametersForOtherEventsStillReachSceneAccess() {
        byte[] payload =
                ClientScriptEventNotify.newBuilder()
                        .setEventType(EventType.EVENT_PLATFORM_REACH_POINT)
                        .build()
                        .toByteArray();

        assertThrows(
                NullPointerException.class,
                () -> new HandlerClientScriptEventNotify().handle(null, new byte[0], payload));
    }
}
