# Máquina de estados del agente

Equivalencia con el ejemplo Python/LangGraph:

| Python | ESP-IDF |
|---|---|
| `AgentState` | estructura de tarea y `agent_decision_t` |
| nodo `decide` | `agent_openai_decide()` o regla fallback |
| `available_tools` | respuesta real de `tools/list` |
| `tool_call` | `local_mcp_client_call_tool()` |
| `mcp_result` | respuesta JSON-RPC del MCP Server local |
| nodo `answer` | segunda decisión opcional o respuesta estructurada |
| `graph.invoke()` | worker FreeRTOS de `agent_runtime.c` |

El ESP32 no ejecuta Python ni LangGraph. Conserva el mismo patrón lógico en
una máquina de estados C, adecuada para memoria limitada.
