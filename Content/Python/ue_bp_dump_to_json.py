# -*- coding: utf-8 -*-
"""
UE 블루프린트 그래프 덤프(graph_dump_lines) → JSON 변환기 (자연 순서 모드)
--------------------------------------------------------------------------
- 입력: graph_dump_lines를 포함한 .dump 파일 또는 그 .dump을 품은 .zip
- 출력: 덤프에 등장한 **순서 그대로** 키를 기록한 JSON

핵심 정책
- Class는 원문 토큰(`/Script/...`) 그대로 보존
- Index, RawLines 같은 도우미 필드는 출력하지 않음
- CustomProperties
  * Pin 항목은 **Type/Data 래퍼 없이 평탄화**하고, **필드 순서를 덤프 순서 그대로** 유지
  * PinType은 (1) PinType=(...) 내부와 (2) PinType.* 점 표기를 **모두 병합**하여
    하나의 객체로 구성(서브키 순서도 덤프 순서 유지)
  * LinkedTo는 [{ "Node","PinId" }] 형태로 정규화하되 **원래 위치**에 둠
  * Pin 이외 항목은 {"Type","Data"} 래퍼를 유지(필요시 평탄화 가능)
- NSLOCTEXT/LOCTEXT는 기본값 인자만 값으로 저장

설계 철학
- 정규식은 **보조적인 부분**(텍스트 매크로/간단 패턴)에서만 사용하고,
  괄호·따옴표를 인지하는 **스택 기반 토크나이저**로 파싱 안정성을 확보.
- 덤프가 케바케로 바뀌어도 터지지 않도록 **모든 미지의 키를 보존**하고,
  등장 순서를 그대로 유지.

사용 예시
    python ue_bp_dump_to_json.py --input BPC_CharacterDeath.zip --output out.json
    python ue_bp_dump_to_json.py --input xxx.dump --output xxx.json
"""

from __future__ import annotations
import json, re, sys, zipfile, argparse
from collections import OrderedDict
from typing import List, Tuple, Dict, Any

# -------------------------- 토크나이저 유틸 --------------------------
def split_top_level_commas(s: str) -> List[str]:
    """
    최상위 레벨의 쉼표 기준으로 문자열을 분리한다.
    - 괄호 깊이(depth)와 따옴표 상태를 추적하여, 괄호/따옴표 내부의 쉼표는 무시.
    - 정규식을 쓰지 않고 스택 방식으로 안전하게 분리.
    """
    parts: List[str] = []
    depth = 0
    buf: List[str] = []
    in_q = False
    esc = False
    for ch in s:
        if esc:
            buf.append(ch); esc = False; continue
        if ch == "\\":
            esc = True; buf.append(ch); continue
        if ch == '"':
            in_q = not in_q; buf.append(ch); continue
        if not in_q:
            if ch == "(":
                depth += 1; buf.append(ch); continue
            if ch == ")":
                depth = max(0, depth-1); buf.append(ch); continue
            if ch == "," and depth == 0:
                parts.append("".join(buf).strip()); buf = []; continue
        buf.append(ch)
    if buf:
        parts.append("".join(buf).strip())
    return parts

def parse_paren_items_preserve(s: str) -> List[Tuple[str, str]]:
    """
    괄호 그룹 문자열을 '등장 순서'대로 (key 또는 None, value_str) 리스트로 변환.
    - value는 원시 문자열(중첩 괄호 포함)로 유지하여 후처리에서 사용.
    - 예: '(A=1,B=(C=2,D=3))' → [("A","1"),("B","(C=2,D=3)")]
    - 예: 'LinkedTo=(K2Node_1 ABCD, K2Node_2 EFGH)' → [("LinkedTo","(K2Node_1 ABCD, K2Node_2 EFGH)")]
    """
    s = s.strip()
    if s.startswith("(") and s.endswith(")"):
        s = s[1:-1]
    items = split_top_level_commas(s)
    out: List[Tuple[str, str]] = []
    for it in items:
        if not it:
            continue
        if "=" in it:
            k, v = it.split("=", 1)
            out.append((k.strip(), v.strip()))
        else:
            # 키 없는 토큰(드문 케이스). 상위 호출부에서 필요 시 해석.
            out.append((None, it.strip()))
    return out

