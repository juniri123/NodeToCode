# FN2CFlowBuilder 코드 흐름 정리

대상 파일:

- `Source/Public/Core/N2CFlowBuilder.h`
- `Source/Private/Core/N2CFlowBuilder.cpp`
- `Source/Public/Models/Python/N2CFlowModel.h`
- `Source/Private/Models/N2CFlowModel.cpp`

이 문서는 예전에 짠 `FN2CFlowBuilder`가 어떤 순서로 Blueprint 그래프를 `flow text` / `flow json`으로 바꾸는지 다시 기억하기 위한 코드 맵이다.

---

## 1. 전체 역할

`FN2CFlowBuilder`는 Unreal `UK2Node` 목록 또는 `UEdGraph`를 받아서 중간 모델인 `FN2CFlowData`를 만든 뒤, 이를 텍스트나 JSON으로 직렬화한다.

핵심 목표는 Blueprint 실행 핀 흐름을 사람이 읽을 수 있는 실행 순서로 펼치는 것이다. Blueprint 실행 그래프는 단순 트리가 아니라 DAG라서, 여러 분기가 같은 노드로 합류하는 경우가 있다. 이 합류 노드를 그대로 DFS 출력하면 합류 이후 로직이 여러 번 중복 출력된다.

그래서 이 구현은 합류 노드를 `Common Step`으로 따로 분리하고, 본문 흐름에는 `Placeholder`만 남긴다. 이후 필요하면 `Merging Point`를 추가해서 어디서 합류되는지 표시한다.

---

## 2. 용어 정리

### DAG

`DAG`는 `Directed Acyclic Graph`의 줄임말이다. 한국어로는 방향 비순환 그래프다.

- `Directed`: 연결에 방향이 있다. Blueprint 실행 핀 흐름도 `A 실행 후 B`처럼 방향이 있다.
- `Acyclic`: 같은 경로를 따라가다가 다시 자기 자신으로 돌아오는 순환이 없다는 뜻이다.
- `Graph`: 노드와 링크로 구성된 구조다.

이 코드에서 Blueprint 실행 흐름을 DAG처럼 설명한 이유는, 일반적인 함수 실행 그래프가 `Entry -> Branch -> CallFunction -> Return`처럼 방향이 있고, 여러 분기가 다시 같은 노드로 합류할 수 있기 때문이다.

다만 현재 구현이 실제 입력 그래프를 순수 DAG로 검증하는 것은 아니다. `FN2CFlowBuilder`에는 cycle detection이나 visited guard가 없다. 즉 "Blueprint 실행 흐름은 DAG다"라기보다는 "현재 flow builder는 실행 흐름이 DAG에 가깝다고 가정하고 처리한다"가 더 정확하다.

트리와 DAG의 차이가 중요하다.

```text
Tree:
  A
  ├─ B
  │  └─ D
  └─ C
     └─ E

DAG:
  A
  ├─ B ┐
  └─ C ┴─ D
```

트리에서는 부모가 하나뿐이라 단순 DFS로 출력해도 중복이 잘 생기지 않는다. DAG에서는 `B`와 `C`가 같은 `D`로 합류할 수 있다. 이때 각 분기를 단순히 끝까지 DFS 출력하면 `D` 이후 로직이 두 번 출력될 수 있다.

### Cycle

`Cycle`은 어떤 경로를 따라가다가 다시 이전 노드로 돌아오는 순환 구조다.

```text
A -> B -> C
     ^    |
     |    v
     +----+
```

Blueprint에서는 loop 계열 노드, macro 확장, 게이트/멀티게이트성 흐름, 또는 사용자가 구성한 실행 핀 연결 방식 때문에 순환처럼 보이는 구조가 생길 수 있다. Unreal이 모든 형태의 실행 cycle을 어떤 단계에서 금지한다고 이 코드가 가정하면 위험하다.

현재 `BuildExecFlow()`는 각 Step의 `OutLinkIdx`를 올리면서 exec-out 링크를 한 번씩 처리하므로, 단순 cycle이 들어와도 빌드 루프 자체가 즉시 무한 루프가 되지 않을 수는 있다. 하지만 `Next`나 `Branches`에 이미 방문한 Step을 다시 연결할 수 있다.

예:

```text
A -> B -> A
```

이 경우 내부 Step 연결이 다음처럼 될 수 있다.

```text
A.Next = B
B.Next = A
```

