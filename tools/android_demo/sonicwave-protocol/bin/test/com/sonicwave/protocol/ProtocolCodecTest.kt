package com.sonicwave.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNotNull

class ProtocolCodecTest {
    @Test
    fun encodeCoreCommandsUseFirmwareCanonicalFormat() {
        assertEquals("CAP?", ProtocolCodec.encode(Command.CapabilityQuery))
        assertEquals("WAVE:SET f=20,i=80", ProtocolCodec.encode(Command.WaveSet(freqHz = 20, intensity = 80)))
        assertEquals("WAVE:START", ProtocolCodec.encode(Command.WaveStart))
        assertEquals("WAVE:STOP", ProtocolCodec.encode(Command.WaveStop))
        assertEquals("SCALE:ZERO", ProtocolCodec.encode(Command.ScaleZero))
        assertEquals("CAL:ZERO", ProtocolCodec.encode(Command.CalibrationZero))
        assertEquals("SCALE:CAL z=-22.0,k=1.0", ProtocolCodec.encode(Command.ScaleCal()))
        assertEquals("CAL:CAPTURE w=72.5", ProtocolCodec.encode(Command.CalibrationCapture(72.5f)))
        assertEquals("CAL:GET_MODEL", ProtocolCodec.encode(Command.CalibrationGetModel))
        assertEquals(
            "CAL:SET_MODEL type=QUADRATIC,ref=1.2,c0=0.1,c1=1.1,c2=0.2",
            ProtocolCodec.encode(
                Command.CalibrationSetModel(
                    type = CalibrationModelType.QUADRATIC,
                    referenceDistance = 1.2f,
                    c0 = 0.1f,
                    c1 = 1.1f,
                    c2 = 0.2f,
                ),
            ),
        )
    }

    @Test
    fun encodeLegacyCommandsMatchFirmwareParser() {
        assertEquals("ZERO", ProtocolCodec.encode(Command.LegacyZero))
        assertEquals("SET_PS:-22.0,1.0", ProtocolCodec.encode(Command.LegacySetPs(-22.0f, 1.0f)))
        assertEquals("F:40,I:90", ProtocolCodec.encode(Command.LegacyWaveFie(freqHz = 40, intensity = 90)))
        assertEquals("E:1", ProtocolCodec.encode(Command.LegacyWaveFie(enable = true)))
        assertEquals("E:0", ProtocolCodec.encode(Command.LegacyWaveFie(enable = false)))
    }

    @Test
    fun decodeCapabilityAck() {
        val event = ProtocolCodec.decode("ACK:CAP fw=SW-HUB-1.0.0 proto=1")
        val cap = assertIs<Event.Capabilities>(event)
        assertEquals("SW-HUB-1.0.0", cap.values["FW"])
        assertEquals("1", cap.values["PROTO"])
    }

    @Test
    fun decodeEvtState() {
        val event = ProtocolCodec.decode("EVT:STATE RUNNING")
        val state = assertIs<Event.State>(event)
        assertEquals(DeviceState.RUNNING, state.state)
    }

    @Test
    fun decodeEvtFault() {
        val event = ProtocolCodec.decode("EVT:FAULT 100")
        val fault = assertIs<Event.Fault>(event)
        assertEquals(100, fault.code)
        assertEquals("100", fault.reason)
    }

    @Test
    fun decodeEvtSafety() {
        val event = ProtocolCodec.decode(
            "EVT:SAFETY reason=USER_LEFT_PLATFORM code=100 effect=RECOVERABLE_PAUSE state=IDLE wave=STOPPED",
        )
        val safety = assertIs<Event.Safety>(event)
        assertEquals("USER_LEFT_PLATFORM", safety.reason)
        assertEquals(100, safety.code)
        assertEquals(SafetyEffect.RECOVERABLE_PAUSE, safety.effect)
        assertEquals(DeviceState.IDLE, safety.state)
        assertEquals(WaveState.STOPPED, safety.wave)
    }

    @Test
    fun decodeEvtStable() {
        val event = ProtocolCodec.decode("EVT:STABLE:65.20")
        val stable = assertIs<Event.Stable>(event)
        assertEquals(65.2f, stable.stableWeightKg)
    }

