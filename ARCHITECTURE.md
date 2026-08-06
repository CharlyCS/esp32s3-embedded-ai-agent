# Arquitectura

```text
SUPERVISOR
  │
  │ high-level task, MQTT/TLS
  ▼
AWS IoT Core
  │ ai/agents/{ThingName}/tasks
  ▼
ESP32-S3
┌──────────────────────────────────────────────────────────┐
│ AWS transport                                           │
│      ↓                                                   │
│ Agent Runtime                                           │
│      ↓ DECIDE                                            │
│ MCP Client local                                        │
│      ↓ initialize / tools/list / tools/call              │
│ MCP Server local, mcp-c-sdk                              │
│      ↓                                                   │
│ Audio service / ES8311 / speaker                         │
│      ↑                                                   │
│ MCP result → Agent FINAL                                 │
└───────────────┬──────────────────────────────────────────┘
                │ ai/agents/{ThingName}/results
                ▼
            AWS IoT Core
                ▼
            SUPERVISOR
```

## Separación de protocolos

```text
Entre supervisor y agente:
MQTT/TLS por AWS IoT Core.

Dentro del agente ESP32:
MCP JSON-RPC real, en proceso y sin HTTP.

Diagnóstico dentro de la LAN:
HTTP MCP opcional en /mcp.
```

AWS no recibe `tools/call`. AWS entrega un objetivo. El agente local consulta
su MCP Server y selecciona la tool.

## Ejemplo

Entrada AWS:

```json
{
  "task_id": "task-0001",
  "goal": "Recibe al visitante con una bienvenida",
  "context": {}
}
```

Secuencia local:

```text
tools/list
→ decisión: self.audio.play_welcome
→ tools/call
→ reproducción
→ resultado MCP
→ respuesta final AWS
```