이렇게 Step 그래프에 cycle이 생기면 이후 단계가 위험해진다.

- 기존 `PrintSteps()`는 재귀라 무한 재귀가 될 수 있다.
- `CollectCommonPlaceholders()`도 재귀라 무한 재귀가 될 수 있다.
- `PrintSteps_01()`처럼 스택 기반이어도 visited guard가 없으면 같은 Step을 계속 순회할 수 있다.
- `GetParentChain()`도 `FromPins[0]`을 따라 부모를 올라가므로 parent chain에 cycle이 있으면 빠져나오지 못할 수 있다.

따라서 현재 코드는 "순수 DAG를 강제한다"가 아니라 "순환이 없는 실행 흐름을 기대한다"에 가깝다. cycle 대응이 필요하면 `BuildExecFlow()`에서 방문 상태를 두거나, 최소한 출력/후처리 순회 함수들에 visited guard를 넣어야 한다.

### 왜 단순 visited 체크를 뺐을 가능성이 큰가

이 flow builder에서 "이미 방문한 노드"는 무조건 스킵하면 안 된다. Blueprint 실행 그래프에서는 같은 노드에 다시 도달했다는 사실이 두 가지 의미를 가질 수 있기 때문이다.

1. 진짜 cycle이라서 더 따라가면 안 되는 경우
2. 여러 분기가 같은 노드로 합류한 정상 흐름인 경우

현재 Common Step / Placeholder 설계는 2번을 표현하기 위해 존재한다. 합류 노드는 두 번째 incoming path를 만났을 때 비로소 의미가 생긴다.

예:

```text
Entry
  -> Branch
     -> TruePath  -> X
     -> FalsePath -> X
```

`X`는 두 경로가 공유하는 합류 노드다. 여기서 단순 visited 체크를 다음처럼 넣으면 문제가 생긴다.

```text
if Visited.Contains(NextStep):
    continue
Visited.Add(NextStep)
```

`TruePath -> X`에서 `X`를 방문 처리한 뒤, `FalsePath -> X`를 만났을 때 그냥 스킵하게 된다. 그러면 다음 정보가 사라진다.

- `X->FromPins`에 두 번째 incoming pin이 기록되지 않는다.
- `X->IsCommonStep()` 판단에 필요한 다중 exec-in 흐름이 제대로 표현되지 않는다.
- `CommonPlaceholder`가 만들어지지 않는다.
- `CommonSteps` 섹션으로 분리해야 할 로직이 누락되거나, 한쪽 분기만 흐름을 가진 것처럼 보인다.
- 이후 `BuildPlaceholderGroups()`와 `FindLCA()`가 사용할 placeholder parent chain도 부족해진다.

즉 이 코드에서 중요한 것은 "노드를 한 번만 방문했는가"가 아니라 "각 exec link를 흐름 표현에 반영했는가"다. 그래서 현재 구현은 Step마다 `OutLinkIdx`를 두고 outlink 단위로 처리한다. 합류 노드를 다시 만나는 것은 버그가 아니라 placeholder를 만들 기회일 수 있다.

cycle 방지를 넣어야 한다면 단순 전역 visited set보다 더 세밀해야 한다.

- `VisitedNode`가 아니라 `VisitedEdge` 또는 `ActivePath` 기준으로 봐야 한다.
- 현재 DFS call stack 안에 이미 있는 노드를 다시 만난 경우만 cycle로 볼 수 있다.
- 이미 완료된 노드를 다시 만난 경우는 합류일 수 있으므로 Common Step / Placeholder 처리로 넘겨야 한다.
- 출력/후처리 순회에서는 cycle guard를 넣더라도 "스킵" 대신 `[Cycle Reference]` 같은 표시를 남겨야 흐름이 사라지지 않는다.

### DFS

`DFS`는 `Depth First Search`의 줄임말이다. 한국어로는 깊이 우선 탐색이다.

한 노드에서 갈 수 있는 첫 번째 자식 경로를 끝까지 따라간 뒤, 다시 돌아와 다음 자식 경로를 처리하는 방식이다.

이 코드의 출력 순서도 기본적으로 DFS다.

```text
현재 Step 출력
  -> 첫 번째 Branch 전체 출력
  -> 두 번째 Branch 전체 출력
  -> Next 출력
```

