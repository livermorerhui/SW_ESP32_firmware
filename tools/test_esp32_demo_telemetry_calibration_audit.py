import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from esp32_demo_telemetry_calibration_audit import build_report


CAP = "RX line emitted=ACK:CAP stream_control_supported=1"
REQUEST = "[STREAM_CONTROL] request enabled=true rate_hz=10 source=connect"
LEGACY_VALID = "[LAYER:MEASUREMENT_CONSUME] seq=1 valid=1"
SUMMARY_VALID = "[MEASUREMENT_CONSUME_SUMMARY] valid_count=2 invalid_count=1 ignored_count=0"


def audit(*android_lines: str, esp32: str = "", stream_count: int = 1, notes: str = "result=pass") -> tuple[str, str]:
    with TemporaryDirectory() as temp_dir:
        capture = Path(temp_dir)
        stream = [f"RX line emitted=EVT:STREAM seq={i}" for i in range(stream_count)]
        (capture / "android_logcat_focus.log").write_text("\n".join([*stream, *android_lines]))
        (capture / "esp32_serial.log").write_text(
            "[MEASUREMENT_DIAG] ok=4 fail=0\n[MEASUREMENT_HEALTH] state=READY\n" + esp32
        )
        (capture / "notes.md").write_text(notes)
        return build_report(capture)


class DemoTelemetryAuditTests(unittest.TestCase):
    def test_request_alone_is_not_an_acknowledgement(self):
        verdict, report = audit(CAP, REQUEST, LEGACY_VALID)
        self.assertEqual(verdict, "FAIL_STREAM_SUBSCRIPTION_HANDSHAKE")
        self.assertIn("STREAM_SUBSCRIPTION_NOT_ACKED", report)

    def test_subscription_confirmation_without_request_log(self):
        confirmations = [
            ("[STREAM_SUBSCRIPTION_RESULT] enabled=true rate_hz=10 acked=true source=connect supported=true", ""),
            ("RX line emitted=ACK:STREAM enabled=1 supported=1 rate_hz=10", ""),
            ("", "[STREAM_CONTROL] action=set enabled=1 rate_hz=10"),
        ]
        for android, esp32 in confirmations:
            with self.subTest(android=android, esp32=esp32):
                verdict, _ = audit(CAP, android, LEGACY_VALID, esp32=esp32)
                self.assertEqual(verdict, "PASS_CANDIDATE")

    def test_legacy_device_can_skip_stream_subscription(self):
        skip = "[STREAM_SUBSCRIPTION_RESULT] enabled=false rate_hz=10 acked=false source=connect reason=capability_not_supported"
        verdict, _ = audit("RX line emitted=ACK:CAP stream_control_supported=0", skip, LEGACY_VALID)
        self.assertEqual(verdict, "PASS_CANDIDATE")

    def test_subscription_timeout_is_reported_as_failure(self):
        timeout = "[STREAM_SUBSCRIPTION_RESULT] enabled=false rate_hz=10 acked=false source=connect reason=timeout"
        verdict, report = audit(CAP, REQUEST, timeout, LEGACY_VALID)
        self.assertEqual(verdict, "FAIL_STREAM_SUBSCRIPTION_HANDSHAKE")
        self.assertIn("STREAM_SUBSCRIPTION_ENABLE_FAILED", report)

    def test_reply_for_another_stream_configuration_does_not_confirm_request(self):
        for reply, device_log in [
            ("RX line emitted=ACK:STREAM enabled=1 supported=0 rate_hz=10", ""),
            ("[STREAM_SUBSCRIPTION_RESULT] acked=true enabled=true supported=true rate_hz=1", ""),
            ("", "[STREAM_CONTROL] action=set enabled=1 rate_hz=1"),
        ]:
            with self.subTest(reply=reply, device_log=device_log):
                verdict, _ = audit(CAP, REQUEST, reply, LEGACY_VALID, esp32=device_log)
                self.assertEqual(verdict, "FAIL_STREAM_SUBSCRIPTION_HANDSHAKE")

    def test_invalid_or_ignored_measurements_do_not_pass_on_transport_alone(self):
        samples = [
            "[MEASUREMENT_CONSUME_SUMMARY] valid_count=0 invalid_count=4 ignored_count=0",
            "[MEASUREMENT_CONSUME_SUMMARY] valid_count=0 invalid_count=0 ignored_count=4",
            "[LAYER:MEASUREMENT_CONSUME] seq=1 distance_valid=1 weight_valid=0",
        ]
        for sample in samples:
            with self.subTest(sample=sample):
                verdict, _ = audit(sample, stream_count=10)
                self.assertEqual(verdict, "FAIL_APP_STREAM_CONSUME_GAP")

    def test_old_capture_without_consumer_logs_retains_evidence_gap(self):
        verdict, _ = audit(stream_count=10)
        self.assertEqual(verdict, "PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP")

    def test_summary_and_successful_calibration_result_are_recognized(self):
        verdict, _ = audit(
            SUMMARY_VALID,
            "[CAL_CAPTURE_ATTEMPT] route=APP_LIVE_SNAPSHOT distance_present=true",
            "[CAL_CAPTURE_RESULT] result=success point_count=1",
        )
        self.assertEqual(verdict, "PASS_CANDIDATE")

    def test_unfinished_calibration_attempt_is_reported(self):
        verdict, _ = audit(LEGACY_VALID, "[CAL_CAPTURE_ATTEMPT] route=APP_LIVE_SNAPSHOT distance_present=true", "NACK:CAL_POINT")
        self.assertEqual(verdict, "FAIL_CALIBRATION_INPUT_GAP")

    def test_failed_calibration_result_without_attempt_is_reported(self):
        for reason in ("NO_LIVE_DISTANCE", "INVALID_REFERENCE_WEIGHT"):
            with self.subTest(reason=reason):
                verdict, report = audit(LEGACY_VALID, f"[CAL_CAPTURE_RESULT] result=failure reason={reason}")
                self.assertEqual(verdict, "FAIL_CALIBRATION_INPUT_GAP")
                if reason == "NO_LIVE_DISTANCE":
                    self.assertIn("CALIBRATION_NO_LIVE_DISTANCE", report)

    def test_operator_verdict_uses_final_marker_and_honors_negative_notes(self):
        for notes, expected in [
            ("result=pass，无异常", "PASS_CANDIDATE"),
            ("不通过", "NEEDS_REVIEW"),
            ("result=pass\ncapture stopped result=observe summary=未确认是否通过", "NEEDS_REVIEW"),
            ("result=fail\ncapture stopped result=pass summary=无异常", "PASS_CANDIDATE"),
        ]:
            with self.subTest(notes=notes):
                verdict, _ = audit(LEGACY_VALID, notes=notes)
                self.assertEqual(verdict, expected)


if __name__ == "__main__":
    unittest.main()
