# -*- coding: utf-8 -*-
from __future__ import annotations
import json
import argparse
from typing import List, Dict
import zipfile
from flow_model import Pin, Link, Node, Step, MergingGroup

def print_step(debug: bool, current_step: Step, stack: List[Step]):
    if (not debug):
        return

    current_node: Node = current_step.node

    print("\n==============================")
    print(f"Visiting Node: {current_node.name} #{current_node.guid}")
    print(f"LogicDepth={current_step.logic_depth}, Outlink Idx={current_step.outlink_idx}, Outlink Count = {len(current_node.exec_out_links)}")
    print(f"Outlinks To: {[f'{o.to_pin.node_name} #{o.to_pin.node_guid} ({o.to_pin.name} #{o.to_pin.guid})' for o in current_node.exec_out_links]}")
    print(f"From Pins: {[f'{p.node_name} #{p.node_guid} ({p.name} #{p.guid})' for p in current_step.from_pins]}")
    print(f"Stack Depth: {len(stack)}")
    print("==============================")

def print_complete_step(debug: bool, current_step: Step, stack: List[Step]):
    if not debug:
        return

    print(f"Completed node {current_step.node.name}, popping stack")
    print(f"Stack Depth: {len(stack)}")

def print_broken_link(debug: bool, current_step: Step, stack: List[Step]):
    if not debug:
        return

    link = current_step.outlink
    next_node_name = link.to_pin.node_name
    print(f"[WARN] Broken link: {current_step.node.name} → {next_node_name}")

def print_link(debug: bool, link: Link, steps: Dict[str, Step]):
    if not debug:
        return

    next_node_name: str = link.to_pin.node_name
    next_node_guid: str = link.to_pin.node_guid
    next_stack_frame: Step = steps[next_node_name]

    print(f"Following link [Pin: {link.from_pin.name}#{link.from_pin.guid}] → [Node {next_node_name}#{next_node_guid}]")
    print(f"Next Node Info: {next_node_name} #{next_node_guid}, from_pins count={len(next_stack_frame.from_pins)}")

def print_common_step_registered(debug: bool, common_step: Step):
    if debug:
        print(f"Step {common_step.node.name} does not register in common step, registering it as common step.")

def print_common_step_already_registered(debug: bool, common_step: Step):
    if debug:
        print(f"Step {common_step.node.name} already registered in common steps.")

def  print_start_parsing_common_step(debug: bool, common_step: Step):
    if debug:
        print()
        print(f"==============================> START common step {common_step.node.name}")

def  print_end_parsing_common_step(debug: bool, common_step: Step):
    if debug:
        print()
        print(f"<============================== END common step {common_step.node.name}")

def  print_creating_common_step_placeholder(debug: bool, step: Step):
    if debug:
        print(f"Created common step placeholder for {step.node.name}")