`N2CFlowBuilder.cpp`의 기존 `PrintSteps()`는 재귀 DFS이고, `N2CFlowBuilder_01.cpp`의 `PrintSteps_01()`은 같은 순서를 스택으로 흉내 낸 비재귀 DFS다.

### LCA

`LCA`는 `Lowest Common Ancestor`의 줄임말이다. 한국어로는 최저 공통 조상이다.

여러 노드가 있을 때, 그 노드들의 부모 체인을 위로 거슬러 올라가며 찾을 수 있는 가장 가까운 공통 부모를 뜻한다.

예:

```text
        A
      /   \
     B     C
    / \   / \
   D   E F   G
```

`D`와 `E`의 LCA는 `B`다. `D`와 `G`의 LCA는 `A`다.

이 코드에서는 여러 `Placeholder`가 같은 Common Step을 가리킬 때, 그 placeholder들이 어느 지점에서 다시 합류 표시를 해야 하는지 찾기 위해 LCA를 쓴다.

대략 이런 목적이다.

```text
BranchParent
  ├─ Path A -> Placeholder(Common X)
  └─ Path B -> Placeholder(Common X)

LCA를 찾고, 적절한 위치에 Merging Point를 끼워 넣는다.
```

### Common Step

`Common Step`은 exec-in 링크가 2개 이상인 실제 합류 노드다.

예를 들어 `Branch True`와 `Branch False`가 모두 같은 `CallFunction_X`로 이어지면, `CallFunction_X`는 여러 실행 경로에서 공유되는 노드다. 이 노드를 각 분기 본문에 그대로 출력하면 이후 로직이 중복 출력될 수 있다.

그래서 `BuildExecFlow()`는 이런 Step을 `CommonSteps` 맵에 따로 등록한다.

### Placeholder

`Placeholder`는 Common Step을 본문에 직접 연결하지 않기 위해 만든 가짜 Step이다.

본문 흐름에는:

```text
... -> Placeholder for CommonStep_X
```

처럼 표시하고, 실제 `CommonStep_X`의 내부 흐름은 아래쪽 `Common Steps` 섹션에서 한 번만 출력한다.

### Merging Point

`Merging Point`는 여러 placeholder가 어느 위치에서 합쳐지는지 보여주기 위해 후처리 단계에서 삽입하는 가짜 Step이다.

`Common Step`은 실제 Blueprint 노드에 대응하는 합류 로직이고, `Merging Point`는 출력 가독성을 위한 표시 노드다. 둘은 역할이 다르다.

### fallthrough

`fallthrough`는 switch-case 계열에서 어떤 case가 명시적으로 끊기지 않고 다음 case 흐름으로 이어지는 형태를 말한다.

이 코드의 fallthrough 처리는 모든 Blueprint switch에 적용되는 일반 규칙이 아니다. 여러 case placeholder가 같은 callstack을 공유하는 특정 출력 패턴을 사람이 읽기 좋게 표시하기 위한 예외 휴리스틱이다.

그래서 `IsSwitchFallthroughCase()`와 `CreateFallthroughMergePoint()`는 "switch 처리"라기보다 "switch 계열의 특정 fallthrough-like 패턴 처리"로 봐야 한다. 조건을 일부러 좁게 잡고 있으며, 일반 branch merge보다 신뢰도가 낮은 특수 처리다.

---

## 3. 공개 진입점

헤더의 공개 API는 크게 세 갈래다.

```cpp
BuildFlowDataFromGraph(...)
BuildFlowDataFromNodes(...)

BuildFlowJsonFromGraph(...)
BuildFlowJsonFromNodes(...)

BuildFlowTextFromGraph(...)
BuildFlowTextFromNodes(...)
```

실제 핵심은 `BuildFlowDataFromNodes()`다. 나머지는 입력 형태가 Graph냐 Nodes냐, 출력 형태가 Data냐 JSON이냐 Text냐의 차이다.

대표 호출 흐름:

```text
BuildFlowTextFromNodes
  -> BuildFlowDataFromNodes
     -> BuildNodesFromK2Nodes
     -> BuildStepsFromNodes
     -> FindEntryStep
     -> BuildExecFlow
     -> ResolveMergingPoints
  -> FlowDataToTextLines
     -> PrintSteps
     -> PrintCommonSteps
```

JSON 출력은 `FlowDataToTextLines` 대신 `FlowDataToJsonObject`를 탄다.

---

## 4. 주요 데이터 모델

