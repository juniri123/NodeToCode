# N2CFlowBuilder 코드 리뷰 및 개선 노트

**날짜**: 2026-04-30  
**대상 파일**:
- `Plugins/NodeToCode/Source/Public/Core/N2CFlowBuilder.h`
- `Plugins/NodeToCode/Source/Private/Core/N2CFlowBuilder.cpp` (1113줄)
- `Plugins/NodeToCode/Source/Public/Models/Python/N2CFlowModel.h`
- `Plugins/NodeToCode/Source/Private/Models/N2CFlowModel.cpp` (536줄)

---

## 1. 코드 개요

### 핵심 문제
Blueprint 그래프의 실행 흐름은 DAG(방향 비순환 그래프)이기 때문에, 단순 DFS로 추적하면 합류 노드(n:1) 이후 로직이 **중복 출력**됨.

### 해결 전략
- **Common Step**: exec-in 링크가 2개 이상인 노드를 별도 "Common Steps" 섹션으로 분리
- **Placeholder**: 본문에서는 Common Step 대신 Placeholder만 삽입하여 중복 방지
- **Merging Point**: LCA(최저 공통 조상) 기반으로 합류 지점을 명시적으로 표시
- **switch fallthrough**: switch-case에서 여러 case가 같은 곳으로 빠지는 패턴 특수 처리

### 파이프라인 (BuildFlowTextFromNodes)
```
TArray<UK2Node*>
  → BuildNodesFromK2Nodes (K2Node → N2CFlow::Node 변환)
  → BuildStepsFromNodes (Node → Step 1:1 매핑)
  → BuildExecFlow (DFS 기반 실행 흐름 트리 구성)
  → ResolveMergingPoints (머지 포인트 해결)
  → FlowDataToTextLines (텍스트 변환)
  → flow.txt
```

---

## 2. 코드 평가

| 영역 | 평가 |
|---|---|
| 설계/아키텍처 | ⭐⭐⭐⭐ — DAG→트리 변환 전략이 깔끔 |
| 코드 품질 | ⭐⭐⭐⭐ — 에러 핸들링, MoveTemp 사용, JSON 대칭 등 |
| 안정성 | ⭐⭐⭐ — 사이클 방어, 재귀 깊이 제한 없음 |
| 확장성 | ⭐⭐⭐ — Step 역할 분리하면 더 좋아질 여지 |
| 실용성 | ⭐⭐⭐⭐⭐ — 실제 BP 패턴 커버 잘 됨 |

### 잘 된 부분
- 데이터 모델 분리 (Pin → Link → Node → Step)
- DFS 스택 기반 비재귀 순회 (BuildExecFlow)
- LCA 기반 머지 포인트
- JSON 직렬화/역직렬화 완비

---

## 3. 논의된 개선 사항 (5개)

### ① 방문 여부 체크 → **안 함**
- `IsCommonStep()` 판별 후 Placeholder로 대체하는 방식이 이미 논리적 방문 제어 역할을 함
- 단순 visited 체크를 넣으면 오히려 흐름이 누락됨

### ② PrintSteps 재귀 → 스택 기반 비재귀 → **완료**
- `PrintSteps_01` 구현 완료
- 파일: `Plugins/NodeToCode/Source/Private/Core/N2CFlowBuilder_01.cpp`
- **핵심**: Branches가 있는 노드에서 Next를 스택에 먼저 push, Branches를 역순 push
  - Branches 없으면 Next를 while 루프로 바로 이동
  - 재귀 버전과 동일한 DFS 순서 보장
- `ExecuteSaveFlowText`를 수정하여 v1/v2 동시 저장 비교 가능

### ③ BuildPlaceholderGroups의 O(n²) + FString::Contains → **안 함**
- 문자열 Contains로 인한 false positive 가능성은 인지함
- 개선안: GetParentChain 결과를 TArray<FString>으로 유지하고 prefix 비교
- 현실적으로 placeholder 수가 적어서 당장 문제 없음
- **결론**: 현행 유지

### ④ FindEntryStep이 K2Node_FunctionEntry만 찾음 → **나중에**
- EventGraph 지원 시 K2Node_Event, K2Node_CustomEvent 등도 entry로 찾아야 함
- 현재는 Function 그래프 전용으로 동작

### ⑤ Step 서브클래싱 → **설계만 확정, 나중에 구현**

제안된 구조:
```
StepBase (추상)
  ├─ ExecStep (일반 직렬 실행 노드)
  ├─ BranchStep (exec-out 여러 개인 분기 노드, Branches 배열 소유)
  ├─ CommonPlaceholderStep (Common Step 참조용 placeholder)
  ├─ CommonStep (여러 경로가 합류하는 공통 로직)
  └─ MergingPointStep (합류 지점 표시 노드)
```

이점:
- PrintSingleStep의 if/else 체인 → 각 서브클래스의 PrintLines() 가상 함수로 분산
- 새 Step 타입 추가 시 OCP 준수
- JSON 직렬화에서 type 필드 기반 팩토리 패턴 적용 가능

파일 영향 범위:
- `N2CFlowModel.h/cpp` — StepBase + 서브클래스 5종
- `N2CFlowBuilder.h/cpp` — Step 생성 시점에 서브클래스 팩토리 필요
- 최소 3~4개 파일 수정 필요 → EventGraph 지원 등 기능 추가 시 함께 리팩토링 권장

---

## 4. 생성된 파일

| 파일 | 용도 |
|---|---|
| `Plugins/NodeToCode/Source/Public/Core/N2CFlowBuilder_01.h` | 개선 비교용 헤더 |
| `Plugins/NodeToCode/Source/Private/Core/N2CFlowBuilder_01.cpp` | 스택 기반 PrintSteps_01 구현 |

---

## 5. 비교 테스트 방법

`Save Flow Text` 메뉴 클릭 시 (ExecuteSaveFlowText 수정됨):
- `{GraphName}_flow_v1.txt` — 기존 재귀 버전
- `{GraphName}_flow_v2.txt` — 스택 기반 비재귀 버전
- 알림 + 로그에 match: YES/NO 표시
- Beyond Compare 등으로 diff 가능
