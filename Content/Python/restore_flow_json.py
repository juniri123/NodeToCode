# -*- coding: utf-8 -*-
from __future__ import annotations
from typing import Dict, Any, Tuple
from flow_model import Node, Step


def restore_flow_from_json(
    raw: Dict[str, Any]
) -> Tuple[Dict[str, Step], Step, Dict[str, Step], Dict[str, Node]]:
    """
    flow.json 을 기반으로 전체 구조를 복구한다.
    - Node 복원
    - Steps 복원
    - node 참조 연결
    - branches 참조 연결
    - Common Step 복원
    - entry step 결정
    """

    # --------------------------------------------------
    # 1) Nodes 복원
    #     raw["nodes"] = { node_name: { ...Node dict... }, ... }
    #     -> Node 객체로 복원하여 name → Node 맵 생성
    # --------------------------------------------------
    nodes: Dict[str, Node] = {}
    for node_name, node_dict in raw["nodes"].items():
        node_obj = Node.from_dict(node_dict)
        nodes[node_name] = node_obj

    # --------------------------------------------------
    # 2) Steps 복원
    #     raw["steps"] = { key: { ...Steps dict... }, ... }
    #     -> Step 객체로 복원하여 key → Steps 맵 생성
    # --------------------------------------------------
    steps: Dict[str, Step] = {}
    for step_key, step_dict in raw["steps"].items():
        step_obj = Step.from_dict(step_dict)
        steps[step_key] = step_obj

    # --------------------------------------------------
    # 3) Steps.node 연결
    #     Steps.node_name 을 기반으로 Node 객체 연결
    # --------------------------------------------------
    for step_key, step in steps.items():
        if step.node_name is not None:
            # node_name 기반 직접 매핑
            if step.node_name in nodes:
                step.node = nodes[step.node_name]
            else:
                # fallback: guid 기반 검색 (optional)
                for n in nodes.values():
                    if n.guid == step.node_guid:
                        step.node = n
                        break

    # --------------------------------------------------
    # 4) Steps.branches 연결
    #     Steps.branch_keys → 실제 Steps 객체 참조
    # --------------------------------------------------
    for step_key, step in steps.items():
        if step.branch_keys:
            step.branches = []
            for child_name in step.branch_keys:
                if child_name in steps:
                    step.branches.append(steps[child_name])

    # --------------------------------------------------
    # 5) Steps.next 연결
    #     Steps.next_keys → 실제 Steps 객체 참조
    # --------------------------------------------------
    for step_key, step in steps.items():
        if step.next_key:
            if step.next_key in steps:
                step.next = steps[step.next_key]

    # --------------------------------------------------
    # 6) Steps.common_placeholders 연결
    #     Steps.common_placeholder_keys → 실제 Steps 객체 참조
    # --------------------------------------------------
    for step_key, step in steps.items():
        if step.common_placeholder_keys:
            step.common_placeholders = []
            for ph_key in step.common_placeholder_keys:
                if ph_key in steps:
                    step.common_placeholders.append(steps[ph_key])

    # --------------------------------------------------
    # 7) Common Step 복원
    #     raw["common_step_keys"] = [key, key, ...]
    #     -> 해당 Steps 객체로 매핑
    # --------------------------------------------------
    common_steps: Dict[str, Step] = {}
    for key in raw.get("common_step_keys", []):
        if key in steps:
            common_steps[key] = steps[key]

    # --------------------------------------------------
    # 8) Entry Step 복원
    #     raw["entry_step_key"] = "K2Node_FunctionEntry_0"
    # --------------------------------------------------
    entry_key: str = raw["entry_step_key"]
    entry_step: Step = steps[entry_key]

    # -------------------------
    # 9) ★ 역파싱용 필드 제거 ★
    # -------------------------
    for step in steps.values():
        # 필요 없는 필드를 안전하게 삭제
        step.node_name = None
        step.node_guid = None
        step.branch_keys = None
        step.next_key = None
        step.common_placeholder_keys = None

    return steps, entry_step, common_steps, nodes
