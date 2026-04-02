package com.sonicwave.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNotNull

class ProtocolCodecTest {
    @Test
    fun encodeCoreCommandsUseFirmwareCanonicalFormat() {
        assertEquals("CAP?", ProtocolCodec.encode(Command.CapabilityQuery))
        assertEquals("SNAPSHOT?", ProtocolCodec.encode(Command.SnapshotQuery))
        assertEquals(
            "DEVICE:SET_CONFIG platform_model=BASE,laser_installed=0",
            ProtocolCodec.encode(
                Command.DeviceSetConfig(
                    platformModel = PlatformModel.BASE,
                    laserInstalled = false,
                ),
            ),
        )
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
        assertEquals(
            "DEBUG:FALL_STOP enabled=0",
            ProtocolCodec.encode(Command.FallStopProtectionSet(enabled = false)),
        )
        assertEquals(
            "DEBUG:MOTION_SAMPLING enabled=1",
            ProtocolCodec.encode(Command.MotionSamplingModeSet(enabled = true)),
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
        val event = ProtocolCodec.decode(
            "ACK:CAP fw=SW-HUB-1.0.0 proto=2 platform_model=PRO laser_installed=1 fall_stop_enabled=0 fall_stop_mode=DETECT_ONLY motion_sampling_mode=1 fall_action_suppressed=1",
        )
        val cap = assertIs<Event.Capabilities>(event)
        assertEquals("SW-HUB-1.0.0", cap.values["FW"])
        assertEquals("2", cap.values["PROTO"])
        assertEquals("PRO", cap.values["PLATFORM_MODEL"])
        assertEquals("1", cap.values["LASER_INSTALLED"])
        assertEquals("0", cap.values["FALL_STOP_ENABLED"])
        assertEquals("DETECT_ONLY", cap.values["FALL_STOP_MODE"])
        assertEquals("1", cap.values["MOTION_SAMPLING_MODE"])
        assertEquals("1", cap.values["FALL_ACTION_SUPPRESSED"])
    }

    @Test
    fun decodeDeviceConfigAck() {
        val event = ProtocolCodec.decode("ACK:DEVICE_CONFIG platform_model=BASE laser_installed=0")
        val config = assertIs<Event.DeviceConfig>(event)
        assertEquals(PlatformModel.BASE, config.platformModel)
        assertEquals(false, config.laserInstalled)
    }

    @Test
    fun decodeSnapshotTruth() {
        val event = ProtocolCodec.decode(
            "SNAPSHOT: top_state=ARMED user_present=0 runtime_ready=1 start_ready=1 baseline_ready=0 wave_output_active=0 current_reason_code=NONE current_safety_effect=NONE stable_weight=0.00 current_frequency=20.00 current_intensity=80 platform_model=BASE laser_installed=0 laser_available=0 protection_degraded=1",
        )
        val snapshot = assertIs<Event.Snapshot>(event)
        assertEquals(DeviceState.ARMED, snapshot.topState)
        assertEquals(false, snapshot.userPresent)
        assertEquals(true, snapshot.runtimeReady)
        assertEquals(true, snapshot.startReady)
        assertEquals(false, snapshot.baselineReady)
        assertEquals(false, snapshot.waveOutputActive)
        assertEquals("NONE", snapshot.currentReasonCode)
        assertEquals("NONE", snapshot.currentSafetyEffect)
        assertEquals(0.0f, snapshot.stableWeightKg)
        assertEquals(20.0f, snapshot.currentFrequencyHz)
        assertEquals(80, snapshot.currentIntensity)
        assertEquals(PlatformModel.BASE, snapshot.platformModel)
        assertEquals(false, snapshot.laserInstalled)
        assertEquals(false, snapshot.laserAvailable)
        assertEquals(true, snapshot.protectionDegraded)
    }

    @Test
    fun decodeEvtState() {
        val event = ProtocolCodec.decode("EVT:STATE RUNNING")
        val state = assertIs<Event.State>(event)
        assertEquals(DeviceState.RUNNING, state.state)
    }

    @Test
    fun decodeEvtWaveOutput() {
        val event = ProtocolCodec.decode("EVT:WAVE_OUTPUT active=1")
        val output = assertIs<Event.WaveOutput>(event)
        assertEquals(true, output.active)
        assertEquals("EVT:WAVE_OUTPUT active=1", output.raw)
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
    fun decodeEvtBaselineCarriesRecoverableStartReadyTruth() {
        val event = ProtocolCodec.decode(
            "EVT:BASELINE start_ready=1 baseline_ready=1 stable_weight=68.40 ma7=68.42 deviation=0.02 ratio=0.0003 main_state=NORMAL abnormal_duration_ms=0 danger_duration_ms=0 stop_reason=NONE stop_source=NONE",
        )
        val baseline = assertIs<Event.BaselineMain>(event)
        assertEquals(true, baseline.startReady)
        assertEquals(true, baseline.baselineReady)
        assertEquals(68.4f, baseline.stableWeightKg)
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
        val evt = ProtocolCodec.decode("EVT:STREAM seq=42 ts_ms=1234 valid=1 ma12_ready=1 distance=120.35 weight=66.80 ma12=66.10")
        val streamEvt = assertIs<Event.StreamSample>(evt)
        assertEquals(MeasurementCarrier.FORMAL_EVT_STREAM, streamEvt.carrier)
        assertEquals(42L, streamEvt.sequence)
        assertEquals(1234L, streamEvt.timestampMs)
        assertEquals(120.35f, streamEvt.distance)
        assertEquals(66.8f, streamEvt.weight)
        assertEquals(66.1f, streamEvt.ma12)
        assertEquals(true, streamEvt.ma12Ready)
        assertEquals(true, streamEvt.valid)

        val bare = ProtocolCodec.decode("120.35,66.80")
        val streamBare = assertIs<Event.StreamSample>(bare)
        assertEquals(MeasurementCarrier.LEGACY_CSV_FALLBACK, streamBare.carrier)
        assertEquals(null, streamBare.sequence)
        assertEquals(120.35f, streamBare.distance)
        assertEquals(66.8f, streamBare.weight)
        assertEquals(true, streamBare.valid)
    }

    @Test
    fun decodeInvalidEvtStream() {
        val evt = ProtocolCodec.decode("EVT:STREAM seq=43 ts_ms=1260 valid=0 ma12_ready=0 reason=READ_FAIL")
        val streamEvt = assertIs<Event.StreamSample>(evt)
        assertEquals(MeasurementCarrier.FORMAL_EVT_STREAM, streamEvt.carrier)
        assertEquals(43L, streamEvt.sequence)
        assertEquals(1260L, streamEvt.timestampMs)
        assertEquals(false, streamEvt.valid)
        assertEquals(false, streamEvt.ma12Ready)
        assertEquals("READ_FAIL", streamEvt.reason)
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
