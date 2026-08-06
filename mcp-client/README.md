# MCP Client y agente local

El cliente MCP está dentro del mismo firmware que el servidor MCP.

```text
AWS IoT task
    ↓
Agent Runtime
    ↓
DECIDE
    ↓
MCP Client local
    ↓ JSON-RPC en memoria
MCP Server local
    ↓
Tool física
    ↓
MCP response
    ↓
FINAL
    ↓
AWS IoT result
```

No usa `localhost`, sockets ni HTTP para el flujo interno. El cliente crea
mensajes MCP JSON-RPC reales (`initialize`, `tools/list`, `tools/call`) y los
entrega directamente al engine oficial de Espressif mediante una interfaz
sin red.

## Agent

El apartado `agent/` implementa el equivalente embebido del grafo Python:

```text
START → decide
          ├── tool_call → MCP Client → MCP Server → resultado
          └── final
        → respuesta AWS
```

Backends de decisión:

- OpenAI mediante el componente oficial `espressif/openai`, opcional.
- Motor local de reglas como fallback para operar y probar sin costo de API.

El agente consulta primero `tools/list`; por ello no mantiene una lista
separada de herramientas inventadas.
