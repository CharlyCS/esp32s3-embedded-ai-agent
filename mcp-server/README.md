# MCP Server local

Este componente contiene el servidor MCP real del ESP32.

Registra:

```text
Tools
├── self.get_device_status
├── self.audio.play_startup
├── self.audio.play_welcome
├── self.audio.stop
└── self.audio.set_volume

Resource
└── device://status

Prompt
└── welcome.plan
```

Tiene dos accesos:

1. **Interno y rápido:** el MCP Client local llama
   `mcp_server_process_local_json()`. No utiliza HTTP ni red.
2. **HTTP opcional para diagnóstico:** `http://IP_DEL_ESP32/mcp`.

Los dos engines registran las mismas capacidades físicas. El endpoint HTTP
no se usa en el flujo AWS → agente → MCP local; queda disponible para una
computadora de monitoreo dentro de la LAN.