# 실행 흐름(Exec Flow)을 구성한다. DFS
def build_exec_flow(nodes: Dict[str, Node], steps: Dict[str, Step], common_steps: Dict[str, Step], entry: Step, debug: bool):
    # DFS 상태를 유지하는 스택
    stack: List[Step] = [entry]
    logic_depth: int = entry.logic_depth

    while stack:
        current_step: Step = stack[-1]
        print_step(debug, current_step, stack)
        # current step의 outlink idx를 1 증가
        current_step.next_outlink_idx()

        #-----------------------------------------------------------------------
        # current step의 outlink를 모두 처리 했다면, 스택에서 제거 처리
        #-----------------------------------------------------------------------
        if current_step.has_all_outlink_processed:
            print_complete_step(debug, current_step, stack)
            stack.pop()
            logic_depth = current_step.pop_logic_depth(logic_depth)
            continue

        #-----------------------------------------------------------------------
        # 처리해야 할 outlink가 남아 있다면,
        #-----------------------------------------------------------------------
        link: Link = current_step.outlink
        next_node_name: str = link.to_pin.node_name
        next_step: Step = steps.get(next_node_name)

        # 다음 Step이 유효하지 않을 때, 예외 처리.
        if not next_step:
            print_broken_link(debug, current_step, stack)
            raise ValueError(f"Broken link: { current_step.node.name} → {next_node_name}")

        # next step 업데이트
        next_step.append_from_pin(link)
        print_link(debug, link, steps)

        # 다음이 머지되는 스텝이라면, Common Logic 표시용 placeholder로 대체
        if next_step.is_common_step:
            # 이미 common steps로 등록된 경우
            if next_node_name in common_steps:
                print_common_step_already_registered(debug, next_step)

            # 아직 common flow로 등록되지 않은 경우
            else:
                print_common_step_registered(debug, next_step)
                # Comomon step 맵에 등록
                common_steps[next_node_name] = next_step

                next_step.make_as_entry(0)
                print_start_parsing_common_step(debug, next_step)
                build_exec_flow(nodes, steps, common_steps, next_step, debug)
                print_end_parsing_common_step(debug, next_step)

            # Common Logic 표시용 placeholder 생성
            common = Step()
            common.key = f"{next_node_name}_PlaceHolder_{len(next_step.from_pins) - 1}"
            common.node = next_step.node
            common.append_from_pin(link)
            common.is_common_placeholder = True
            common.is_branched = current_step.has_branches
            common.logic_depth = logic_depth
            steps[common.key] = common
            print_creating_common_step_placeholder(debug, next_step)
            next_step.record_common_placeholder(common)

            # 실행 흐름 연결
            if common.is_branched:
                current_step.append_branch(common)
            else:
                current_step.set_next(common)

        # 다음이 머지되는 스텝이 아니라면,
        else:
            next_step.logic_depth = logic_depth
            next_step.is_branched = current_step.has_branches

            # 실행 흐름 연결
            if next_step.is_branched:
                current_step.append_branch(next_step)
            else:
                current_step.set_next(next_step)

            # 자식 노드를 스택에 push
            stack.append(next_step)
            logic_depth = next_step.push_logic_depth(logic_depth)

# 주어진 step 의 부모(step.parent)를 따라 FunctionEntry 까지 올라가며, parent-chain 리스트를 bottom → top 방향으로 반환한다.
def get_parent_chain(step: Step, step_by_key: Dict[str, Step]) -> List[Step]:
    # from pins가 여러 개인 것들은 common step으로 등록하고,
    # common placeholder를 새로 만들어 넣었기 때문에, (같은 common step을 가르키는 placholder들이 생성됨)
    # 실행 흐름을 flatten 했고, 부모가 1개임을 보장한다.
    # 따라서 from_pin = cur.from_pins[0]; 이런 코드가 유효함
    chain = []

    cur = step
    while cur is not None:
        chain.append(cur)
        if len(cur.from_pins) > 0:
            from_pin = cur.from_pins[0];
            cur = step_by_key[from_pin.node_name]
        else:
            cur = None

    return chain

def find_lca(placeholders: List[Step], step_by_key: Dict[str, Step]) -> Step:
    # 각 placeholder 의 parent chain 가져오기 (child→root)
    chains = [get_parent_chain(ph, step_by_key) for ph in placeholders]

    # 각 체인을 root→child 로 뒤집기
    revs = [list(reversed(chain)) for chain in chains]

    # 가장 짧은 체인 길이
    min_len = min(len(c) for c in revs)

    lca = None
    for i in range(min_len):
        cand = revs[0][i]
        if all(c[i] is cand for c in revs):
            lca = cand
        else:
            break
    return lca