`FN2CFlowData`는 흐름 생성 결과를 담는 컨테이너다.

```cpp
struct FN2CFlowData
{
    TMap<FString, TSharedPtr<N2CFlow::Node>> NodesByName;
    TMap<FString, TSharedPtr<N2CFlow::Step>> StepsByKey;
    TSharedPtr<N2CFlow::Step> EntryStep;
    TMap<FString, TSharedPtr<N2CFlow::Step>> CommonSteps;
};
```

### Node

`N2CFlow::Node`는 K2Node 하나를 평탄화한 모델이다.

- `Name`, `Guid`
- `ExecInPins`, `ExecOutPins`
- `ExecInLinks`, `ExecOutLinks`
- `DataInPins`, `DataOutPins`
- `DataInLinks`, `DataOutLinks`

여기서는 아직 실행 흐름 트리를 만들지 않는다. 노드와 핀, 링크 정보를 캐시하는 단계다.

### Step

`N2CFlow::Step`은 출력 흐름을 만들기 위한 실행 단위다. 대체로 Node 하나에 Step 하나가 먼저 만들어진다.

중요 필드:

- `Key`: Step 식별자. 기본은 Node 이름이고, placeholder/merging point는 접미사가 붙는다.
- `Node`: 이 Step이 가리키는 원본 Node.
- `FromPins`: 이 Step으로 들어온 실행 링크의 출발 핀들.
- `Branches`: 분기 자식들. 부모 노드의 exec-out 핀이 여러 개인 경우 여기에 붙는다.
- `Next`: 직렬 다음 Step.
- `CommonPlaceholders`: Common Step을 가리키는 placeholder 목록.
- `LogicDepth`: 텍스트 출력 들여쓰기 깊이.
- `bIsBranched`: 분기 경로 안의 Step인지 여부.
- `bIsCommonPlaceholder`: Common Step 대신 본문에 남겨둔 placeholder인지 여부.
- `bIsMergingPoint`: 합류 지점을 표시하기 위해 만든 synthetic Step인지 여부.
- `bIsCommentOut`: merge 처리 후 placeholder를 주석처럼 출력할지 여부.
- `bIsFallthrough`: switch fallthrough 특수 처리 여부.
- `OutLinkIdx`: `BuildExecFlow()`가 outlink를 순회할 때 쓰는 런타임 인덱스.

---

## 5. BuildFlowDataFromNodes 흐름

`BuildFlowDataFromNodes()`가 전체 빌드의 중심이다.

1. `OutData`를 새로 초기화한다.
2. `BuildNodesFromK2Nodes()`로 `UK2Node`들을 `N2CFlow::Node`로 변환한다.
3. `BuildStepsFromNodes()`로 모든 Node에 대해 Step을 미리 만든다.
4. `FindEntryStep()`으로 시작 Step을 찾는다.
5. Entry Step에 `MakeAsEntry(0)`를 호출해 `OutLinkIdx = -1`, `LogicDepth = 0`으로 초기화한다.
6. `BuildExecFlow()`로 `Next` / `Branches` / `CommonSteps` / placeholder를 구성한다.
7. `ResolveMergingPoints()`를 Entry 흐름과 각 Common Step 흐름에 대해 돌린다.

현재 `FindEntryStep()`은 `Pair.Key.Contains("K2Node_FunctionEntry")`만 본다. EventGraph나 CustomEvent까지 제대로 지원하려면 이 부분을 확장해야 한다.

---

## 6. BuildNodesFromK2Nodes

이 함수는 Unreal 그래프 노드들을 내부 `Node` 모델로 바꾼다.

처리 방식:

1. 각 `UK2Node`의 `GetName()`과 `NodeGuid`를 읽어 `N2CFlow::Node` 생성.
2. 노드의 모든 `Pins`를 순회한다.
3. 핀 카테고리가 `"exec"`이면 실행 핀, 아니면 데이터 핀으로 분류한다.
4. 핀 방향에 따라 In/Out 배열에 넣는다.
5. `Pin->LinkedTo`를 순회해서 `Link(LocalPin, RemotePin)` 또는 `Link(RemotePin, LocalPin)`을 만든다.

주의점:

- 링크는 핀 방향 기준으로 저장된다.
- exec 입력 핀의 링크는 `ExecInLinks`에 `Remote -> Local` 형태로 들어간다.
- exec 출력 핀의 링크는 `ExecOutLinks`에 `Local -> Remote` 형태로 들어간다.
- 이 단계에서는 graph traversal을 하지 않는다.

