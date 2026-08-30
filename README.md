# ESP32-S3 Embedded AI Agent VERSION 01

Proyecto ESP-IDF para **un único AWS IoT Thing** usando la
**Waveshare ESP32-S3-AUDIO-Board**.

## Flujo implementado

```text
Supervisor
  ↓ objetivo por AWS IoT Core
Agent Runtime del ESP32
  ↓
MCP Client local
  ↓
MCP Server local
  ↓
Tool física
  ↓
MCP Client
  ↓
Agent Runtime
  ↓ resultado listo por AWS IoT Core
Supervisor
```

AWS IoT Core no transporta MCP en este diseño. Transporta las tareas de alto
nivel y sus resultados. MCP funciona dentro del ESP32 entre el agente y las
capacidades locales.

## Carpetas solicitadas

```text
esp32_s3_embedded_ai_agent/
├── aws/
│   ├── certs/
│   ├── policy/
│   └── aws_iot_agent.c
├── data/
│   └── audio/
├── scripts/
├── mcp-server/
├── mcp-client/
│   └── agent/
└── main/
```

## Certificados

El ZIP no incluye tus credenciales privadas. Instálalas con:

```bash
python scripts/install_certificates.py \
  --root-ca "/ruta/AmazonRootCA1.pem" \
  --certificate "/ruta/device-certificate.pem.crt" \
  --private-key "/ruta/device-private.pem.key"
```

## Agente similar al ejemplo Python

El código Python proporcionado usa:

```text
decide → tool_call/final → respuesta
```

Este proyecto implementa:

```text
Agent Runtime
├── consulta tools/list al MCP Server local;
├── decide con OpenAI o fallback local;
├── ejecuta tools/call mediante el MCP Client local;
├── recibe el resultado MCP;
└── publica la respuesta final por AWS.
```

No usa LangGraph porque el firmware es C/FreeRTOS, pero conserva la misma
máquina de estados.

## OpenAI opcional

El agente puede usar `espressif/openai` con Chat Completions y
`gpt-4o-mini`. Está desactivado por defecto para que puedas probar toda la
arquitectura sin consumir API.

Sin OpenAI, el fallback reconoce objetivos de:

```text
bienvenida
audio inicial
detener audio
volumen
estado/diagnóstico
```

## MCP completo en el ESP32

El servidor usa `espressif/mcp-c-sdk 2.0.1` y registra:

```text
tools/list
tools/call
resources/list
resources/read
prompts/list
prompts/get
initialize
ping
```

Las capacidades concretas son:

```text
self.get_device_status
self.audio.play_startup
self.audio.play_welcome
self.audio.stop
self.audio.set_volume
device://status
welcome.plan
```

## HTTP MCP opcional

Se conserva:

```text
http://IP_DEL_ESP32/mcp
```

para diagnóstico o agentes de monitoreo dentro de la LAN.

El agente embebido no usa HTTP. Su MCP Client se comunica en proceso con el
MCP Server, evitando TCP, encabezados HTTP y latencia de red.

## AWS configurado

```text
Account ID: 730335216238
Region: us-east-1
Thing Group: arch-embedded-agents
Topic prefix: ai/agents
```

La política está en:

```text
aws/policy/ArchEmbeddedAgentsPolicy.json
```

## Inicio

Sigue:

[SETUP_STEP_BY_STEP.md](SETUP_STEP_BY_STEP.md)
