from __future__ import annotations

from .utils import run_case, send_and_expect


async def run(client, ctx):
    reporter = ctx["reporter"]
    suite = "laser"
    results = []

    async def case_scale_zero():
        resp = await client.auto_reconnect(
            lambda: send_and_expect(
                client,
                "SCALE:ZERO",
                lambda m: m.startswith("ACK:") or m.startswith("NACK:"),
                timeout_s=4.0,
            ),
            operation_name="SCALE:ZERO",
        )
        if resp.startswith("ACK:OK"):
            return "PASS", f"Received: {resp}"
        return "FAIL", f"Unexpected response: {resp}"

    results.append(
        await run_case("LASER-001", suite, "SCALE:ZERO basic link", reporter, case_scale_zero)
    )

    async def case_scale_cal():
        resp = await client.auto_reconnect(
            lambda: send_and_expect(
                client,
                "SCALE:CAL z=-22.0 k=1.0",
                lambda m: m.startswith("ACK:") or m.startswith("NACK:"),
                timeout_s=4.0,
            ),
            operation_name="SCALE:CAL",
        )
        if resp.startswith("ACK:OK"):
            return "PASS", f"Received: {resp}"
        return "FAIL", f"Unexpected response: {resp}"

    results.append(
        await run_case("LASER-002", suite, "SCALE:CAL basic link", reporter, case_scale_cal)
    )

    async def case_evt_param_optional():
        try:
            evt = await client.wait_for_notify(lambda m: m.startswith("EVT:PARAM:"), timeout=5.0)
            return "PASS", f"Received optional event: {evt}"
        except TimeoutError:
            return "SKIPPED", "EVT:PARAM not observed in timeout (timing/device dependent)."

    results.append(
        await run_case("LASER-003", suite, "Observe EVT:PARAM", reporter, case_evt_param_optional)
    )

    async def case_evt_stable_optional():
        try:
            evt = await client.wait_for_notify(lambda m: m.startswith("EVT:STABLE:"), timeout=12.0)
            return "PASS", f"Received optional event: {evt}"
        except TimeoutError:
            return (
                "SKIPPED",
                "EVT:STABLE requires stable user/load condition; not observed in timeout.",
            )

    results.append(
        await run_case(
            "LASER-004", suite, "Observe EVT:STABLE (optional)", reporter, case_evt_stable_optional
        )
    )

    return results