---

## 7. BuildStepsFromNodes

모든 `Node`에 대해 `Step`을 하나씩 미리 만든다.

```text
NodesByName:
  K2Node_FunctionEntry_0 -> Node
  K2Node_CallFunction_1  -> Node

StepsByKey:
  K2Node_FunctionEntry_0 -> Step(Node = FunctionEntry)
  K2Node_CallFunction_1  -> Step(Node = CallFunction)
```

이렇게 미리 만들어두기 때문에 `BuildExecFlow()`는 링크의 `ToPin.NodeName`으로 다음 Step을 바로 찾을 수 있다.

---

## 8. BuildExecFlow

실행 흐름 트리를 구성하는 핵심 함수다.

입력:

- `NodesByName`: 전체 Node 목록
- `StepsByKey`: 전체 Step 레지스트리
- `CommonSteps`: 합류 노드 저장소
- `EntryStep`: 이번 traversal의 시작점

출력 효과:

- 각 Step의 `Next`, `Branches`가 연결된다.
- 각 Step의 `FromPins`, `LogicDepth`, `bIsBranched`가 채워진다.
- Common Step과 Common Placeholder가 생성된다.

### 순회 방식

메인 traversal은 스택 기반 DFS다.

```text
Stack = [EntryStep]
LogicDepth = EntryStep.LogicDepth

while Stack not empty:
    CurrentStep = Stack.Last()
    CurrentStep.NextOutlinkIdx()

    if CurrentStep has processed all outlinks:
        Stack.Pop()
        LogicDepth = CurrentStep.PopLogicDepth(LogicDepth)
        continue

    Link = CurrentStep.GetOutlink()
    NextStep = StepsByKey[Link.ToPin.NodeName]
    NextStep.AppendFromPin(Link)

    if NextStep.IsCommonStep():
        handle common step by placeholder
    else:
        connect real NextStep
        Stack.Add(NextStep)
        LogicDepth = NextStep.PushLogicDepth(LogicDepth)
```

`OutLinkIdx`는 각 Step이 자기 exec-out 링크를 어디까지 처리했는지 기억한다. `MakeAsEntry()`가 `-1`로 초기화하고, 루프 첫 줄에서 `NextOutlinkIdx()`로 0부터 처리한다.

### 분기 판단

`Step::HasBranches()`는 `Node->ExecOutPins.Num() > 1`을 본다. 즉 실제 링크 수가 아니라 exec 출력 핀 개수 기준이다.

다음 Step을 연결할 때:

- 부모가 분기 노드이면 `CurrentStep->AppendBranch(NextStep)`
- 아니면 `CurrentStep->SetNext(NextStep)`

`NextStep->bIsBranched = CurrentStep->HasBranches()`로 설정되므로, "분기 부모에서 나온 자식"이 branched step으로 출력된다.

### Common Step 처리

`NextStep->IsCommonStep()`은 `HasMultipleExecInlink() && !bIsMergingPoint`다. 즉 exec-in 링크가 2개 이상인 노드는 합류 노드로 보고 Common Step 처리한다.

처리 방식:

1. `NextStep->AppendFromPin(Link)`로 incoming pin을 기록한다.
2. 아직 `CommonSteps`에 없으면 `CommonSteps.Add(NextNodeName, NextStep)` 한다.
3. 처음 등록된 Common Step은 `NextStep->MakeAsEntry(0)` 후 `BuildExecFlow(..., NextStep, ...)`로 Common Step 내부 흐름을 별도 구성한다.
4. 본문 흐름에는 실제 `NextStep`을 연결하지 않고 `CommonPlaceholder`를 새로 만든다.
5. Placeholder는 `StepsByKey`에 추가되고 `NextStep->CommonPlaceholders`에도 기록된다.
6. Placeholder도 부모가 분기 노드이면 `Branches`, 아니면 `Next`에 연결된다.

이 구조 때문에 본문에는 "여기서 common logic으로 감"이라는 placeholder만 남고, 실제 common logic은 `Common Steps` 섹션에 따로 출력된다.

주의점:

- `BuildExecFlow()`의 메인 순회는 스택 기반이지만, Common Step 최초 등록 시에는 `BuildExecFlow()`를 재귀 호출한다.
- 이미 등록된 Common Step을 다시 만나면 common flow는 다시 만들지 않고 placeholder만 추가한다.