# 같은 common step의 placeholder를 대상으로 처리 한다.
def build_placeholder_groups(all_placeholders: List[Step], step_by_key: Dict[str, Step]) -> List[MergingGroup]:
    # parent-chain 수집: {step: "A B C ..."}
    callstackline_by_key: Dict[Step, str] = {}

    for one in all_placeholders:
        chain = get_parent_chain(one, step_by_key)
        chain.pop(0) #자기 자신은 제외
        callstackline_by_key[one] = "/".join(s.node.name for s in chain)

    # 대표 placeholder를 담을 통
    leader_placeholders: List[Step] = []
    # candidate를 하나 뽑아서, 다른 placeholder들과 비교하여야 한다.
    # 이 때, 다른 placeholder의 콜스택에 포함되면 리더 placeholder가 아닌다.
    for candidate in all_placeholders:
        candidate_callstak = callstackline_by_key[candidate]

        # 자기 자신을 제외한 나머지 placeholder들의 콜스택과 비교
        is_leader = True
        for one in all_placeholders:
            if one is candidate:
                continue
            other_callstack = callstackline_by_key[one]

            # 보통은 candidate의 콜스택이 다른 placeholder의 callstack에 포함되면 candidate가 리더가 될 수 없지만
            if candidate_callstak in other_callstack:
                is_leader = False

                # 두개의 콜 스택이 동일하고 leader_placeholders에 아직 안들어 있다면, candidate를 리더로 봐줘야 한다.
                # 설명하기 어려운데 DFS 탐색되기 때문에, 깊이 우선 탐색 방향으로 순회를 하게 되고
                # 바로 윗 부모에서 시작하는 콜스택이 완벽하게 동일하다는 말은
                # 두 placeholder가 sibling일 때 가능한데, 이 둘중에 하나는 리더가 되어야 하기 때문이다.
                # 아래 조건이 없으면 sibling인 것들은 리더 placeholder를 못 찾는다.
                if candidate_callstak == other_callstack:
                    if one not in leader_placeholders and candidate not in leader_placeholders:
                        is_leader = True
                break

        if is_leader:
            leader_placeholders.append(candidate)

    # 그룹 초기화
    placeholder_groups: Dict[Step, List[Step]] = {leader: [] for leader in leader_placeholders}

    # 그룹 내용 채우기
    for one in all_placeholders:
        # 리더는 그룹에 넣기만 하면 되고
        if one in leader_placeholders:
            placeholder_groups[one].append(one)
            continue

        # 리더가 아닌 경우에, parent str이 어떤 리더에 포함되는지 확인하고 그룹에 추가한다.
        callstackline = callstackline_by_key[one]
        for leader in leader_placeholders:
            leader_callstackline = callstackline_by_key[leader]
            if callstackline in leader_callstackline:
                placeholder_groups[leader].append(one)
                break


    merging_groups: List[MergingGroup] = []
    for leader, placeholders in placeholder_groups.items():
        common_step: Step = step_by_key[leader.node.name]
        lca: Step = find_lca(placeholders, step_by_key)

        mg: MergingGroup = None
        # 여기서 fall through 처리 해야 하나?
        # lca.key.lower()가 swtich를 포함하고 있고, 각 플레이스 홀더들의 콜스택이 동일하고
        # default 핀에 대한 placeholder가 포함되어 있지 않는 경우를 찾는 함수를 만들고 싶음
        if is_switch_fallthrough_case(lca, placeholders, step_by_key):
            mg = MergingGroup(common_step, placeholders[len(placeholders) - 1], placeholders, True)
        else:
            mg = MergingGroup(common_step, lca, placeholders, False)
        merging_groups.append(mg)

    return merging_groups

def is_switch_fallthrough_case(lca: Step,
                               placeholders: List[Step],
                               step_by_key: Dict[str, Step]) -> bool:
    """
    조건:
      1) lca 노드 이름에 switch 포함
      2) placeholder 들의 parent-chain 문자열(callstack)이 모두 동일
      3) default branch placeholder 가 포함되지 않음
    """

    # --- 조건 1) LCA가 switch 계열인지 확인 ---
    if lca is None or lca.node is None:
        return False

    if "switch" not in lca.node.name.lower():
        return False

    # --- 조건 2) placeholder 들의 callstackline 이 동일한지 확인 ---
    callstacks = []
    for ph in placeholders:
        chain = get_parent_chain(ph, step_by_key)
        chain.pop(0)  # 자기 자신 제거
        callstacks.append("/".join(s.node.name for s in chain))

    # 모두 동일한지 확인
    if len(set(callstacks)) != 1:
        return False

    # --- 조건 3) default branch placeholder 배제 ---
    for ph in placeholders:
        for pin in ph.from_pins:
            # 정확히 "Default" 핀이 아님
            if pin.name.lower() == "default":
                return False

    return True


