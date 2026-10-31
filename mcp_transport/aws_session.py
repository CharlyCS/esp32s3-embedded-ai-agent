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
        thing_names: list[str],
        certificate: str,
        private_key: str,
        root_ca: str,
        client_id: Optional[str] = None,
        timeout: float = 30.0,
    ):
        #self.request_topic = f"ai/agents/{thing_name}/tasks"
        #self.response_topic = f"ai/agents/{thing_name}/results"
        self.timeout = timeout
        self._responses: queue.Queue[Dict[str, Any]] = queue.Queue()
        self._pending: Dict[str, Dict[str, Any]] = {}
        self.thing_names = list(thing_names)
        self._request_lock = asyncio.Lock()
        self._connected = False
        self._response_sources = {self._response_topic_for(name): name for name in self.thing_names}
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
    
    def _validate_destination(self, thing_name: str):
        if thing_name not in self.thing_names:
            raise ValueError(f"Dispositivo no permitido: {thing_name}")


    def _request_topic_for(self, thing_name: str) -> str:
        return f"ai/agents/{thing_name}/tasks"


    def _response_topic_for(self, thing_name: str) -> str:
        return f"ai/agents/{thing_name}/results"
        
    def _connect_sync(self):
        self._connection.connect().result()
        self._connected = True

        for thing_name in self.thing_names:
            subscribe_future, _ = self._connection.subscribe(
                topic=self._response_topic_for(thing_name),
                qos=mqtt.QoS.AT_LEAST_ONCE,
                callback=self._on_message,
            )
            subscribe_future.result()

        # subscribe_future, packet_id  = self._connection.subscribe(
        #     topic=self.response_topic,
        #     qos=mqtt.QoS.AT_LEAST_ONCE,
        #     callback=self._on_message,
        # )
        # subscribe_result = subscribe_future.result()

    def _disconnect_sync(self):
        if self._connected:
            try:
                self._connection.disconnect().result(timeout=self.timeout)
            finally:
                self._connected = False


    def _on_message(self, topic, payload, **kwargs):
        thing_name = self._response_sources.get(topic)
        if thing_name is None:
            return
        try:
            message = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        if (
            isinstance(message, dict)
            and message.get("jsonrpc") == "2.0"
            and "id" in message
            and ("result" in message or "error" in message)
        ):
            self._responses.put((thing_name, message))

    def _publish_sync(self, message: Dict[str, Any], thing_name: str):
        self._validate_destination(thing_name)
        if not self._connected:
            raise RuntimeError("La conexion MQTT no esta abierta")
        future, packet_id = self._connection.publish(
            topic=self._request_topic_for(thing_name),
            payload=json.dumps(message, ensure_ascii=False, separators=(",", ":")),
            qos=mqtt.QoS.AT_LEAST_ONCE,
        )
        future.result(timeout=self.timeout)
        print(f"Publicado para {thing_name}. Packet ID: {packet_id}")

    async def notify(
        self,
        method: str,
        params: Optional[Dict[str, Any]] = None,
        *,
        thing_name: str,
    ):
        self._validate_destination(thing_name)
        message: Dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        await asyncio.to_thread(self._publish_sync, message, thing_name)

    async def request(self, method: str, params: Optional[Dict[str, Any]] = None,*, thing_name: str) -> Dict[str, Any]:
        self._validate_destination(thing_name)
        async with self._request_lock:
            # Mantener el bloqueo hasta que termine el hilo, incluso si se
            # cancela el llamador: evita dos consumidores sobre la misma cola.
            task = asyncio.create_task(asyncio.to_thread(
                self._request_sync, method, params, thing_name
            ))
            try:
                return await asyncio.shield(task)
            except asyncio.CancelledError:
                while not task.done():
                    try:
                        await asyncio.shield(task)
                    except asyncio.CancelledError:
                        continue
                    except Exception:
                        break
                if not task.cancelled():
                    task.exception()
                raise

    def _request_sync(self, method, params, thing_name):
        # Descartar duplicados o respuestas atrasadas de llamadas anteriores.
        while True:
            try:
                self._responses.get_nowait()
            except queue.Empty:
                break
        request_id = uuid.uuid4().hex
        message: Dict[str, Any] = {
            "jsonrpc": "2.0", "id": request_id, "method": method,
        }
        if params is not None:
            message["params"] = params
        self._publish_sync(message, thing_name)
        return self._wait_response(request_id, thing_name)

    def _wait_response(self, request_id: str, thing_name: str) -> Dict[str, Any]:
        deadline = time.monotonic() + self.timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"Sin respuesta de {thing_name} para id={request_id}")
            try:
                source, response = self._responses.get(timeout=remaining)
            except queue.Empty as exc:
                raise TimeoutError(
                    f"{thing_name} no respondio en {self.timeout} segundos. "
                    "Verifica el ESP32, sus topics y los permisos AWS IoT."
                ) from exc
            if source == thing_name and response.get("id") == request_id:
                return response

    @staticmethod
    def _result(response: Dict[str, Any]) -> Any:
        if "error" in response:
            raise RuntimeError(json.dumps(response["error"], ensure_ascii=False))
        return response.get("result", {})

    async def initialize(self, *, thing_name: str) -> Dict[str, Any]:
        # Se conserva la version del protocolo utilizada por el firmware original.
        response = await self.request(
            "initialize",
            {
                "protocolVersion": "2025-03-26",
                "capabilities": {},
                "clientInfo": {"name": "pc-ai-agent-mcp-aws", "version": "1.0.0"},
            },
            thing_name=thing_name,
        )
        result = self._result(response)
        await self.notify("notifications/initialized", thing_name=thing_name)
        return result

    async def list_tools(self, *, thing_name: str) -> Any:
        return self._result(await self.request(
            "tools/list", {}, thing_name=thing_name
        ))

    async def call_tool(
        self, name: str, arguments: Dict[str, Any], *, thing_name: str
    ) -> Any:
        return self._result(await self.request(
            "tools/call", {"name": name, "arguments": arguments},
            thing_name=thing_name,
        ))

    async def read_resource(self, uri: str, *, thing_name: str) -> Any:
        return self._result(await self.request(
            "resources/read", {"uri": uri}, thing_name=thing_name
        ))