---

## 9. ResolveMergingPoints

`BuildExecFlow()`가 만든 placeholder들을 보고, 합류 지점 표시용 `Merging Point` Step을 삽입하는 후처리다.

흐름:

1. `CollectCommonPlaceholders()`로 실행 트리에서 placeholder를 수집한다.
2. common step key별로 placeholder 목록을 묶는다.
3. `BuildPlaceholderGroups()`로 placeholder들을 callstack 기준으로 그룹핑한다.
4. 각 그룹에 대해 일반 merge인지 switch fallthrough인지 판단한다.
5. `CreateNormalMergePoint()` 또는 `CreateFallthroughMergePoint()`로 synthetic merge Step을 만든다.

`BuildFlowDataFromNodes()`에서는 Entry 흐름에 대해 한 번, 각 Common Step 흐름에 대해 다시 `ResolveMergingPoints()`를 호출한다.

---

## 10. Placeholder 그룹핑과 LCA

`BuildPlaceholderGroups()`는 placeholder들의 parent chain을 문자열 callstack으로 만들고, 포함 관계를 이용해서 leader placeholder를 고른다.

관련 함수:

- `GetParentChain()`: placeholder에서 시작해 `FromPins[0].NodeName`을 따라 부모 체인을 만든다.
- `FindLCA()`: 여러 placeholder parent chain을 root -> child 방향으로 뒤집은 뒤 마지막 공통 Step을 찾는다.
- `IsSwitchFallthroughCase()`: 모든 switch가 아니라 특정 fallthrough-like 출력 패턴인지 판단한다.

일반 merge:

- `FindLCA()` 결과를 merge point 후보로 쓴다.
- 후보의 기존 `Next` 앞에 merge point를 끼워 넣는다.

switch fallthrough-like 예외 처리:

모든 switch에 적용되는 일반 정책이 아니다. 여러 case placeholder가 같은 callstack을 공유하고 default 경로가 섞이지 않는 좁은 패턴만 잡는다.

- LCA 이름에 `switch`가 포함되어야 한다.
- placeholder들의 callstack이 모두 같아야 한다.
- `Default` pin placeholder가 포함되면 fallthrough로 보지 않는다.
- 마지막 placeholder를 merge point 후보로 쓴다.

주의점:

- 그룹핑은 `FString::Contains` 기반이라 이름이 우연히 포함되는 경우 false positive 가능성이 있다.
- `GetParentChain()`은 `FromPins[0]`만 따라간다. common step flatten 이후 부모 하나를 가정한 설계다.

---

## 11. Merge Point 생성

### CreateNormalMergePoint

일반 merge point는 LCA 후보의 `Next` 앞에 삽입된다.

```text
before:
  MergingPointCandidate -> OldNext

after:
  MergingPointCandidate -> MergePoint -> OldNext
```

추가 처리:

- MergePoint key는 `{CommonStepKey}_Merging_[idx|idx]` 형태다.
- MergePoint의 Node는 CommonStep의 Node를 공유한다.
- `bIsMergingPoint = true`
- `OldNext->FromPins` 중 후보를 가리키던 부모 이름을 MergePoint key로 바꾼다.
- 그룹에 속한 placeholder들은 `bIsCommentOut = true`로 표시한다.
- MergePoint는 어떤 placeholder들이 합쳐졌는지 `CommonPlaceholders`에 기록한다.

### CreateFallthroughMergePoint

특정 switch fallthrough-like 패턴용 merge point는 마지막 placeholder 뒤에 붙는다. 이 함수는 switch 전체에 대한 일반 merge 정책이 아니라, 위 조건을 만족한 예외 케이스에만 사용된다.

```text
MergingPointCandidate -> MergePoint
```

추가 처리:

- `LogicDepth = MergingPointCandidate->LogicDepth + 1`
- placeholder들은 `bIsCommentOut = true`, `bIsFallthrough = true`

---

## 12. Text 출력

`FlowDataToTextLines()`는 두 섹션을 만든다.

```text
=========== Steps ===========
EntryStep부터 출력

=========== Common Steps ===========
CommonSteps에 등록된 flow 출력
```

기존 `N2CFlowBuilder.cpp`의 `PrintSteps()`는 재귀 출력이다.

```text
PrintSingleStep(Step)
for Branch in Step.Branches:
    PrintSteps(Branch)
PrintSteps(Step.Next)
```

