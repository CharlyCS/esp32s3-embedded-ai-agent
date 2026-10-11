import asyncio
import json
import queue
import time
import uuid
from typing import Any, Dict, Optional

from awscrt import mqtt
from awsiot import mqtt_connection_builder


class AwsMcpSession:
    """MCP JSON-RPC client session transported over AWS IoT MQTT/TLS."""

    def __init__(
        self,
        endpoint: str,
        thing_name: str,
        certificate: str,
        private_key: str,
        root_ca: str,
        client_id: Optional[str] = None,
        timeout: float = 30.0,
    ):
        self.request_topic = f"ai/agents/{thing_name}/tasks"
        self.response_topic = f"ai/agents/{thing_name}/results"
        self.timeout = timeout
        self._responses: queue.Queue[Dict[str, Any]] = queue.Queue()
        self._pending: Dict[str, Dict[str, Any]] = {}
        self._connection = mqtt_connection_builder.mtls_from_path(
            endpoint=endpoint,
            cert_filepath=certificate,
            pri_key_filepath=private_key,
            ca_filepath=root_ca,
            client_id=client_id or f"embedded-agent-{uuid.uuid4().hex[:12]}",
            clean_session=True,
            keep_alive_secs=30,
        )

    async def __aenter__(self):
        await asyncio.to_thread(self._connect_sync)
        return self

    async def __aexit__(self, exc_type, exc, traceback):
        await asyncio.to_thread(self._connection.disconnect().result)

    def _connect_sync(self):
        self._connection.connect().result()
        subscribe_future, packet_id  = self._connection.subscribe(
            topic=self.response_topic,
            qos=mqtt.QoS.AT_LEAST_ONCE,
            callback=self._on_message,
        )
        subscribe_result = subscribe_future.result()

    def _on_message(self, topic, payload, **kwargs):
        try:
            message = json.loads(payload.decode("utf-8"))
            if isinstance(message, dict):
                self._responses.put(message)
        except (UnicodeDecodeError, json.JSONDecodeError):
            return

    def _publish_sync(self, message: Dict[str, Any]):
        publish_future, packet_id = self._connection.publish(
            topic=self.request_topic,
            payload=json.dumps(message, ensure_ascii=False, separators=(",", ":")),
            qos=mqtt.QoS.AT_LEAST_ONCE,
        )
        publish_future.result()
        print(f"Published! Packet ID: {packet_id}")

    async def notify(self, method: str, params: Optional[Dict[str, Any]] = None):
        message: Dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        await asyncio.to_thread(self._publish_sync, message)

    async def request(
        self, method: str, params: Optional[Dict[str, Any]] = None
    ) -> Dict[str, Any]:
        request_id = uuid.uuid4().hex
        message: Dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
        }
        if params is not None:
            message["params"] = params
        await asyncio.to_thread(self._publish_sync, message)
        return await asyncio.to_thread(self._wait_response, request_id)

    def _wait_response(self, request_id: str) -> Dict[str, Any]:
        if request_id in self._pending:
            return self._pending.pop(request_id)

        deadline = time.monotonic() + self.timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"Sin respuesta MCP para id={request_id}")

            try:
                response = self._responses.get(timeout=remaining)
            except queue.Empty as exc:
                raise TimeoutError(
                    f"El MCP Server no respondió en {self.timeout} segundos. "
                    "Verifica que el ESP32 esté encendido y conectado a AWS IoT."
                ) from exc
            response_id = str(response.get("id", ""))
            if response_id == request_id:
                return response
            if response_id:
                self._pending[response_id] = response

    @staticmethod
    def _result(response: Dict[str, Any]) -> Any:
        if "error" in response:
            raise RuntimeError(json.dumps(response["error"], ensure_ascii=False))
        return response.get("result", {})

    async def initialize(self) -> Dict[str, Any]:
        response = await self.request(
            "initialize",
            {
                "protocolVersion": "2025-03-26",
                "capabilities": {},
                "clientInfo": {"name": "pc-ai-agent-mcp-aws", "version": "1.0.0"},
            },
        )
        result = self._result(response)
        await self.notify("notifications/initialized")
        return result

    async def list_tools(self) -> Any:
        return self._result(await self.request("tools/list", {}))

    async def call_tool(self, name: str, arguments: Dict[str, Any]) -> Any:
        return self._result(
            await self.request("tools/call", {"name": name, "arguments": arguments})
        )

    async def read_resource(self, uri: str) -> Any:
        return self._result(await self.request("resources/read", {"uri": uri}))
