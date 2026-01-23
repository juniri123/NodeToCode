# -*- coding: utf-8 -*-
from __future__ import annotations
from typing import List, Optional


# 핀 정보
class Pin:
    def __init__(self, pin_name: str, pin_guid: str, node_name: str, node_guid: str):
        # 핀 이름
        self.name = pin_name
        # 핀 guid
        self.guid = pin_guid
        # 이 핀을 가진 node의 이름
        self.node_name = node_name
        # 이 핀을 가진 node의 guid
        self.node_guid = node_guid

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "guid": self.guid,
            "node_name": self.node_name,
            "node_guid": self.node_guid,
        }

    @staticmethod
    def from_dict(d):
        return Pin(
            d["name"],
            d["guid"],
            d["node_name"],
            d["node_guid"],
        )

# 두 핀간 연결 관계
class Link:
    def __init__(self, from_pin: Pin, to_pin: Pin):
        # 출발 핀
        self.from_pin: Pin = from_pin
        # 도착 핀
        self.to_pin: Pin = to_pin

    def to_dict(self) -> dict:
        return {
            "from": self.from_pin.to_dict(),
            "to": self.to_pin.to_dict(),
        }

    @staticmethod
    def from_dict(d):
        return Link(
            Pin.from_dict(d["from"]),
            Pin.from_dict(d["to"])
        )

# BP Graph노드 데이터 일부를 파싱하여 저장
class Node:
    def __init__(self, name: str, guid: str):
        # 노드 이름
        self.name = name
        # 노드 guid
        self.guid = guid

        # in/out 실행 핀
        self.exec_in_pins: List[Pin] = []
        self.exec_out_pins: List[Pin] = []
        # in/out 실행 핀 링크
        self.exec_in_links: List[Link] = []
        self.exec_out_links: List[Link] = []
        # in/out 데이터 핀
        self.data_in_pins: List[Pin] = []
        self.data_out_pins: List[Pin] = []
        # in/out 데이터 핀 링크
        self.data_in_links: List[Link] = []
        self.data_out_links: List[Link] = []

    def to_dict(self) -> dict:
        return {
            "node_name": self.name,
            "node_guid": self.guid,
            "exec_in_pins": [p.to_dict() for p in self.exec_in_pins],
            "exec_out_pins": [p.to_dict() for p in self.exec_out_pins],
            "exec_in_links": [l.to_dict() for l in self.exec_in_links],
            "exec_out_links": [l.to_dict() for l in self.exec_out_links],
            "data_in_pins": [p.to_dict() for p in self.data_in_pins],
            "data_out_pins": [p.to_dict() for p in self.data_out_pins],
            "data_in_links": [l.to_dict() for l in self.data_in_links],
            "data_out_links": [l.to_dict() for l in self.data_out_links],
        }

    @staticmethod
    def from_dict(d):
        node = Node(d["node_name"], d["node_guid"])

        node.exec_in_pins  = [Pin.from_dict(x) for x in d["exec_in_pins"]]
        node.exec_out_pins = [Pin.from_dict(x) for x in d["exec_out_pins"]]
        node.exec_in_links = [Link.from_dict(x) for x in d["exec_in_links"]]
        node.exec_out_links = [Link.from_dict(x) for x in d["exec_out_links"]]

        node.data_in_pins = [Pin.from_dict(x) for x in d["data_in_pins"]]
        node.data_out_pins = [Pin.from_dict(x) for x in d["data_out_pins"]]
        node.data_in_links = [Link.from_dict(x) for x in d["data_in_links"]]
        node.data_out_links = [Link.from_dict(x) for x in d["data_out_links"]]

        return node

