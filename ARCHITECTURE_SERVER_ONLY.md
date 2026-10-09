# Arquitectura MCP Server-only

```text
PC MCP Client
    | MCP JSON-RPC 2.0 sobre MQTT/TLS
    v
AWS IoT Core
    | ai/agents/{thing}/tasks
    v
ESP32-S3 MCP Server -> tools físicas
    |
    | ai/agents/{thing}/results
    v
AWS IoT Core -> PC MCP Client
```

El firmware ya no contiene `mcp-client` ni el agente local. AWS entrega al
ESP32 solicitudes MCP completas (`initialize`, `tools/list`, `tools/call`, etc.)
y el adaptador MQTT las pasa al motor del servidor. El agente se encuentra dentro del MCP CLIENT y el MCP SERVER solo tiene inicado los parametros JSON RPC.

El endpoint `/mcp` se conserva como transporte HTTP MCP directo para la LAN.
No forma parte del flujo MQTT. El servidor HTTP incluido por el SDK no termina
TLS; HTTPS requiere un proxy/gateway o implementar `esp_https_server`.