즉 출력 순서는:

1. 현재 Step
2. 모든 Branch를 배열 순서대로 깊게 출력
3. Next 출력

현재 별도 파일 `N2CFlowBuilder_01.cpp`에는 이 출력만 스택 기반으로 바꾼 `PrintSteps_01()`이 있다. `ExecuteSaveFlowText()`는 v1/v2를 모두 저장해서 결과 문자열이 같은지 비교한다.

---

## 13. JSON 출력

`FlowDataToJsonObject()`는 `FN2CFlowData`를 JSON object로 바꾼다.

저장 필드:

- `entry_step_key`
- `common_step_keys`
- `steps`
- `nodes`

각 Step/Node의 실제 JSON 변환은 `N2CFlowModel.cpp`의 `ToJsonObject()` 구현에 위임한다.

---

## 14. 상태값이 헷갈릴 때 보는 기준

### `bIsBranched`

"이 Step이 분기 자식으로 출력되어야 하는가"를 뜻한다. 부모 노드가 여러 exec-out 핀을 가지면 자식 Step에 true가 들어간다.

### `Branches` vs `Next`

- 부모가 분기 노드이면 자식은 `Branches`에 들어간다.
- 부모가 일반 직렬 노드이면 자식은 `Next`에 들어간다.

### `CommonSteps`

exec-in 링크가 2개 이상인 실제 합류 Step들을 담는다. 본문에서 중복 출력하지 않기 위해 따로 출력한다.

### `CommonPlaceholder`

본문 흐름에 남겨두는 가짜 Step이다. 실제 common logic은 `Common Steps` 섹션에 있고, placeholder는 "여기서 그 common logic을 참조한다"는 표시다.

### `MergingPoint`

placeholder들이 어디서 합류되는지 보여주기 위해 후처리에서 삽입하는 가짜 Step이다. Common Step 자체와는 다르다.

### `OutLinkIdx`

`BuildExecFlow()` 순회 중 현재 Step의 exec-out 링크 처리 위치다. JSON으로 저장하지 않는 런타임 상태다.

---

## 15. 현재 남아 있는 개선 후보

1. `FindEntryStep()`은 FunctionEntry만 찾는다. EventGraph, CustomEvent 지원을 추가하려면 여기부터 봐야 한다.
2. `BuildPlaceholderGroups()`의 `FString::Contains` 기반 그룹핑은 callstack token 단위 비교로 바꾸는 편이 안전하다.
3. `PrintSteps()`는 기존 v1에서는 재귀다. v2의 `PrintSteps_01()`이 스택 기반 비교용으로 존재한다.
4. `BuildExecFlow()`는 Common Step 최초 처리 시 재귀 호출이 남아 있다. 현재 목표가 출력 재귀 제거였으면 문제는 아니지만, 완전 비재귀화를 하려면 이 부분도 봐야 한다.
5. `CollectCommonPlaceholders()`도 재귀다. 깊은 그래프 안정성을 끝까지 보려면 스택 기반으로 바꿀 수 있다.
6. `HasBranches()`가 링크 수가 아니라 exec-out pin 수를 본다. 링크가 없는 exec-out pin까지 분기 노드로 취급하는 게 의도인지 확인할 만하다.

---

## 16. 빠른 디버깅 순서

flow text가 이상할 때는 이 순서로 보면 된다.

1. `BuildNodesFromK2Nodes()` 결과에서 Node의 `ExecOutLinks` / `ExecInLinks`가 맞는지 확인한다.
2. `BuildStepsFromNodes()` 후 `StepsByKey`에 모든 node key가 들어갔는지 확인한다.
3. `FindEntryStep()`이 올바른 시작 노드를 잡았는지 확인한다.
4. `BuildExecFlow()`에서 문제가 되는 Step의 `Next`, `Branches`, `FromPins`, `LogicDepth`, `bIsBranched`를 본다.
5. 합류 노드라면 `CommonSteps`에 등록됐는지, placeholder가 `StepsByKey`와 `CommonPlaceholders`에 모두 들어갔는지 본다.
6. merge point가 이상하면 `BuildPlaceholderGroups()`의 callstack과 `FindLCA()` 결과를 확인한다.
7. 최종 출력 차이는 `PrintSingleStep()`과 `PrintSteps()` 또는 `PrintSteps_01()` 순서를 비교한다.
