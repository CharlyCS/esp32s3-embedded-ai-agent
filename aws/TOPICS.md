# Topics del Thing

Para un Thing llamado `embedded-agent-000001`:

```text
Supervisor → agente:
ai/agents/embedded-agent-000001/tasks

Agente → supervisor:
ai/agents/embedded-agent-000001/results
ai/agents/embedded-agent-000001/events
ai/agents/embedded-agent-000001/status
```

El mensaje en `tasks` es una asignación de alto nivel, no una llamada MCP:

```json
{
  "task_id": "task-0001",
  "goal": "Reproduce el audio de bienvenida",
  "priority": "normal",
  "context": {
    "source": "supervisor"
  }
}
```

El agente local decide qué tool MCP usar y devuelve el resultado final.