# placeholder 그룹을 기반으로 merging_point step 들을 생성하고 트리에 연결한다.
def create_merge_points(merging_groups: List[MergingGroup], step_by_key: Dict[str, Step]) -> List[Step]:
    created_merge_points: List[Step] = []

    # todo. 부모가 스위치 이면 중단. 예외적으로 swtich는 fall through 가 되어야 하는데,
    # fall through까지 따져서 만들어 줘야 하나...
    # 일단은 아래 for문에서 switch에 머징 포인트가 걸리면 스킵한다.

    for group in merging_groups:
        merging_point_step: Step = group.merging_point_step
        placeholders: List[Step] = group.placeholders
        common_step: Step = group.common_step

        merge_point: Step = None
        # todo. 아래 조건이 하나 더 있어야 함. 모든 플레이스 홀더의 경로가 같아야 하고, default도 같은 경로여야 함.
        if group.is_fallthough:
            merge_point = create_fallthrough_merge_point(step_by_key, merging_point_step, placeholders, common_step)
        else:
            merge_point= create_normal_merge_point(step_by_key, merging_point_step, placeholders, common_step)

        if merge_point:
            created_merge_points.append(merge_point)

    return created_merge_points

def create_normal_merge_point(step_by_key: Dict[str, Step], merging_point_candidate: Step, placeholders: List[Step], common_step: Step) -> Step:
    # 그루핑된 인덱스 검색
    indices = [
        common_step.common_placeholders.index(ph)
        for ph in placeholders
    ]
    grouped_idxes = "|".join(str(i) for i in indices)

    # --- 1) 기존 parent.next 저장 ---
    old_next = merging_point_candidate.next

    # --- 2) merge point 생성 ---
    merge_point = Step()
    merge_point.key = f"{common_step.key}_Merging_[{grouped_idxes}]"
    merge_point.node = common_step.node
    merge_point.is_merging_point = True
    merge_point.from_pins.append(Pin("", "", merging_point_candidate.key, ""))

    # logic depth: old_next 있을 때만 복사
    if old_next is not None:
        merge_point.logic_depth = old_next.logic_depth
    else:
        merge_point.logic_depth = merging_point_candidate.logic_depth

    # step registry 등록
    step_by_key[merge_point.key] = merge_point

    # --- 3) parent.next = merge_point 로 교체 ---
    merging_point_candidate.next = merge_point

    # --- 4) merge_point.next = old_next ---
    merge_point.next = old_next

    # --- 5) old_next 의 from_pins 에서 parent → merge_point 로 변경 ---
    if old_next is not None:
        for pin in old_next.from_pins:
            if pin.node_name == merging_point_candidate.key:
                pin.node_name = merge_point.key

    # --- 6) placeholder 들 comment-out 처리 ---
    for ph in placeholders:
        merge_point.record_common_placeholder(ph)
        ph.is_comment_out = True

    return merge_point

def create_fallthrough_merge_point(step_by_key: Dict[str, Step], merging_point_candidate: Step, placeholders: List[Step], common_step: Step) -> Step:
    # 그루핑된 인덱스 검색
    indices = [
        common_step.common_placeholders.index(ph)
        for ph in placeholders
    ]
    grouped_idxes = "|".join(str(i) for i in indices)

    # 이 경우에 마지막 placeholder가 candidate로 오는데, next는 없다.

    # --- 2) merge point 생성 ---
    merge_point = Step()
    merge_point.key = f"{common_step.key}_Merging_[{grouped_idxes}]"
    merge_point.node = common_step.node
    merge_point.is_merging_point = True
    merge_point.from_pins.append(Pin("", "", merging_point_candidate.key, ""))

    # logic depth
    merge_point.logic_depth = merging_point_candidate.logic_depth + 1

    # step registry 등록
    step_by_key[merge_point.key] = merge_point

    #
    merging_point_candidate.next = merge_point

    # --- 6) placeholder 들 comment-out 처리 ---
    for ph in placeholders:
        merge_point.record_common_placeholder(ph)
        ph.is_comment_out = True
        ph.is_fallthrough = True

    return merge_point

