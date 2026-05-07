아래 두 파일을 기준으로 Blueprint를 Unreal C++ 코드로 변환해줘.

파일 역할:
- *_flow.txt : 실행 흐름(exec 흐름) 정보
- *_graph.txt 또는 붙여넣은 텍스트 : 노드 상세 정보(데이터 핀 연결)

중요 규칙:
1. 반드시 flow.txt를 기준으로 실행 흐름을 구성한다.
2. graph.txt는 데이터 핀/변수/함수 입력값 복원용으로만 사용한다.
3. 추측하지 말고, 불명확한 타입이나 함수 시그니처는 TODO로 남긴다.
4. Blueprint 노드명(K2Node_...)은 가능한 유지한다.
5. merge point는 사람이 읽기 좋은 C++ 흐름으로 정리해도 된다.
6. Unreal Engine 스타일 C++로 작성한다.
7. 함수 이름/변수 이름은 BP 원본 이름 최대한 유지한다.

주석 포맷 규칙:
반드시 아래 구조를 유지한다.

예시:

// =====================================================
// [Flow.txt]
// ➡️ 📌True::P66 from (📋K2Node_IfThenElse_4::N16)
// │   📋K2Node_GenericCreateObject_0::N23
// │   📌exec::P81 from (📋K2Node_GenericCreateObject_0::N23) → 📋K2Node_CallFunction_10::N6
//
// [Flow]
// IfThenElse_4 True
//  -> GenericCreateObject_0
//  -> CallFunction_10 : GetFilteredGameObjects
//  -> MacroInstance_0 : ForEachLoop
//
// NodeInfo:
// GenericCreateObject Class = InventoryFilter
// GenericCreateObject ReturnValue -> GetFilteredGameObjects Target
// Self -> Inventory
// GetFilteredGameObjects.GameObjects -> ForEachLoop Array
// =====================================================

설명:
- [Flow.txt]
  -> flow.txt 원문을 최대한 그대로 적는다.
  -> 요약하거나 번역하지 않는다.

- [Flow]
  -> 사람이 읽기 쉽게 실행 흐름을 요약한다.

- [NodeInfo]
  -> graph.txt에서 데이터 핀 연결을 복원해서 적는다.
  -> "A -> B(Target)" 형태의 데이터 핀 흐름을 사람이 읽기 좋은 형태로 정리한다.

추가 규칙:
- [Flow.txt] 블록은 함수 전체를 한 번에 덮는 용도로 사용하지 않는다.
- [Flow.txt] 블록은 반드시 바로 아래에 작성되는 C++ 코드 조각과 1:1로 대응되는 최소 실행 구간만 포함한다.
- 상위 제어문 주석 블록에는 하위 branch/body의 상세 Flow.txt를 포함하지 않는다.
- if/switch/loop가 나오면 해당 제어문을 판단/진입시키는 Flow.txt 구간만 제어문 바로 앞에 붙인다.
- if/switch/loop 내부에는 해당 branch/body에 해당하는 Flow.txt 구간을 별도 블록으로 붙인다.
- 공통 merge 지점도 주석으로 유지한다.
- return 지점은 FunctionResult 노드 기준으로 표시한다.
- 데이터 핀 체이닝은 최대한 복원한다.
- BP 원본과 grep 가능하도록 K2Node 이름을 유지한다.

출력 형식:
1. 함수 시그니처
2. 상세 주석 포함 C++ 코드
3. 마지막에 TODO / 확인 필요 목록