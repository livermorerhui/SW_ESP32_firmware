from __future__ import annotations

from .utils import run_case, send_and_expect


async def run(client, ctx):
    reporter = ctx["reporter"]
    suite = "ble"
    results = []

    async def case_cap_query():
        resp = await client.auto_reconnect(
            lambda: send_and_expect(
                client,
                "CAP?",
                lambda m: m.startswith("ACK:CAP") or (m.startswith("ACK:") and "CAP" in m),
                timeout_s=4.0,
            ),
            operation_name="CAP query",
        )
        return "PASS", f"Received: {resp}"

    results.append(
        await run_case(
            "BLE-001",
            suite,
            "CAP? returns capability ACK",
            reporter,
            case_cap_query,
        )
    )

    async def case_invalid_cmd():
        resp = await client.auto_reconnect(
            lambda: send_and_expect(
                client,
                "BAD:CMD",
                lambda m: m.startswith("NACK:"),
                timeout_s=4.0,
            ),
            operation_name="Invalid command handling",
        )
        return "PASS", f"Received: {resp}"

    results.append(
        await run_case(
            "BLE-002",
            suite,
            "Invalid command returns NACK",
            reporter,
            case_invalid_cmd,
        )
    )

    return results