# 주어진 step을 기준으로 하는 Tree에서 is_common_placeholder를 수집한다.
def collect_common_placeholders(step: Step, step_by_key: Dict[str, Step], placeholders_by_commonkey: Dict[str, List[Step]]):
    """
        placeholders_by_commonkey: Common step key별 place holder list를 담는 통
    """
    if step is None:
        return

    # placeholder 발견
    if step.is_common_placeholder:
        common_step = step_by_key[step.node.name] # placeholder가 가르키는 common step 원본
        placeholders_by_commonkey.setdefault(common_step.key, []).append(step)

    # branches
    for branch in step.branches:
        collect_common_placeholders(branch, step_by_key, placeholders_by_commonkey)

    # next
    if step.next:
        collect_common_placeholders(step.next, step_by_key, placeholders_by_commonkey)


def resolve_merging_points_for_entry(entry: Step, step_by_key: Dict[str, Step], debug: bool):
    # { commonstep_key : [common_placeholder_step, .....], ...} 의 형식임.
    placeholders_by_commonkey: Dict[str, List[Step]] = {}
    collect_common_placeholders(entry, step_by_key, placeholders_by_commonkey)

    for commonkey, placeholders in placeholders_by_commonkey.items():
        # placeholder들 중에 서로 묶일 수 있을 만한 그루핑
        merging_groups: List[MergingGroup] = build_placeholder_groups(placeholders, step_by_key)
        if debug:
            print("\n[DEBUG] Placeholder Groups:")
            for mg in merging_groups:
                print(f"  Merging point - {mg.merging_point_step.key} for ...")
                for ph in mg.placeholders:
                    print(f"    - {ph.key}")

        # 그루핑 별로 머지 포인트 찾기
        merge_points: List[Step] = create_merge_points(merging_groups, step_by_key)
        if debug:
            print("\n[DEBUG] Merge Point created:")
            for mp in merge_points:
                print(f"  merging point step key: {mp.key}")
                print(f"  common step key: {mp.node.name}")
                for ph in mp.common_placeholders:
                    print(f"      - from placeholder {ph.key}")


