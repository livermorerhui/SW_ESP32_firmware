from __future__ import annotations

import time
from typing import Any, Awaitable, Callable, Dict, Tuple


def _status_rank(status: str) -> str:
    if status not in {"PASS", "FAIL", "SKIPPED"}:
        return "FAIL"
    return status


async def run_case(
    case_id: str,
    suite: str,
    description: str,
    reporter,
    runner: Callable[[], Awaitable[Tuple[str, str]]],
) -> Dict[str, Any]:
    start = time.perf_counter()
    try:
        status, details = await runner()
        status = _status_rank(status)
    except Exception as exc:
        status = "FAIL"
        details = f"Unhandled exception: {type(exc).__name__}: {exc}"

    duration = time.perf_counter() - start
    reporter.log(f"[{suite}/{case_id}] {status} - {details}")

    return {
        "id": case_id,
        "suite": suite,
        "description": description,
        "status": status,
        "details": details,
        "duration_s": round(duration, 3),
    }


async def send_and_expect(client, command: str, predicate, timeout_s: float = 3.0) -> str:
    client.clear_notify_queue()
    await client.write_rx(command)
    return await client.wait_for_notify(predicate, timeout=timeout_s)
