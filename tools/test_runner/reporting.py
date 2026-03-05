from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List

try:
    from rich.console import Console
except Exception:  # pragma: no cover
    Console = None


class TestReporter:
    def __init__(self, base_dir: Path) -> None:
        self.start_time = datetime.now(timezone.utc)
        self.session_name = self.start_time.strftime("%Y%m%d_%H%M%S")
        self.session_dir = base_dir / self.session_name
        self.session_dir.mkdir(parents=True, exist_ok=True)
        self.raw_logs: List[str] = []
        self.console = Console() if Console else None

    def log(self, message: str) -> None:
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        line = f"[{ts}] {message}"
        self.raw_logs.append(line)
        if self.console:
            self.console.print(line)
        else:
            print(line)

    def write(self, metadata: Dict[str, Any], results: List[Dict[str, Any]]) -> None:
        end_time = datetime.now(timezone.utc)

        summary = {
            "total": len(results),
            "passed": sum(1 for r in results if r.get("status") == "PASS"),
            "failed": sum(1 for r in results if r.get("status") == "FAIL"),
            "skipped": sum(1 for r in results if r.get("status") == "SKIPPED"),
            "duration_s": round((end_time - self.start_time).total_seconds(), 3),
        }

        payload = {
            "metadata": metadata,
            "summary": summary,
            "results": results,
            "start_time_utc": self.start_time.isoformat(),
            "end_time_utc": end_time.isoformat(),
        }

        (self.session_dir / "session.json").write_text(
            json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        (self.session_dir / "raw_log.txt").write_text(
            "\n".join(self.raw_logs) + "\n", encoding="utf-8"
        )
        (self.session_dir / "results.md").write_text(
            self._render_md(metadata, summary, results), encoding="utf-8"
        )

    def _render_md(
        self,
        metadata: Dict[str, Any],
        summary: Dict[str, Any],
        results: List[Dict[str, Any]],
    ) -> str:
        lines: List[str] = []
        lines.append("# SonicWave Automation Test Results")
        lines.append("")
        lines.append("## Session")
        lines.append("")
        lines.append(f"- Session: `{self.session_name}`")
        lines.append(f"- Config: `{metadata.get('config_path', '')}`")
        lines.append(f"- Suite: `{metadata.get('suite', 'all')}`")
        lines.append(f"- Device: `{metadata.get('device_name', 'unknown')}`")
        lines.append("")
        lines.append("## Summary")
        lines.append("")
        lines.append(f"- Total: **{summary['total']}**")
        lines.append(f"- Passed: **{summary['passed']}**")
        lines.append(f"- Failed: **{summary['failed']}**")
        lines.append(f"- Skipped: **{summary['skipped']}**")
        lines.append(f"- Duration: **{summary['duration_s']} s**")
        lines.append("")
        lines.append("## Case Results")
        lines.append("")
        lines.append("| ID | Suite | Status | Description | Details | Duration(s) |")
        lines.append("|---|---|---|---|---|---:|")

        for result in results:
            lines.append(
                "| {id} | {suite} | {status} | {description} | {details} | {duration_s:.3f} |".format(
                    id=result.get("id", ""),
                    suite=result.get("suite", ""),
                    status=result.get("status", ""),
                    description=str(result.get("description", "")).replace("|", "\\|"),
                    details=str(result.get("details", "")).replace("|", "\\|"),
                    duration_s=float(result.get("duration_s", 0.0)),
                )
            )

        lines.append("")
        lines.append("## Artifacts")
        lines.append("")
        lines.append("- `session.json`")
        lines.append("- `raw_log.txt`")
        lines.append("- `results.md`")
        lines.append("")
        return "\n".join(lines)