def print_single_step(step: Step, with_indent: bool) -> List[str]:
    lines = []
    node = step.node

    if step is None:
        return lines
    indent = "│   " * step.logic_depth if with_indent else ""

    # --------------------------------------
    # 현재 노드 정보 구성
    # --------------------------------------
    node = step.node
    if not node:
        return lines

    # 여러 입력 핀 이름을 쉼표로 구분해 표시
    # 아래와 같이 표시됨.
    # 📌then::2A4423FF4FB34347A11FFE82928022D9 from (📋K2Node_VariableSet_1::849BB46A44885F7038553284EDA7183F)
    # ➡️ 📌then::2A4423FF4FB34347A11FFE82928022D9 from (📋K2Node_VariableSet_1::849BB46A44885F7038553284EDA7183F)
    # 참고) from_pins가 여러 개인 경우 (step.is_merged = true), common step임
    branch_label = ""
    if step.from_pins:
        prefix_icon = "➡️ " if step.is_branched else ""
        branch_label = ", ".join([f"{prefix_icon}📌{p.name}::{p.guid} from (📋{p.node_name}::{p.node_guid})" for p in step.from_pins])

    comment_out = '//' if step.is_comment_out else ""

    # --------------------------------------
    # Common Logic Placeholder 처리
    # --------------------------------------
    if step.is_common_placeholder:
        if step.is_branched:
            # 아래과 같이 표시됨
            # │   │   │   │   ➡️ 📌false::4993A1884B2A68C0739B769DBBC844BF from (📋K2Node_IfThenElse_4::2F7C0CF74EAF1D3615517D94219B1700)
            # │   │   │   │   │   ↪️ Placeholder for 📋K2Node_CallFunction_22::62A8B15646056B6DD281A883B0D709C1
            lines.append(f"{indent}{branch_label}")
            commonstep_indent = "│   " * (step.logic_depth + 1)
            lines.append(f"{commonstep_indent}{comment_out}↪️ Placeholder::{step.key} for 📋{node.name}::{node.guid}")
        else:
            # 아래과 같이 표시됨
            # 📌then::D0B8C99240979410512A18A1E1F29AAF from (📋K2Node_CallFunction_7::49BDF9DF412E94633DD43DAD72F2525C) →  ↪️ Placeholder for 📋K2Node_CallFunction_22::62A8B15646056B6DD281A883B0D709C1
            lines.append(f"{indent}{comment_out}{branch_label} → ↪️ Placeholder::{step.key} for 📋{node.name}::{node.guid}")
        return lines

    # --------------------------------------
    # is_common_step 인 경우, (common step인 경우 처리)
    # 아래와 같이 표시됨
    # ----- From Pins -----
    # 📌then::D0B8C99240979410512A18A1E1F29AAF from (📋K2Node_CallFunction_7::49BDF9DF412E94633DD43DAD72F2525C)
    # 📌false::4993A1884B2A68C0739B769DBBC844BF from (📋K2Node_IfThenElse_4::2F7C0CF74EAF1D3615517D94219B1700)
    # ---------------------
    # 📋K2Node_CallFunction_22::62A8B15646056B6DD281A883B0D709C1
    # --------------------------------------
    if step.is_common_step:
        lines.append(f"{indent}----- From Pins -----")
        for p in step.from_pins:
            lines.append(f"{indent}📌{p.name}::{p.guid} from (📋{p.node_name}::{p.node_guid})")
        lines.append(f"{indent}----- Placeholders -----")
        for p in step.common_placeholders:
            lines.append(f"{indent}{p.key}")
        lines.append(f"{indent}---------------------")
        lines.append(f"{indent}📋{node.name}::{node.guid}")

    # --------------------------------------
    # todo. mergind point 출력, 아이콘 등 정리가 필요함.
    # --------------------------------------
    elif step.is_merging_point:
        lines.append(f"{indent}Merging Point {step.key}")
        lines.append(f"{indent}----- Merged Placeholders -----")
        for p in step.common_placeholders:
            lines.append(f"{indent}{p.key} (📋{p.node.name}::{p.node.guid})")
        lines.append(f"{indent}---------------------")
    # --------------------------------------
    # is_branched 인 경우, (부모의 exec_outpin이 여러 개인 경우)
    # 아래와 같이 표시됨
    # │   ➡️ 📌Is Valid::8EA0C0824B56B31B3DB44B9D88578E64 from (📋K2Node_MacroInstance_1::122D45054CFE1EB17F12A18FEC4D156A) <== 분기 표현
    # │   │   📋K2Node_VariableSet_0::495E764A4160B86313C32DB84295F9E7                                                       <== 엔트리 표현
    # --------------------------------------
    elif step.is_branched:
        lines.append(f"{indent}{branch_label}")
        branched_indent = "│   " * (step.logic_depth + 1)
        lines.append(f"{branched_indent}{comment_out}📋{node.name}::{node.guid}")

    # --------------------------------------
    # 일반적인 경우,
    # --------------------------------------
    # 아래와 같이 표시됨
    # 📌then::BEC30DEC449B78C7A3AD8E8ACDE8047A from (📋K2Node_FunctionEntry_0::BD218B894B2A57EF18407CA99042A11B) → 📋K2Node_VariableSet_1::849BB46A44885F7038553284EDA7183F
    else:
        lines.append(f"{comment_out}{indent}{branch_label} → 📋{node.name}::{node.guid}")

    return lines

# 실행 흐름을 문자열 리스트로 출력한다.
def print_steps(step: Step) -> List[str]:
    lines: List[str] = []

    if step is None:
        return lines

    # step print
    lines.extend(print_single_step(step, with_indent=True))

    # 브랜치 처리 후 다음 스텝 처리
    for child in step.branches:
        lines.extend(print_steps(child))
    lines.extend(print_steps(step.next))
    return lines