def parse_paren_dict(s: str) -> Dict[str, Any]:
    """
    일반 용도의 (순서 비보장) 괄호 파서.
    - Non-Pin CustomProperties 등 '순서 중요도가 낮은' 곳에 사용.
    - 알 수 없는 키도 모두 보존.
    """
    s = s.strip()
    if s.startswith("(") and s.endswith(")"):
        s = s[1:-1]
    out: Dict[str, Any] = {}
    if s == "":
        return out
    items = split_top_level_commas(s)
    for item in items:
        if not item:
            continue
        if "=" not in item:
            out.setdefault("_", []).append(item)
            continue
        k, v = item.split("=", 1); k = k.strip(); v = v.strip()
        # 배열 인덱스 형태 Key(0)=... 지원
        m = re.match(r"^(.*)\((\d+)\)$", k)
        if m:
            base, idx = m.group(1), int(m.group(2))
            arr = out.setdefault(base, [])
            while len(arr) <= idx:
                arr.append(None)
            arr[idx] = v
            continue
        # 중첩 괄호는 재귀 처리
        if v.startswith("(") and v.endswith(")"):
            out[k] = parse_paren_dict(v); continue
        # 따옴표 값 처리
        if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
            out[k] = v[1:-1]
        else:
            out[k] = v
    return out

# -------------------------- 텍스트 매크로 --------------------------
_re_nsloctext = re.compile(r'NSLOCTEXT\(\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"([^"]*)"\s*\)')
_re_loctext  = re.compile(r'LOCTEXT\(\s*"[^"]*"\s*,\s*"([^"]*)"\s*\)')

def resolve_text_macro(token: str):
    """
    NSLOCTEXT/LOCTEXT 토큰을 '표시 문자열'만 남기도록 단순 해석.
    - NSLOCTEXT("Namespace","Key","Default") → "Default"
    - LOCTEXT("Key","Default") → "Default"
    """
    if not isinstance(token, str):
        return token
    m = _re_nsloctext.fullmatch(token)
    if m: return m.group(1)
    m = _re_loctext.fullmatch(token)
    if m: return m.group(1)
    return token

