import json
import os
from typing import Any, Dict, List, Literal, Optional, TypedDict

from dotenv import load_dotenv
from langgraph.graph import END, START, StateGraph
from openai import OpenAI


load_dotenv()

DEFAULT_SYSTEM_PROMPT = """
Eres un agente supervisor en español conectado a un ESP32 mediante MCP.
Analiza la solicitud del usuario y decide si debes utilizar una herramienta.

Reglas:
1. Responde con type="final" si no necesitas una herramienta.
2. Responde con type="tool_call" si necesitas una herramienta MCP.
3. Usa exactamente un nombre incluido en las herramientas disponibles.
4. Usa únicamente argumentos definidos por input_schema.
5. No inventes herramientas, argumentos ni resultados.
6. Si recibes mcp_result, genera una respuesta final basada en ese resultado.
7. Devuelve siempre un objeto JSON válido.
""".strip()


class AgentState(TypedDict, total=False):
    question: str
    available_tools: List[Dict[str, Any]]
    mcp_result: Optional[str]
    decision: Dict[str, Any]
    answer: str


class MCPAgent:
    def __init__(
        self,
        model: Optional[str] = None,
        system_prompt: str = DEFAULT_SYSTEM_PROMPT,
        api_key: Optional[str] = None,
    ):
        self.api_key = api_key or os.getenv("OPENAI_API_KEY")
        if not self.api_key:
            raise ValueError("Falta OPENAI_API_KEY en el archivo .env")

        self.model = model or os.getenv("OPENAI_MODEL", "gpt-4o-mini")
        self.system_prompt = system_prompt
        self.client = OpenAI(api_key=self.api_key)
        self.graph = self._build_graph()

    def _build_graph(self):
        graph = StateGraph(AgentState)
        graph.add_node("decide", self._decide)
        graph.add_node("answer", self._answer)
        graph.add_edge(START, "decide")
        graph.add_conditional_edges(
            "decide",
            self._route_after_decision,
            {"tool_call": END, "final": "answer"},
        )
        graph.add_edge("answer", END)
        return graph.compile()

    def _route_after_decision(
        self, state: AgentState
    ) -> Literal["tool_call", "final"]:
        decision = state.get("decision", {})
        if decision.get("type") == "tool_call" and not state.get("mcp_result"):
            return "tool_call"
        return "final"

    def _decide(self, state: AgentState) -> AgentState:
        prompt = f"""
Pregunta del usuario:
{state.get('question', '')}

Tools MCP disponibles:
{json.dumps(state.get('available_tools', []), ensure_ascii=False, indent=2)}

Resultado MCP previo:
{state.get('mcp_result') or 'No hay resultado MCP todavía.'}

Devuelve SOLO uno de estos objetos JSON:

{{
  "type": "tool_call",
  "tool_name": "nombre_exacto",
  "arguments": {{}},
  "reason": "explicación breve"
}}

o:

{{
  "type": "final",
  "answer": "respuesta final"
}}
"""
        content = self._call_openai_json(prompt)
        decision = self._safe_json_loads(content)

        if decision.get("type") == "tool_call":
            allowed = {tool["name"] for tool in state.get("available_tools", [])}
            if decision.get("tool_name") not in allowed:
                decision = {
                    "type": "final",
                    "answer": "No pude seleccionar una herramienta MCP válida.",
                }
            if not isinstance(decision.get("arguments", {}), dict):
                decision["arguments"] = {}

        if decision.get("type") not in {"tool_call", "final"}:
            decision = {"type": "final", "answer": content}

        return {**state, "decision": decision}

    def _answer(self, state: AgentState) -> AgentState:
        decision = state.get("decision", {})
        mcp_result = state.get("mcp_result")

        if decision.get("type") == "final" and decision.get("answer") and not mcp_result:
            return {**state, "answer": decision["answer"]}

        prompt = f"""
Pregunta original:
{state.get('question', '')}

Resultado obtenido mediante MCP:
{mcp_result}

Redacta una respuesta final clara en español. No menciones el grafo ni inventes
información que no esté en el resultado.
"""
        return {**state, "answer": self._call_openai_text(prompt)}

    def _call_openai_text(self, prompt: str) -> str:
        response = self.client.chat.completions.create(
            model=self.model,
            messages=[
                {"role": "system", "content": self.system_prompt},
                {"role": "user", "content": prompt},
            ],
            temperature=0.2,
        )
        return response.choices[0].message.content.strip()

    def _call_openai_json(self, prompt: str) -> str:
        response = self.client.chat.completions.create(
            model=self.model,
            messages=[
                {"role": "system", "content": self.system_prompt},
                {"role": "user", "content": prompt},
            ],
            temperature=0.1,
            response_format={"type": "json_object"},
        )
        return response.choices[0].message.content.strip()

    @staticmethod
    def _safe_json_loads(text: str) -> Dict[str, Any]:
        try:
            value = json.loads(text)
            return value if isinstance(value, dict) else {"type": "final", "answer": text}
        except json.JSONDecodeError:
            return {"type": "final", "answer": text}

    def invoke(
        self,
        question: str,
        available_tools: Optional[List[Dict[str, Any]]] = None,
        mcp_result: Optional[str] = None,
    ) -> Dict[str, Any]:
        result = self.graph.invoke(
            {
                "question": question,
                "available_tools": available_tools or [],
                "mcp_result": mcp_result,
            }
        )
        decision = result.get("decision", {})
        if decision.get("type") == "tool_call" and not mcp_result:
            return {
                "type": "tool_call",
                "tool_name": decision.get("tool_name"),
                "arguments": decision.get("arguments", {}),
                "reason": decision.get("reason", ""),
            }
        return {
            "type": "final",
            "answer": result.get("answer", decision.get("answer", "")),
        }