# common_steps에 저장된 모든 step을 순회하며 print_flow로 출력한다.
def print_common_steps(common_steps: Dict[str, Step]) -> List[str]:
    lines: List[str] = []

    if not common_steps:
        lines.append("[INFO] No common step to display.")
        return lines

    num: int  = 0
    for stack_frame in common_steps.values():
        lines.append(f"[#{num}]")
        lines.extend(print_steps(stack_frame))
        lines.append("")
        num = num + 1

    return lines

def init_nodes(data) -> Dict[str, Node]:
    # 모든 노드와 핀 정보를 저장할 딕셔너리
    nodes: Dict[str, Node] = {}
    pin_lookup: Dict[str, str] = {}  # pin_id -> pin_name

    # 1️⃣ 먼저 모든 핀 정보 / 노드 정보를 수집
    for n in data["Nodes"]:
        node = Node(n["Name"], n["NodeGuid"])
        nodes[node.name] = node
        for pin in n.get("Pins", []):
            pin_id = pin.get("PinId", "")
            pin_name = pin.get("PinFriendlyName") or pin.get("PinName", "")
            pin_lookup[pin_id] = pin_name  # ✅ 핀 이름만 저장

    # 2️⃣ 실제 Link 구성
    for n in data["Nodes"]:
        node = nodes[n["Name"]]

        # 핀 정보 순회
        for pin in n.get("Pins", []):
            pin_type = pin.get("PinType", {}).get("PinCategory")

            # 로컬 핀 복구
            pin_name: str = pin.get("PinFriendlyName") or pin.get("PinName", "")
            pin_guid: str = pin.get("PinId", "")
            pin_direction: str = pin.get("Direction")
            local_pin: Pin = Pin(pin_name, pin_guid, node.name, node.guid)

            # 로컬 핀 정보 캐시(링크여부 상관 없음.)
            if pin_type == "exec":
                if pin_direction == "EGPD_Output":
                    node.exec_out_pins.append(local_pin)
                else:
                    node.exec_in_pins.append(local_pin)
            else:
                if pin_direction == "EGPD_Output":
                    node.data_out_pins.append(local_pin)
                else:
                    node.data_in_pins.append(local_pin)

            # 핀의 링크 정보 순회
            for link in pin.get("LinkedTo", []):
                # 리모트 핀 정보 복구
                linked_pin_guid: str = link.get("PinId", "")
                linked_pin_name: str = pin_lookup.get(linked_pin_guid, "(unknown)")
                linked_node = nodes[link.get("Node", "")]
                remote_pin: Pin = Pin(linked_pin_name, linked_pin_guid, linked_node.name, linked_node.guid)

                # 방향에 따라 Link 생성
                if pin_type == "exec":
                    if pin_direction == "EGPD_Output":
                        node.exec_out_links.append(Link(local_pin, remote_pin))
                    else:
                        node.exec_in_links.append(Link(remote_pin, local_pin))
                else:
                    if pin_direction == "EGPD_Output":
                        node.data_out_links.append(Link(local_pin, remote_pin))
                    else:
                        node.data_in_links.append(Link(remote_pin, local_pin))
    return nodes

def init_steps(nodes: Dict[str, Node]) -> Dict[str, Step]:
    """모든 노드에 대한 step을 미리 생성하여 name을 키로 매핑 반환"""
    steps: Dict[str, Step] = {}

    for n in nodes.values():
        frame = Step()
        frame.node = n
        frame.key = n.name
        steps[n.name] = frame

    return steps

def parse_flow(data, debug):
    nodes: Dict[str, Node] = init_nodes(data)
    steps = init_steps(nodes)
    entry_node = next((n for n in nodes.values() if "K2Node_FunctionEntry" in n.name), None)
    common_steps: Dict[str, Step] = {}

    if entry_node:
        entry_step = steps[entry_node.name]
        entry_step.make_as_entry(0)
        build_exec_flow(nodes, steps, common_steps, entry_step, debug)
    else:
        print("No entry node found.")

    return steps, entry_step, common_steps, nodes

