아래 파일들을 기준으로 Blueprint를 Unreal C++ 코드로 변환해줘.
최종 출력은 "Blueprint dump"가 아니라 실제 사람이 유지보수 가능한 Unreal gameplay C++ 코드가 되어야 한다.
Blueprint tracing 정보는 주석에 유지하고, 실제 코드는 readability를 우선한다.

파일 역할:
- *_flow.txt : 실행 흐름(exec 흐름) 정보
- *_graph.txt 또는 붙여넣은 텍스트 : 노드 상세 정보(데이터 핀 연결)
- *_structs.txt : Blueprint에서 사용된 Struct 타입/필드 정보

중요 규칙:
1. 반드시 flow.txt를 기준으로 실행 흐름을 구성한다.
2. graph.txt는 데이터 핀/변수/함수 입력값 복원용으로만 사용한다.
3. Blueprint/graph/structs 정보와 Unreal Engine 문맥을 기반으로 합리적으로 복원한다.
   단, 입력 데이터에 직접 근거가 없는 추론/가정이 들어간 경우 반드시 주석 또는 TODO로 명시한다.
   예:
   - TODO: structs.txt에 없어 정확한 타입 검토 필요
   - Assumed from UE convention
   - Inferred from Blueprint pin usage
   - Estimated signature based on graph connection
4. Blueprint 노드명(K2Node_...)은 주석([Flow.txt], [Flow], [NodeInfo]) 및 매핑 설명에서만 유지한다.
   실제 C++ 코드의 변수명, 리턴값 이름, 임시값 이름에는 K2Node_*, *_ReturnValue, PromotableOperator_* 같은 BP intermediate 이름을 사용하지 않는다.
5. merge point는 사람이 읽기 좋은 C++ 흐름으로 정리해도 된다.
6. Unreal Engine 스타일 C++로 작성한다.
7. Blueprint에서 명시적으로 존재하는 gameplay 의미의 함수명/변수명은 최대한 유지한다.
   단, Blueprint intermediate 이름(K2Node_*, Temp*, *_ReturnValue 등)은 유지하지 않는다.
8. 리턴 값의 이름은 컨텍스트를 반영하여 생성한다.
   모든 함수 호출 결과, 연산 결과, Map_Find 결과값, bool 결과값은 gameplay 의미가 드러나는 semantic name으로 작성한다.
   예:
   - K2Node_CallFunction_3_ReturnValue ❌ → InstigatorOwner ⭕
   - K2Node_CallFunction_1_ReturnValue ❌ → StatsComponent ⭕
   - K2Node_CallFunction_30_ReturnValue ❌ → FreshnessType ⭕
   - K2Node_CallFunction_13_ReturnValue ❌ → FreshnessRule ⭕
   - K2Node_PromotableOperator_1_ReturnValue ❌ → FoodPoisoningAmount ⭕

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
- 데이터 핀 흐름은 유지하되, 불필요한 Blueprint intermediate temp 변수는 생성하지 않는다.
- 사람이 읽기 좋은 Unreal C++ 흐름으로 정리한다.

- Unreal Engine 관례 수준의 일반적 타입 추론(예: AActor, UActorComponent, TMap)은 별도 TODO 없이 허용한다.
- gameplay specific 타입/시그니처 추론은 반드시 TODO 또는 Assumed/Inferred 주석으로 표시한다.
- 확정 정보와 추론 정보를 구분해서 출력한다.

- BP 원본과 grep 가능하도록 K2Node 이름은 주석([Flow.txt], [Flow], [NodeInfo])에서만 유지한다.
- BP node id는 코드 주석에서 grep/debug 용도로만 보존한다.
- 실제 C++ 코드에서는 K2Node_*, *_ReturnValue, PromotableOperator_* 같은 Blueprint intermediate naming을 변수명으로 사용하지 않는다.
- 실제 C++ 코드는 사람이 유지보수 가능한 Unreal C++ 스타일 변수명을 사용한다.
- 함수 리턴값/임시값/연산값은 반드시 컨텍스트 기반 semantic naming으로 작성한다.
- 이름을 확정할 수 없으면 K2Node 이름을 쓰지 말고 TODO 의미 이름을 사용한다.
  예: UnknownTargetObject /* TODO: source K2Node_CallFunction_23 */

출력 형식:
1. 함수 시그니처
2. 상세 주석 포함 C++ 코드
3. 마지막에 TODO / 확인 필요 목록