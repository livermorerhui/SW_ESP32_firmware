from __future__ import annotations

from .utils import run_case, send_and_expect


async def run(client, ctx):
    reporter = ctx["reporter"]
    suite = "wave"
    results = []
    runtime = ctx.setdefault("runtime", {})

    async def case_wave_set():
        resp = await client.auto_reconnect(
            lambda: send_and_expect(
                client,
                "WAVE:SET freq=20 amp=60",
                lambda m: m.startswith("ACK:") or m.startswith("NACK:"),
                timeout_s=4.0,
            ),
            operation_name="WAVE:SET",
        )
        if resp.startswith("ACK:OK"):
            return "PASS", f"Received: {resp}"
        return "FAIL", f"Unexpected response: {resp}"

    results.append(
        await run_case("WAVE-001", suite, "WAVE:SET legal params", reporter, case_wave_set)
    )

    async def case_wave_start():
        resp = await client.auto_reconnect(
            lambda: send_and_expect(
                client,
                "WAVE:START",
                lambda m: m.startswith("ACK:") or m.startswith("NACK:"),
                timeout_s=4.0,
            ),
            operation_name="WAVE:START",
        )

        if resp.startswith("NACK:NOT_ARMED"):
            runtime["wave_started"] = False
            return "SKIPPED", "Device not ARMED. Place user/load to satisfy start precondition."
        if resp.startswith("NACK:"):
            runtime["wave_started"] = False
            return "FAIL", f"Start rejected: {resp}"

        runtime["wave_started"] = True
        try:
            evt = await client.wait_for_notify(
                lambda m: m.startswith("EVT:STATE") and "RUNNING" in m, timeout=5.0
            )
            return "PASS", f"Received: {resp}; Event: {evt}"
        except TimeoutError:
            return "FAIL", f"Received {resp} but no EVT:STATE RUNNING within timeout"

    results.append(
        await run_case("WAVE-002", suite, "WAVE:START enters RUNNING", reporter, case_wave_start)
    )

    async def case_wave_stop():
        resp = await client.auto_reconnect(
            lambda: send_and_expect(
                client,
                "WAVE:STOP",
                lambda m: m.startswith("ACK:") or m.startswith("NACK:"),
                timeout_s=4.0,
            ),
            operation_name="WAVE:STOP",
        )
        if resp.startswith("NACK:"):
            return "FAIL", f"Stop rejected: {resp}"

        if runtime.get("wave_started"):
            try:
                evt = await client.wait_for_notify(
                    lambda m: m.startswith("EVT:STATE") and ("IDLE" in m or "FAULT_STOP" in m),
                    timeout=5.0,
                )
                return "PASS", f"Received: {resp}; Event: {evt}"
            except TimeoutError:
                return "FAIL", f"Received {resp} but no stop-state event"

        return "PASS", f"Received: {resp}"

    results.append(
        await run_case("WAVE-003", suite, "WAVE:STOP exits RUNNING", reporter, case_wave_stop)
    )

    return results
