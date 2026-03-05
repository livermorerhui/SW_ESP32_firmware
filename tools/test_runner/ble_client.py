from __future__ import annotations

import asyncio
from typing import Awaitable, Callable, Optional

try:
    from bleak import BleakClient, BleakScanner
except Exception as exc:  # pragma: no cover
    BleakClient = None  # type: ignore
    BleakScanner = None  # type: ignore
    _IMPORT_ERROR = exc
else:
    _IMPORT_ERROR = None


class BLEClient:
    def __init__(self, target: dict, logger: Optional[Callable[[str], None]] = None) -> None:
        self.target = target
        self.logger = logger or (lambda _: None)

        self.device_name = target.get("device_name", "")
        self.device_address = target.get("device_address", "")
        self.service_uuid = target.get("service_uuid", "")
        self.rx_char_uuid = target.get("rx_char_uuid", "")
        self.tx_char_uuid = target.get("tx_char_uuid", "")

        self.connect_timeout_s = float(target.get("connect_timeout_s", 8))
        self.reconnect_retry = int(target.get("reconnect_retry", 2))
        self.notify_timeout_ms = int(target.get("notify_timeout_ms", 3000))

        self._client: Optional[BleakClient] = None
        self._notify_started = False
        self._notify_callbacks: list[Callable[[str], None]] = []
        self._notify_queue: asyncio.Queue[str] = asyncio.Queue()
        self._connected_event = asyncio.Event()

    @property
    def is_connected(self) -> bool:
        return bool(self._client and self._client.is_connected)

    async def connect(self) -> None:
        if _IMPORT_ERROR is not None:
            raise RuntimeError(
                "Failed to import bleak. Please install dependencies: pip install -r tools/test_runner/requirements.txt"
            ) from _IMPORT_ERROR

        if self.is_connected:
            self.logger("BLE already connected")
            return

        device = await self._find_device()
        if device is None:
            raise RuntimeError(
                f"BLE target not found (device_name={self.device_name!r}, device_address={self.device_address!r})"
            )

        self.logger(f"Connecting to {device.name or device.address} ...")
        self._client = BleakClient(
            device, timeout=self.connect_timeout_s, disconnected_callback=self._on_disconnected
        )
        await self._client.connect()
        self._connected_event.set()
        self.logger("BLE connected")

        if self._notify_callbacks:
            await self._ensure_notify_started()

    async def disconnect(self) -> None:
        if not self._client:
            return

        try:
            if self._notify_started:
                await self._client.stop_notify(self.tx_char_uuid)
                self._notify_started = False
        except Exception as exc:
            self.logger(f"stop_notify warning: {exc}")

        try:
            if self._client.is_connected:
                await self._client.disconnect()
                self.logger("BLE disconnected")
        finally:
            self._connected_event.clear()
            self._client = None

    async def write_rx(self, str_cmd: str) -> None:
        if not self.is_connected or not self._client:
            raise RuntimeError("BLE not connected")

        await self._client.write_gatt_char(
            self.rx_char_uuid, str_cmd.encode("utf-8"), response=True
        )
        self.logger(f">> {str_cmd}")

    async def subscribe_tx(self, callback: Callable[[str], None]) -> None:
        self._notify_callbacks.append(callback)
        await self._ensure_notify_started()

    async def wait_for_notify(
        self,
        predicate: Callable[[str], bool],
        timeout: Optional[float] = None,
    ) -> str:
        timeout_s = float(timeout) if timeout is not None else (self.notify_timeout_ms / 1000.0)
        end = asyncio.get_running_loop().time() + timeout_s

        while True:
            remain = end - asyncio.get_running_loop().time()
            if remain <= 0:
                raise TimeoutError(f"Notify timeout ({timeout_s:.2f}s)")

            msg = await asyncio.wait_for(self._notify_queue.get(), timeout=remain)
            if predicate(msg):
                return msg

    async def auto_reconnect(
        self,
        operation: Callable[[], Awaitable[str]],
        operation_name: str = "operation",
    ) -> str:
        retries = max(self.reconnect_retry, 0)
        attempt = 0
        last_exc: Optional[Exception] = None

        while attempt <= retries:
            try:
                return await operation()
            except Exception as exc:
                last_exc = exc
                if attempt >= retries:
                    break
                self.logger(f"{operation_name} failed (attempt {attempt + 1}/{retries + 1}): {exc}")
                await self._reconnect_once()
                attempt += 1

        raise RuntimeError(f"{operation_name} failed after retries: {last_exc}")

    def clear_notify_queue(self) -> None:
        while not self._notify_queue.empty():
            try:
                self._notify_queue.get_nowait()
            except asyncio.QueueEmpty:
                break

    async def _find_device(self):
        assert BleakScanner is not None

        devices = await BleakScanner.discover(timeout=self.connect_timeout_s)
        for dev in devices:
            if self.device_address and dev.address.lower() == self.device_address.lower():
                return dev
            if self.device_name and (dev.name or "") == self.device_name:
                return dev
        return None

    async def _ensure_notify_started(self) -> None:
        if not self._client or not self._client.is_connected:
            return
        if self._notify_started:
            return

        await self._client.start_notify(self.tx_char_uuid, self._on_notify)
        self._notify_started = True
        self.logger("TX notify subscribed")

    async def _reconnect_once(self) -> None:
        await self.disconnect()
        await asyncio.sleep(1.0)
        await self.connect()

    def _on_notify(self, _sender: int, data: bytearray) -> None:
        msg = bytes(data).decode("utf-8", errors="ignore").strip()
        if msg:
            self.logger(f"<< {msg}")
            self._notify_queue.put_nowait(msg)
            for cb in self._notify_callbacks:
                try:
                    cb(msg)
                except Exception as exc:  # pragma: no cover
                    self.logger(f"notify callback warning: {exc}")

    def _on_disconnected(self, _client) -> None:
        self._connected_event.clear()
        self.logger("BLE disconnected callback fired")