# 실행 흐름
class Step:
    def __init__(self):
        # Step의 키,
        # 보통은 node.name이 사용되나,
        # 실행 일력핀이 여러 개인 경우에, 같은 노드가 중복해서 들어오기 때문에, 이 경우에는
        # {node.name}_Merging_{#n} 과 같은 형태로 저장된다.
        self.key: str = None

        #--------------------------------------------------------
        # Node 기반 정보
        #--------------------------------------------------------
        # 이 스텝에 연결된 node
        # json으로 저장될 때, node_guid, node_name 가 저장된다.
        self.node: Node = None
        # node.exec_in_links의 from pin을 저장
        self.from_pins: List[Pin] = []


        #--------------------------------------------------------
        # 흐름을 저장하기 위한 용도
        #--------------------------------------------------------
        # 분기되는 자식 노드들,
        # json으로 저장될 때, branch_keys로 저장된다.
        self.branches: List[Step] = []
        # 직렬 흐름(next) 노드
        # json으로 저장될 때, next_key로 저장된다.
        self.next: Step = None
        # 이 step이 is_merging_point이거나 common step인 경우에, 이 스텝으로 머지되는 common_placeholder들을 저장한다.
        # common step인 경우에, 모든 플레이스 홀더가 들어 있을 것이고
        # is_merging_point인 경우에, 머지되는 플레이 홀더만 들어 있을 것.
        self.common_placeholders: List[Step] = []
        # 인덴테이션 depth라고 생각하면 된다.
        self.logic_depth = 0

        #--------------------------------------------------------
        # 상태 플래그
        #--------------------------------------------------------
        # 분기 되는 스텝인지 여부
        # (부모 노드의 아웃 실행핀이 여러 개 이면 자식들은 분기 노드가 된다.)
        self.is_branched: bool = False
        # Common step이 여러군데 쓰일 경우, 지금 구조에서 흐름을 정의하기 어려움.
        # 따라서 Common step이 여기 들어와야 함을 나타내는 더미 노드인지 여부를 저장하는 변수.
        # 추후, 이 placeholder들은 적당한 위치에서 머지 되어야, 휴먼리더블한 코드가 된다.
        self.is_common_placeholder: bool = False
        #
        self.is_merging_point: bool = False
        #
        self.is_comment_out: bool = False
        #
        self.is_fallthrough: bool = False


        #--------------------------------------------------------
        # Step들 간의 Flow를 만들 때, 사용하는 필드
        #--------------------------------------------------------
        # Step간 Flow를  만들 때, 사용하는 필드로,
        # 현재 처리 중인 exec_out_pin의 idx를 나타낸다
        # serialize/deserialize될 필요 없음 / 런타임에만 사용한 필드
        self.__out_link_idx: Optional[int] = None


        #--------------------------------------------------------
        # Json으로 저장될 때, 사용되는 필드
        #--------------------------------------------------------
        # 이 스텝에 해당하는 노드 이름과 guid를 저장.
        # node <---> node_name / node_guid
        self.node_name: Optional[str] = None
        self.node_guid: Optional[str] = None
        # branche들의 키를 저장하는 리스트,
        # branches <---> branch_keys
        self.branch_keys: Optional[List[str]] = None
        # next의 키를 저장,
        # next <---> next_key
        self.next_key: Optional[str] = None
        # Common placeholder들의 키를 저장하는 리스트,
        # common_placeholders <---> common_placeholdre_keys
        self.common_placeholder_keys: Optional[List[str]] = None

    def make_as_entry(self, logic_depth: int):
        self.__out_link_idx = -1
        self.logic_depth = logic_depth

    @property
    def outlink_idx(self):
        if self.__out_link_idx is None:
            self.__out_link_idx = -1
        return self.__out_link_idx

    def next_outlink_idx(self) -> int:
        if self.__out_link_idx is None:
            self.__out_link_idx = -1
        self.__out_link_idx += 1
        return self.__out_link_idx

    @property
    def outlink(self) -> Optional[Link]:
        idx = self.__out_link_idx
        if idx is None or idx < 0:
            return None
        if idx >= len(self.node.exec_out_links):
            return None
        return self.node.exec_out_links[idx]

    @property
    def has_all_outlink_processed(self) -> bool:
        return self.outlink_idx >= len(self.node.exec_out_links)

    @property
    def has_multiple_exec_inlink(self) -> bool:
        return len(self.node.exec_in_links) > 1

    @property
    def has_branches(self) -> bool:
        return len(self.node.exec_out_pins) > 1

    @property
    def is_common_step(self) -> bool:
        return self.has_multiple_exec_inlink and not self.is_merging_point

    def pop_logic_depth(self, global_logic_depth:int) -> int:
        if self.is_branched:
            return global_logic_depth - 1
        return global_logic_depth

    def push_logic_depth(self, global_logic_depth:int) -> int:
        if self.is_branched:
            return global_logic_depth + 1
        return global_logic_depth

    def append_from_pin(self, link: Link):
        self.from_pins.append(link.from_pin)

    def append_branch(self, branch: Step):
        self.branches.append(branch)

    def set_next(self, next: Step):
        self.next = next

    def record_common_placeholder(self, placeholder: Step):
        if self.common_placeholders is None:
            self.common_placeholders = []
        self.common_placeholders.append(placeholder)

    # Step 객체를 JSON 직렬화 가능한 dict로 변환한다.
    def to_dict(self):
        return {
            "key": self.key,
            "node_guid": self.node.guid if self.node else None,
            "node_name": self.node.name if self.node else None,
            "from_pins": [p.__dict__ for p in self.from_pins],
            "branch_keys": [b.key for b in self.branches],
            "next_key": self.next.key if (self.next and self.next.node) else None,
            "logic_depth": self.logic_depth,
            "is_branched": self.is_branched,
            "is_common_placeholder": self.is_common_placeholder,
            "is_merging_point": self.is_merging_point,
            "is_comment_out": self.is_comment_out,
            "is_fallthrough": self.is_fallthrough,
            "common_placeholder_keys": [ph.key for ph in self.common_placeholders]
        }

    # Json 역직렬화 시, Step 객체로 변환한다.
    # 이 함수 실행 후에, 전체 Step/Node을 보고 처리 해 줘야 하는 것들이 있어서 유의해야 함
    @staticmethod
    def from_dict(d):
        frame = Step()
        frame.logic_depth = d["logic_depth"]
        frame.key = d["key"]
        frame.from_pins = [Pin.from_dict(x) for x in d["from_pins"]]
        frame.is_branched = d["is_branched"]
        frame.is_common_placeholder = d["is_common_placeholder"]
        frame.is_merging_point = d["is_merging_point"]
        frame.is_comment_out = d["is_comment_out"]
        frame.is_fallthrough = d["is_fallthrough"]

        # 역파싱용 필드 채워넣기
        frame.node_name = d["node_name"]
        frame.node_guid = d["node_guid"]
        frame.branch_keys = d.get("branch_keys", [])
        frame.next_key = d.get("next_key")
        frame.common_placeholder_keys = d.get("common_placeholder_keys")
        return frame


class MergingGroup:
    """
    placeholder 그룹 1개를 표현하는 데이터 구조.
    merge point 생성에 필요한 모든 정보가 들어있다.
    """

    def __init__(self, common_step: Step, merging_point_step: Step, placeholders: List[Step], is_fallthough: bool):
        # placeholder들이 향하는 기존 공통 목적지 Step (Step)
        self.common_step: Step = common_step

        # 생성해야 하는 merging point Step (Step)
        self.merging_point_step: Step = merging_point_step

        # 이 그룹에 속한 placeholder 목록 (List[Step])
        self.placeholders: List[Step] = placeholders

        # fallthrough 되어야 하는 머지 포인트 인지 여부
        self.is_fallthough = is_fallthough

    def __repr__(self):
        return (
            f"MergingGroup(placeholders={len(self.placeholders)}, "
            f"merging_point_step={self.merging_point_step.key}, "
            f"common_step={self.common_step.key},"
            f"common_step={self.is_fallthough})"
        )
