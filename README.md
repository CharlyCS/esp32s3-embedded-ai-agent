# AI Agent + MCP Client + AWS IoT

Este proyecto ejecuta en la computadora:

- el AI Agent con OpenAI y LangGraph;
- el cliente MCP JSON-RPC;
- el transporte MQTT/TLS hacia AWS IoT Core.

El ESP32 contiene solamente el MCP Server.

## Flujo

`Usuario -> AI Agent -> MCP Client -> AWS IoT -> ESP32 MCP Server -> Tool`

## Instalación

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
```

Edita `.env` con tu API key, endpoint AWS y rutas de certificados. Usa un
certificado AWS IoT propio para la computadora; no reutilices la clave privada
del ESP32.

## Ejecución

```bash
python main.py
```

Al iniciar, el cliente realiza `initialize`, envía
`notifications/initialized`, obtiene `tools/list` y entrega esas herramientas
al AI Agent. Cuando el modelo selecciona una herramienta, ejecuta `tools/call`
y usa el resultado MCP para redactar la respuesta final.

## Topics

- Solicitudes: `ai/agents/{thing_name}/tasks`
- Respuestas: `ai/agents/{thing_name}/results`

Esta implementación transporta MCP sobre MQTT/TLS. No utiliza
`streamable_http_client`, porque AWS IoT Core no convierte MQTT en Streamable
HTTP. Para usar exactamente `streamable_http_client`, el cliente tendría que
conectarse directamente a `/mcp` o a un gateway HTTPS intermedio.

## Avances