def resolve_text_macros_recursive(obj):
    """사전/리스트 전역으로 텍스트 매크로 해석 적용."""
    if isinstance(obj, dict):
        return {k: resolve_text_macros_recursive(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [resolve_text_macros_recursive(x) for x in obj]
    if isinstance(obj, str):
        return resolve_text_macro(obj)
    return obj

# -------------------------- LinkedTo 정규화 --------------------------
def _parse_link_token(s: str):
    """
    LinkedTo 토큰 하나를 {Node, PinId}로 변환 (가능한 경우).
    - 허용 포맷: 'K2Node_Name GUID', 'EdGraphPin'K2Node_Name GUID''
    - 실패 시 {}(빈 dict) 반환 → 호출부에서 필터링
    """
    s = s.strip().strip(",")
    m = re.match(r"^([A-Za-z0-9_]+)\s+([0-9A-Fa-f]{8,})$", s)
    if m: return {"Node": m.group(1), "PinId": m.group(2)}
    m = re.search(r"EdGraphPin'([^']+)'", s)
    if m:
        inner = m.group(1)
        mm = re.match(r"^([A-Za-z0-9_]+)\s+([0-9A-Fa-f]{8,})$", inner)
        if mm: return {"Node": mm.group(1), "PinId": mm.group(2)}
    m = re.match(r"^([0-9A-Fa-f]{8,})$", s)
    if m: return {"PinId": m.group(1)}
    m = re.match(r"^([A-Za-z0-9_]+)$", s)
    if m: return {"Node": m.group(1)}
    return {}

def parse_linked_to_tokens(v_raw: str):
    """
    LinkedTo=(토큰,토큰,...) → [{Node,PinId}, ...]
    - 괄호/따옴표 인지 스플릿을 사용해 안전하게 분해
    - 인식 실패 항목은 버림(UE 편집기에서 대부분 '노드명 GUID' 형식)
    """
    s = v_raw.strip()
    if s.startswith("(") and s.endswith(")"):
        s = s[1:-1]
    # 괄호/쉼표가 섞일 수 있으므로 안전분해
    tokens = split_top_level_commas(s) if ("," in s or "(" in s) else [s]
    out = []
    for t in tokens:
        parsed = _parse_link_token(t)
        if parsed:
            out.append(parsed)
    return out

# -------------------------- Pin(순서 유지) 빌더 --------------------------
def build_pin_ordered(pin_paren_str: str) -> OrderedDict:
    """
    Pin을 OrderedDict로 구성(필드 순서 = 덤프 순서).
    - PinType=(...) 중첩과 PinType.* 점 표기를 하나의 PinType 객체로 병합
      (서브키 순서도 등장 순서 유지)
    - LinkedTo는 [{Node,PinId}]로 정규화하되 '원래 위치'에 둔다.
    - 값이 "문자열" 형태로 출력되었으면 문자열 그대로 유지(덤프 충실도 우선).
    """
    od: OrderedDict[str, Any] = OrderedDict()
    pt: OrderedDict[str, Any] | None = None  # PinType 서브키 순서 유지용
    for k, v in parse_paren_items_preserve(pin_paren_str):
        if k is None:
            # 최상위 Pin에서는 거의 나오지 않으므로 무시
            continue
        v_res = resolve_text_macro(v)
        # 따옴표로 감싼 값은 벗겨서 순수 문자열로
        if isinstance(v_res, str) and len(v_res) >= 2 and v_res[0] == '"' and v_res[-1] == '"':
            v_res = v_res[1:-1]
        # LinkedTo 특수 처리
        if k == "LinkedTo":
            od["LinkedTo"] = parse_linked_to_tokens(v_res)
            continue
        # PinType=(...) 중첩 처리
        if k == "PinType" and isinstance(v_res, str) and v_res.startswith("("):
            pt = pt or OrderedDict()
            for sk, sv in parse_paren_items_preserve(v_res):
                if sk is None:
                    continue
                sv_res = resolve_text_macro(sv)
                if isinstance(sv_res, str) and len(sv_res) >= 2 and sv_res[0] == '"' and sv_res[-1] == '"':
                    sv_res = sv_res[1:-1]
                if sk not in pt:
                    pt[sk] = sv_res
            od["PinType"] = pt
            continue
        # PinType.* 점 표기 병합
        if k.startswith("PinType."):
            sub = k[len("PinType.") :]
            pt = pt or OrderedDict()
            if sub not in pt:
                pt[sub] = v_res
            od["PinType"] = pt
            continue
        # 일반 키: 최초 등장만 기록(UE 덤프는 보통 중복 없음)
        if k not in od:
            od[k] = v_res
    return od

# -------------------------- Begin 라인 파싱 --------------------------
def parse_begin_header(line: str):
    """
    'Begin Object Class=... Name="..." ...' 라인에서
    - Class(원문 토큰), Name, 그 외 추가 key=value 토큰을 '등장 순서대로' 추출.
    """
    cls = ""
    m = re.search(r"Class=([^\s]+)", line)
    if m: cls = m.group(1)
    name = ""
    m = re.search(r'Name="([^"]+)"', line)
    if m: name = m.group(1)
    extras: list[tuple[str, str]] = []
    for m in re.finditer(r'([A-Za-z0-9_.]+)=("([^"]*)"|[^"\s]+)', line):
        k = m.group(1)
        if k in ("Begin", "Object", "Class", "Name"):
            continue
        raw = m.group(2)
        if raw.startswith('"') and raw.endswith('"'):
            raw = raw[1:-1]
        extras.append((k, resolve_text_macro(raw)))
    return cls, name, extras

# -------------------------- 노드 블록 반복자 --------------------------
def iter_node_blocks(lines: List[str]):
    """
    Begin Object ~ End Object 한 덩어리씩 잘라서 반환.
    - 덤프가 깨져 있어도 가능한 한 다음 블록으로 진행.
    """
    cur: List[str] = []
    for L in lines:
        if L.strip().startswith("Begin Object"):
            if cur:
                yield cur
                cur = []
        cur.append(L)
        if L.strip().startswith("End Object"):
            yield cur
            cur = []
    if cur:
        yield cur

# -------------------------- 내보내기(자연 순서) --------------------------
def export_natural_order(lines: List[str]) -> Dict[str, Any]:
    """
    - 노드 키: 등장 순서대로 기록
    - CustomProperties:
        Pin → 평탄화(OrderedDict), 필드 순서 보존
        Non-Pin → {"Type","Data"} 유지
    """
    out_nodes: list[OrderedDict[str, Any]] = []
    for block in iter_node_blocks(lines):
        begin = block[0]
        cls, name, extras = parse_begin_header(begin)

        node_obj: OrderedDict[str, Any] = OrderedDict()
        node_obj["Class"] = cls
        node_obj["Name"] = name
        for k, v in extras:
            node_obj[k] = v

        for L in block[1:]:
            s = L.strip()
            if s.startswith("End Object"):
                break
            if s.startswith("NodePosX="):
                if "NodePosX" not in node_obj:
                    try:
                        node_obj["NodePosX"] = int(s.split("=", 1)[1])
                    except:
                        node_obj["NodePosX"] = s.split("=", 1)[1]
            elif s.startswith("NodePosY="):
                if "NodePosY" not in node_obj:
                    try:
                        node_obj["NodePosY"] = int(s.split("=", 1)[1])
                    except:
                        node_obj["NodePosY"] = s.split("=", 1)[1]
            elif s.startswith("NodeGuid="):
                if "NodeGuid" not in node_obj:
                    node_obj["NodeGuid"] = s.split("=", 1)[1]
            elif s.startswith("FunctionReference="):
                if "FunctionReference" not in node_obj:
                    d = parse_paren_dict(s.split("=", 1)[1])
                    node_obj["FunctionReference"] = resolve_text_macros_recursive(d)
            elif s.startswith("VariableReference="):
                if "VariableReference" not in node_obj:
                    d = parse_paren_dict(s.split("=", 1)[1])
                    node_obj["VariableReference"] = resolve_text_macros_recursive(d)
            elif s.startswith("CustomProperties "):
                rest = s[len("CustomProperties ") :]
                mtype = rest.split(" ", 1)[0]  # 예: "Pin", "EdGraphNode" ...
                paren_start = rest.find("(")
                paren = rest[paren_start:] if paren_start != -1 else ""
                # build a subdict in the same style
                if mtype == "Pin":
                    pin_od = build_pin_ordered(paren)
                    node_obj.setdefault("Pins", []).append(pin_od)  # ✅ 변경: Pin은 Pins 배열에 저장
                else:
                    d = parse_paren_dict(paren)
                    d = resolve_text_macros_recursive(d)
                    node_obj.setdefault("CustomProperties", []).append({"Type": mtype, "Data": d})
            else:
                m = re.match(r"([A-Za-z0-9_.]+)=(.*)$", s)
                if m:
                    k, v = m.group(1), m.group(2)
                    # 따옴표 문자열이면 껍질만 제거
                    if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
                        v = v[1:-1]
                    v = resolve_text_macro(v)
                    if k not in node_obj:
                        node_obj[k] = v

        out_nodes.append(node_obj)

    return {"Nodes": out_nodes}

# -------------------------- IO & CLI --------------------------
def _load_lines_from_dump(path: str, dump_name: str | None = None):
    """
    입력이 .zip 이면 내부의 첫 번째 .dump(또는 --dump-name 지정)을 열어 graph_dump_lines를 찾고,
    입력이 .dump 이면 그 파일에서 graph_dump_lines를 직접 꺼낸다.
    - 반환: List[str] (그래프 덤프 라인들)
    """
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            name = None
            if dump_name and dump_name in z.namelist():
                name = dump_name
            else:
                for n in z.namelist():
                    if n.lower().endswith(".dump"):
                        name = n
                        break
            if not name:
                raise FileNotFoundError("zip 내부에 .dump 파일을 찾지 못했습니다.")
            data = json.loads(z.read(name).decode("utf-8"))
            return data.get("graph_dump_lines") or data.get("lines") or data
    else:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data.get("graph_dump_lines") or data.get("lines") or data

def main():
    ap = argparse.ArgumentParser(description="UE 블루프린트 그래프 덤프 → JSON (자연 순서 모드)")
    ap.add_argument("--input", required=True, help="graph_dump_lines를 담은 .dump 또는 그 .dump를 포함한 .zip 경로")
    ap.add_argument("--output", required=True, help="출력 JSON 경로")
    ap.add_argument("--dump-name", default=None, help="입력이 .zip일 때 내부의 특정 .json 파일명 지정")
    args = ap.parse_args()

    lines = _load_lines_from_dump(args.input, args.dump_name)
    if not isinstance(lines, list):
        raise ValueError("graph_dump_lines는 문자열 리스트(List[str])여야 합니다.")
    res = export_natural_order(lines)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=2)

if __name__ == "__main__":
    main()
