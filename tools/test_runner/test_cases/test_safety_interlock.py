from __future__ import annotations

import asyncio

from .utils import run_case, send_and_expect


async def run(client, ctx):
    reporter = ctx["reporter"]
    suite = "safety"
    results = []

    async def case_disconnect_interlock():
        start_resp = await client.auto_reconnect(
            lambda: send_and_expect(
                client,
                "WAVE:START",
                lambda m: m.startswith("ACK:") or m.startswith("NACK:"),
                timeout_s=4.0,
            ),
            operation_name="Safety pre-start",
        )

        if start_resp.startswith("NACK:NOT_ARMED"):
            return (
                "SKIPPED",
                "Cannot validate disconnect interlock automatically: device not ARMED.",
            )
        if start_resp.startswith("NACK:"):
            return "FAIL", f"Unexpected start response: {start_resp}"

        await client.disconnect()
        await asyncio.sleep(1.0)
        await client.connect()
        await client.subscribe_tx(lambda _msg: None)

        cap_resp = await send_and_expect(
            client,
            "CAP?",
            lambda m: m.startswith("ACK:CAP") or m.startswith("ACK:"),
            timeout_s=4.0,
        )

        retry_resp = await send_and_expect(
            client,
            "WAVE:START",
            lambda m: m.startswith("ACK:") or m.startswith("NACK:"),
            timeout_s=4.0,
        )

        if retry_resp.startswith("NACK:FAULT_LOCKED") or retry_resp.startswith("NACK:NOT_ARMED"):
            return "PASS", f"After BLE reconnect got {retry_resp}; CAP response was {cap_resp}"

        return "FAIL", f"Expected interlock evidence after disconnect, got {retry_resp}"

    results.append(
        await run_case(
            "SAFE-001",
            suite,
            "BLE disconnect should trigger safe-stop evidence",
            reporter,
            case_disconnect_interlock,
        )
    )

    async def case_sensor_fault_manual():
        return (
            "SKIPPED",
            "Manual step required: disconnect laser sensor bus during RUNNING and verify FAULT_STOP + stop output.",
        )

    results.append(
        await run_case(
            "SAFE-002",
            suite,
            "Sensor exception interlock (manual assist)",
            reporter,
            case_sensor_fault_manual,
        )
    )

    async def case_user_leave_manual():
        return (
            "SKIPPED",
            "Manual step required: user leaves platform (weight < LEAVE_TH) and verify FAULT_STOP + stop output.",
        )

    results.append(
        await run_case(
            "SAFE-003",
            suite,
            "User leave interlock (manual assist)",
            reporter,
            case_user_leave_manual,
        )
    )

    async def case_fall_manual():
        return (
            "SKIPPED",
            "Manual step required: induce rapid weight change and verify onFallSuspected -> FAULT_STOP.",
        )

    results.append(
        await run_case(
            "SAFE-004",
            suite,
            "Fall suspected interlock (manual assist)",
            reporter,
            case_fall_manual,
        )
    )

    return results
