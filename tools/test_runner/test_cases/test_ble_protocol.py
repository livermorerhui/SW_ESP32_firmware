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

    async def case_invalid_param():
        invalid_commands = [
            "WAVE:SET f=abc,i=60",
            "SCALE:CAL z=-22.0 k=oops",
            "CAL:CAPTURE w=nan",
            "CAL:SET_MODEL type=LINEAR ref=-22.0 c0=0 c1=bad c2=0",
            "SET_PS:-22.0,oops",
            "F:12.5,I:bad,E:1",
        ]

        for command in invalid_commands:
            resp = await client.auto_reconnect(
                lambda command=command: send_and_expect(
                    client,
                    command,
                    lambda m: m.startswith("NACK:") or m.startswith("ACK:"),
                    timeout_s=4.0,
                ),
                operation_name=f"Invalid parameter handling: {command}",
            )
            if not resp.startswith("NACK:INVALID_PARAM"):
                return "FAIL", f"Expected NACK:INVALID_PARAM for {command}, got {resp}"

        return "PASS", f"Rejected {len(invalid_commands)} invalid-parameter commands"

    results.append(
        await run_case(
            "BLE-003",
            suite,
            "Malformed numeric parameters return NACK:INVALID_PARAM",
            reporter,
            case_invalid_param,
        )
    )

    return results