    @Test
    fun decodeEvtParam() {
        val event = ProtocolCodec.decode("EVT:PARAM:-22.00,1.0000")
        val param = assertIs<Event.Param>(event)
        assertEquals(-22.0f, param.zeroDistance)
        assertEquals(1.0f, param.scaleFactor)
    }

    @Test
    fun decodeEvtStreamAndBareCsv() {
        val evt = ProtocolCodec.decode("EVT:STREAM:120.35,66.80")
        val streamEvt = assertIs<Event.StreamSample>(evt)
        assertEquals(120.35f, streamEvt.distance)
        assertEquals(66.8f, streamEvt.weight)

        val bare = ProtocolCodec.decode("120.35,66.80")
        val streamBare = assertIs<Event.StreamSample>(bare)
        assertEquals(120.35f, streamBare.distance)
        assertEquals(66.8f, streamBare.weight)
    }

    @Test
    fun decodeCalibrationModelAck() {
        val event = ProtocolCodec.decode("ACK:CAL_MODEL type=QUADRATIC ref=1.2000 c0=0.100000 c1=1.100000 c2=0.200000")
        val model = assertIs<Event.CalibrationModel>(event)
        assertEquals(CalibrationModelType.QUADRATIC, model.type)
        assertEquals(1.2f, model.referenceDistance)
        assertEquals(0.1f, model.c0)
        assertEquals(1.1f, model.c1)
        assertEquals(0.2f, model.c2)
    }

    @Test
    fun decodeCalibrationPointAck() {
        val event = ProtocolCodec.decode("ACK:CAL_POINT idx=3 ts=1234 d_mm=100.50 ref_kg=72.50 pred_kg=71.80 stable=1 valid=1")
        val point = assertIs<Event.CalibrationPoint>(event)
        assertEquals(3, point.index)
        assertEquals(1234L, point.timestampMs)
        assertEquals(100.5f, point.distanceMm)
        assertEquals(72.5f, point.referenceWeightKg)
        assertEquals(71.8f, point.predictedWeightKg)
        assertEquals(true, point.stableFlag)
        assertEquals(true, point.validFlag)
    }

    @Test
    fun decodeCalibrationSetModelAck() {
        val event = ProtocolCodec.decode("ACK:CAL_SET_MODEL type=QUADRATIC ref=1.2000 c0=0.100000 c1=1.100000 c2=0.200000")
        val result = assertIs<Event.CalibrationSetModelResult>(event)
        assertEquals(true, result.success)
        assertEquals(CalibrationModelType.QUADRATIC, result.type)
        assertEquals(null, result.reason)
    }

    @Test
    fun decodeCalibrationSetModelNack() {
        val event = ProtocolCodec.decode("NACK:CAL_SET_MODEL type=LINEAR reason=NON_MONOTONIC")
        val result = assertIs<Event.CalibrationSetModelResult>(event)
        assertEquals(false, result.success)
        assertEquals(CalibrationModelType.LINEAR, result.type)
        assertEquals("NON_MONOTONIC", result.reason)
    }

    @Test
    fun decodeNackAndErr() {
        val nack = ProtocolCodec.decode("NACK:NOT_ARMED")
        val nackEvent = assertIs<Event.Nack>(nack)
        assertEquals("NOT_ARMED", nackEvent.reason)

        val err = ProtocolCodec.decode("ERR:PROTO_MISMATCH")
        val errEvent = assertIs<Event.Error>(err)
        assertEquals("PROTO_MISMATCH", errEvent.reason)
    }

    @Test
    fun decodeUnknownReturnsNull() {
        val unknown = ProtocolCodec.decode("HELLO")
        assertEquals(null, unknown)
    }

    @Test
    fun decodeUnknownEventDoesNotBreakParsing() {
        val unknown = ProtocolCodec.decode("EVT:FUTURE foo=bar")
        assertEquals(null, unknown)
    }

    @Test
    fun decodeAckOkAsGenericAck() {
        val event = ProtocolCodec.decode("ACK:OK")
        val ack = assertIs<Event.Ack>(event)
        assertNotNull(ack.raw)
        assertEquals("ACK:OK", ack.raw)
    }
}