def make_outputs(steps, entry_step, common_steps, nodes, args) -> List[str]:
    # 실행 흐름 빌드 및 결과 문자열 구성
    result: List[str] = []

    # 결과 문자열 리스트 생성
    result.append("=========== Steps ===========")
    result.extend(print_steps(entry_step))
    result.append("")
    result.append("")
    result.append("=========== Common Steps ===========")
    result.extend(print_common_steps(common_steps))

    if args.print_txt:
        print("\n".join(result))

    # ✅ TXT 저장
    if args.save_txt:
        txt_path = args.output + '.txt'
        with open(txt_path, 'w', encoding='utf-8') as f:
            f.write("\n".join(result))
        if args.debug:
            print(f"텍스트 결과 저장 완료 → {txt_path}")

    # ✅ JSON 저장
    if args.save_json:
        # 모든 Steps을 순회하며 to_dict()로 변환한다.
        def collect_steps(steps: Dict[str, Step]) -> Dict[str, Dict]:
            steps_for_json: Dict[str, Dict] = {}
            for key, frame in steps.items():
                steps_for_json[key] = frame.to_dict()
            return steps_for_json

        # 모든 Node를 순회하며 to_dict()로 변환한다.
        def collect_nodes(nodes: Dict[str, Node]) -> Dict[str, Dict]:
            nodes_for_json: Dict[str, Dict] = {}
            for key, node in nodes.items():
                nodes_for_json[key] = node.to_dict()
            return nodes_for_json

        json_output = {
            "entry_step_key": entry_step.key,
            "common_step_keys": [s.key for s in common_steps.values() if s],
            "steps": collect_steps(steps),
            "nodes": collect_nodes(nodes),
        }

        json_path = args.output + '.json'
        with open(json_path, 'w', encoding='utf-8') as jf:
            json.dump(json_output, jf, indent=2, ensure_ascii=False)
        if args.debug:
            print(f"JSON 결과 저장 완료 → {json_path}")

    return result

def _load_lines_from_dump(path: str, dump_name: str | None = None) -> str:
    """
    path 입력이 .zip 이면 dump_name으로 지정된 파일을 사용
    입력이 .json 이면 바로 그 파일을 사용
    - 반환: List[str] (그래프 덤프 라인들)
    """
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            name = None
            if dump_name and dump_name in z.namelist():
                name = dump_name
            else:
                for n in z.namelist():
                    if n.lower().endswith(".json"):
                        name = n
                        break
            if not name:
                raise FileNotFoundError("zip 내부에 .json 파일을 찾지 못했습니다.")
            data = json.loads(z.read(name).decode("utf-8"))
            return data
    else:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data


def main():
    ap = argparse.ArgumentParser(description="UE 블루프린트 실행 흐름 분석")
    ap.add_argument("--input", required=True, help="func_xxx.json 파일 또는 그 파일을 포함한 .zip 경로")
    ap.add_argument("--output", required=True, help="출력 파일 경로 경로")
    ap.add_argument("--dump-name", default=None, help="입력이 .zip일 때 내부의 특정 .json 파일명 지정")

    ap.add_argument("--save-json", action="store_true", help="JSON 포맷으로 결과 저장")
    ap.add_argument("--save-txt", action="store_true", help="텍스트 포맷으로 결과 저장")
    ap.add_argument("--print-txt", action="store_true", help="텍스트 포맷으로 결과 저장")
    ap.add_argument("--debug", action="store_true", help="디버그 모드 활성화")
    args = ap.parse_args()

    data: str = _load_lines_from_dump(args.input, args.dump_name)
    steps, entry_step, common_steps, nodes = parse_flow(data, args.debug)
    resolve_merging_points_for_entry(entry_step, steps, args.debug)
    for common_key, common_step in common_steps.items():
        resolve_merging_points_for_entry(common_step, steps, args.debug)
    make_outputs(steps, entry_step, common_steps, nodes, args)

if __name__ == '__main__':
    main()
# json_path = "func_TryAdjustStats_parsed.json"
