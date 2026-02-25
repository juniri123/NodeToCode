#include "Core/N2CFlowBuilder.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node.h"
#include "Models/Python/N2CFlowModel.h"
#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#pragma region ODS
namespace
{
    // 로직 depth만큼 indent prefix 생성
    static FString MakeIndentPrefix(int32 Depth)
    {
        FString Result;
        for (int32 i = 0; i < Depth; ++i)
        {
            Result += TEXT("│   ");
        }
        return Result;
    }

    // FGuid -> 문자열 (Digits 포맷)
    FString GuidToString(const FGuid& Guid)
    {
        return Guid.ToString(EGuidFormats::Digits);
    }

    // 핀 표시 이름 추출
    FString PinDisplayName(const UEdGraphPin* Pin)
    {
        return Pin ? Pin->GetDisplayName().ToString() : FString();
    }

    // Step 키로 찾기
    TSharedPtr<N2CFlow::Step> FindStepByKey(const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey, const FString& Key)
    {
        const TSharedPtr<N2CFlow::Step>* Found = StepsByKey.Find(Key);
        return Found ? *Found : nullptr;
    }

    // 단일 Step을 텍스트 라인으로 변환
    TArray<FString> PrintSingleStep(const TSharedPtr<N2CFlow::Step>& Step, bool bWithIndent)
    {
        TArray<FString> Lines;
        if (!Step.IsValid() || !Step->Node.IsValid())
        {
            return Lines;
        }

        const FString IndentPrefix = bWithIndent ? MakeIndentPrefix(Step->LogicDepth) : TEXT("");

        FString BranchLabel;
        if (Step->FromPins.Num() > 0)
        {
            const FString PrefixIcon = Step->bIsBranched ? TEXT("➡️ ") : TEXT("");
            TArray<FString> Parts;
            for (const N2CFlow::Pin& Pin : Step->FromPins)
            {
                Parts.Add(FString::Printf(TEXT("%s📌%s::%s from (📋%s::%s)"),
                                        *PrefixIcon,
                                        *Pin.Name,
                                        *Pin.Guid,
                                        *Pin.NodeName,
                                        *Pin.NodeGuid));
            }
            BranchLabel = FString::Join(Parts, TEXT(", "));
        }

        const FString CommentOut = Step->bIsCommentOut ? TEXT("//") : TEXT("");

        // Common Logic Placeholder 처리
        if (Step->bIsCommonPlaceholder)
        {
            if (Step->bIsBranched)
            {
                Lines.Add(IndentPrefix + BranchLabel);
                const FString CommonIndent = MakeIndentPrefix(Step->LogicDepth + 1);
                Lines.Add(FString::Printf(TEXT("%s%s↪️ Placeholder::%s for 📋%s::%s"),
                                        *CommonIndent,
                                        *CommentOut,
                                        *Step->Key,
                                        *Step->Node->Name,
                                        *Step->Node->Guid));
            }
            else
            {
                Lines.Add(FString::Printf(TEXT("%s%s%s → ↪️ Placeholder::%s for 📋%s::%s"),
                                        *IndentPrefix,
                                        *CommentOut,
                                        *BranchLabel,
                                        *Step->Key,
                                        *Step->Node->Name,
                                        *Step->Node->Guid));
            }
            return Lines;
        }

        // common step 출력
        if (Step->IsCommonStep())
        {
            Lines.Add(IndentPrefix + TEXT("----- From Pins -----"));
            for (const N2CFlow::Pin& Pin : Step->FromPins)
            {
                Lines.Add(FString::Printf(TEXT("%s📌%s::%s from (📋%s::%s)"),
                                        *IndentPrefix,
                                        *Pin.Name,
                                        *Pin.Guid,
                                        *Pin.NodeName,
                                        *Pin.NodeGuid));
            }
            Lines.Add(IndentPrefix + TEXT("----- Placeholders -----"));
            for (const TSharedPtr<N2CFlow::Step>& Placeholder : Step->CommonPlaceholders)
            {
                if (Placeholder.IsValid())
                {
                    Lines.Add(IndentPrefix + Placeholder->Key);
                }
            }
            Lines.Add(IndentPrefix + TEXT("---------------------"));
            Lines.Add(FString::Printf(TEXT("%s📋%s::%s"), *IndentPrefix, *Step->Node->Name, *Step->Node->Guid));
            return Lines;
        }

        // merging point 출력
        if (Step->bIsMergingPoint)
        {
            Lines.Add(IndentPrefix + TEXT("Merging Point ") + Step->Key);
            Lines.Add(IndentPrefix + TEXT("----- Merged Placeholders -----"));
            for (const TSharedPtr<N2CFlow::Step>& Placeholder : Step->CommonPlaceholders)
            {
                if (Placeholder.IsValid() && Placeholder->Node.IsValid())
                {
                    Lines.Add(FString::Printf(TEXT("%s%s (📋%s::%s)"),
                                            *IndentPrefix,
                                            *Placeholder->Key,
                                            *Placeholder->Node->Name,
                                            *Placeholder->Node->Guid));
                }
            }
            Lines.Add(IndentPrefix + TEXT("---------------------"));
            return Lines;
        }

        // branched step 출력
        if (Step->bIsBranched)
        {
            Lines.Add(IndentPrefix + BranchLabel);
            const FString BranchedIndent = MakeIndentPrefix(Step->LogicDepth + 1);
            Lines.Add(FString::Printf(TEXT("%s%s📋%s::%s"),
                                    *BranchedIndent,
                                    *CommentOut,
                                    *Step->Node->Name,
                                    *Step->Node->Guid));
            return Lines;
        }

        // 일반적인 경우
        Lines.Add(FString::Printf(TEXT("%s%s%s → 📋%s::%s"),
                                *CommentOut,
                                *IndentPrefix,
                                *BranchLabel,
                                *Step->Node->Name,
                                *Step->Node->Guid));
        return Lines;
    }

    // 실행 흐름을 문자열 리스트로 출력
    TArray<FString> PrintSteps(const TSharedPtr<N2CFlow::Step>& Step)
    {
        TArray<FString> Lines;
        if (!Step.IsValid())
        {
            return Lines;
        }

        Lines.Append(PrintSingleStep(Step, true));
        for (const TSharedPtr<N2CFlow::Step>& Child : Step->Branches)
        {
            Lines.Append(PrintSteps(Child));
        }
        Lines.Append(PrintSteps(Step->Next));
        return Lines;
    }

    // common_steps에 저장된 모든 step을 순회하며 출력
    TArray<FString> PrintCommonSteps(const TMap<FString, TSharedPtr<N2CFlow::Step>>& CommonSteps)
    {
        TArray<FString> Lines;
        if (CommonSteps.Num() == 0)
        {
            Lines.Add(TEXT("[INFO] No common step to display."));
            return Lines;
        }

        int32 Num = 0;
        for (const TPair<FString, TSharedPtr<N2CFlow::Step>>& Pair : CommonSteps)
        {
            Lines.Add(FString::Printf(TEXT("[#%d]"), Num));
            Lines.Append(PrintSteps(Pair.Value));
            Lines.Add(TEXT(""));
            ++Num;
        }
        return Lines;
    }

    // 부모 체인 수집 (child -> root)
    // from 핀이 여러 개인 경우 common step으로 flatten 되었으므로 부모 1개 보장
    TArray<TSharedPtr<N2CFlow::Step>> GetParentChain(const TSharedPtr<N2CFlow::Step>& Step,
                                                    const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey)
    {
        TArray<TSharedPtr<N2CFlow::Step>> Chain;
        TSharedPtr<N2CFlow::Step> Cur = Step;
        while (Cur.IsValid())
        {
            Chain.Add(Cur);
            if (Cur->FromPins.Num() > 0)
            {
                const N2CFlow::Pin& FromPin = Cur->FromPins[0];
                Cur = FindStepByKey(StepsByKey, FromPin.NodeName);
            }
            else
            {
                Cur.Reset();
            }
        }
        return Chain;
    }

    // placeholder들의 LCA(최저 공통 조상) 찾기
    TSharedPtr<N2CFlow::Step> FindLCA(const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders,
                                    const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey)
    {
        // 각 placeholder의 parent chain 가져오기 (child -> root)
        if (Placeholders.Num() == 0)
        {
            return nullptr;
        }

        // 각 체인을 root -> child 로 뒤집기
        TArray<TArray<TSharedPtr<N2CFlow::Step>>> Chains;
        Chains.Reserve(Placeholders.Num());
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
        {
            TArray<TSharedPtr<N2CFlow::Step>> Chain = GetParentChain(Placeholder, StepsByKey);
            Algo::Reverse(Chain);
            Chains.Add(MoveTemp(Chain));
        }

        // 가장 짧은 체인 길이
        int32 MinLen = MAX_int32;
        for (const TArray<TSharedPtr<N2CFlow::Step>>& Chain : Chains)
        {
            MinLen = FMath::Min(MinLen, Chain.Num());
        }

        TSharedPtr<N2CFlow::Step> Lca;
        for (int32 i = 0; i < MinLen; ++i)
        {
            const TSharedPtr<N2CFlow::Step>& Candidate = Chains[0][i];
            bool bAllMatch = true;
            for (int32 j = 1; j < Chains.Num(); ++j)
            {
                if (Chains[j][i] != Candidate)
                {
                    bAllMatch = false;
                    break;
                }
            }
            if (bAllMatch)
            {
                Lca = Candidate;
            }
            else
            {
                break;
            }
        }

        return Lca;
    }

    // switch fallthrough 케이스 판정 (Python 로직 그대로)
    bool IsSwitchFallthroughCase(const TSharedPtr<N2CFlow::Step>& Lca,
                                const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders,
                                const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey)
    {
        // 조건:
        // 1) LCA 노드 이름에 switch 포함
        // 2) placeholder들의 parent-chain 문자열(callstack)이 모두 동일
        // 3) default branch placeholder가 포함되지 않음
        if (!Lca.IsValid() || !Lca->Node.IsValid())
        {
            return false;
        }

        // --- 조건 1) LCA가 switch 계열인지 확인 ---
        if (!Lca->Node->Name.ToLower().Contains(TEXT("switch")))
        {
            return false;
        }

        // --- 조건 2) placeholder들의 callstackline 이 동일한지 확인 ---
        TSet<FString> Callstacks;
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
        {
            TArray<TSharedPtr<N2CFlow::Step>> Chain = GetParentChain(Placeholder, StepsByKey);
            if (Chain.Num() > 0)
            {
                Chain.RemoveAt(0);
            }
            TArray<FString> Names;
            for (const TSharedPtr<N2CFlow::Step>& Step : Chain)
            {
                if (Step.IsValid() && Step->Node.IsValid())
                {
                    Names.Add(Step->Node->Name);
                }
            }
            Callstacks.Add(FString::Join(Names, TEXT("/")));
        }

        // 모두 동일한지 확인
        if (Callstacks.Num() != 1)
        {
            return false;
        }

        // --- 조건 3) default branch placeholder 배제 ---
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
        {
            for (const N2CFlow::Pin& Pin : Placeholder->FromPins)
            {
                // 정확히 "Default" 핀이 아님
                if (Pin.Name.ToLower() == TEXT("default"))
                {
                    return false;
                }
            }
        }

        return true;
    }

    // placeholder 그룹 1개를 표현하는 데이터 구조
    // merge point 생성에 필요한 모든 정보 포함
    struct FMergingGroup
    {
        TSharedPtr<N2CFlow::Step> CommonStep;
        TSharedPtr<N2CFlow::Step> MergingPointStep;
        TArray<TSharedPtr<N2CFlow::Step>> Placeholders;
        bool bIsFallthrough = false;
    };

    // placeholder들을 callstack 기준으로 그루핑 (Python build_placeholder_groups)
    TArray<FMergingGroup> BuildPlaceholderGroups(const TArray<TSharedPtr<N2CFlow::Step>>& AllPlaceholders,
                                                const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey)
    {
        // parent-chain 수집: {step_key: "A/B/C/..."}
        TMap<FString, FString> CallstackByKey;
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : AllPlaceholders)
        {
            TArray<TSharedPtr<N2CFlow::Step>> Chain = GetParentChain(Placeholder, StepsByKey);
            if (Chain.Num() > 0)
            {
                Chain.RemoveAt(0); // 자기 자신은 제외
            }
            TArray<FString> Names;
            for (const TSharedPtr<N2CFlow::Step>& Step : Chain)
            {
                if (Step.IsValid() && Step->Node.IsValid())
                {
                    Names.Add(Step->Node->Name);
                }
            }
            CallstackByKey.Add(Placeholder->Key, FString::Join(Names, TEXT("/")));
        }

        // 대표 placeholder(leader) 선택
        TArray<TSharedPtr<N2CFlow::Step>> LeaderPlaceholders;
        // candidate를 하나 뽑아 다른 placeholder들과 비교한다.
        for (const TSharedPtr<N2CFlow::Step>& Candidate : AllPlaceholders)
        {
            const FString& CandidateStack = CallstackByKey[Candidate->Key];
            // candidate가 다른 placeholder의 콜스택에 포함되면 리더 아님
            bool bIsLeader = true;
            // 자기 자신을 제외한 나머지 placeholder들의 콜스택과 비교
            for (const TSharedPtr<N2CFlow::Step>& Other : AllPlaceholders)
            {
                if (Other == Candidate)
                {
                    continue;
                }
                const FString& OtherStack = CallstackByKey[Other->Key];
                // 보통은 candidate의 콜스택이 다른 placeholder의 callstack에 포함되면 리더가 될 수 없다.
                if (OtherStack.Contains(CandidateStack))
                {
                    bIsLeader = false;
                    // 콜스택이 동일한 sibling일 경우 리더 1명은 필요
                    // DFS 탐색 특성상 바로 윗 부모에서 시작하는 콜스택이 동일할 수 있어
                    // sibling 중 하나는 리더가 되어야 함
                    if (OtherStack == CandidateStack)
                    {
                        if (!LeaderPlaceholders.Contains(Other) && !LeaderPlaceholders.Contains(Candidate))
                        {
                            bIsLeader = true;
                        }
                    }
                    break;
                }
            }
            if (bIsLeader)
            {
                LeaderPlaceholders.Add(Candidate);
            }
        }

        // 그룹 초기화
        TMap<TSharedPtr<N2CFlow::Step>, TArray<TSharedPtr<N2CFlow::Step>>> Groups;
        for (const TSharedPtr<N2CFlow::Step>& Leader : LeaderPlaceholders)
        {
            Groups.Add(Leader, {});
        }

        // 그룹 내용 채우기
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : AllPlaceholders)
        {
            // 리더는 그룹에 넣기만 하면 됨
            if (LeaderPlaceholders.Contains(Placeholder))
            {
                Groups[Placeholder].Add(Placeholder);
                continue;
            }

            // 리더가 아닌 경우에, parent str이 어떤 리더에 포함되는지 확인하고 그룹에 추가
            const FString& Callstack = CallstackByKey[Placeholder->Key];
            for (const TSharedPtr<N2CFlow::Step>& Leader : LeaderPlaceholders)
            {
                const FString& LeaderStack = CallstackByKey[Leader->Key];
                if (LeaderStack.Contains(Callstack))
                {
                    Groups[Leader].Add(Placeholder);
                    break;
                }
            }
        }

        // 그룹 -> 머지 포인트 후보 구성
        TArray<FMergingGroup> Result;
        for (const TPair<TSharedPtr<N2CFlow::Step>, TArray<TSharedPtr<N2CFlow::Step>>>& Pair : Groups)
        {
            const TSharedPtr<N2CFlow::Step>& Leader = Pair.Key;
            const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders = Pair.Value;
            if (!Leader.IsValid() || !Leader->Node.IsValid())
            {
                continue;
            }

            TSharedPtr<N2CFlow::Step> CommonStep = FindStepByKey(StepsByKey, Leader->Node->Name);
            TSharedPtr<N2CFlow::Step> Lca = FindLCA(Placeholders, StepsByKey);

            // switch fallthrough 케이스면 마지막 placeholder가 merge 후보
            FMergingGroup Group;
            // 여기서 fallthrough 처리
            if (IsSwitchFallthroughCase(Lca, Placeholders, StepsByKey))
            {
                Group.CommonStep = CommonStep;
                Group.MergingPointStep = Placeholders.Num() > 0 ? Placeholders.Last() : nullptr;
                Group.Placeholders = Placeholders;
                Group.bIsFallthrough = true;
            }
            else
            {
                Group.CommonStep = CommonStep;
                Group.MergingPointStep = Lca;
                Group.Placeholders = Placeholders;
            }
            Result.Add(MoveTemp(Group));
        }

        return Result;
    }

    // Create normal merge point and rewire next pointers
    // 일반 머지 포인트 생성 및 next 재배선
    TSharedPtr<N2CFlow::Step> CreateNormalMergePoint(
        TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey,
        const TSharedPtr<N2CFlow::Step>& MergingPointCandidate,
        const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders,
        const TSharedPtr<N2CFlow::Step>& CommonStep)
    {
        if (!MergingPointCandidate.IsValid() || !CommonStep.IsValid())
        {
            return nullptr;
        }

        // 그루핑된 인덱스 검색
        TArray<int32> Indices;
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
        {
            int32 Index = CommonStep->CommonPlaceholders.IndexOfByKey(Placeholder);
            Indices.Add(Index);
        }

        TArray<FString> IndexStrings;
        for (int32 Index : Indices)
        {
            IndexStrings.Add(FString::FromInt(Index));
        }
        const FString GroupedIdxes = FString::Join(IndexStrings, TEXT("|"));

        // --- 1) 기존 parent.next 저장 ---
        TSharedPtr<N2CFlow::Step> OldNext = MergingPointCandidate->Next;

        // --- 2) merge point 생성 ---
        TSharedPtr<N2CFlow::Step> MergePoint = MakeShared<N2CFlow::Step>();
        MergePoint->Key = FString::Printf(TEXT("%s_Merging_[%s]"), *CommonStep->Key, *GroupedIdxes);
        MergePoint->Node = CommonStep->Node;
        MergePoint->bIsMergingPoint = true;
        MergePoint->FromPins.Add(N2CFlow::Pin(TEXT(""), TEXT(""), MergingPointCandidate->Key, TEXT("")));

        // logic depth: old_next 있을 때만 복사
        MergePoint->LogicDepth = OldNext.IsValid() ? OldNext->LogicDepth : MergingPointCandidate->LogicDepth;

        // step registry 등록
        StepsByKey.Add(MergePoint->Key, MergePoint);

        // --- 3) parent.next = merge_point 로 교체 ---
        MergingPointCandidate->Next = MergePoint;
        // --- 4) merge_point.next = old_next ---
        MergePoint->Next = OldNext;

        // --- 5) old_next 의 from_pins 에서 parent → merge_point 로 변경 ---
        if (OldNext.IsValid())
        {
            for (N2CFlow::Pin& Pin : OldNext->FromPins)
            {
                if (Pin.NodeName == MergingPointCandidate->Key)
                {
                    Pin.NodeName = MergePoint->Key;
                }
            }
        }

        // --- 6) placeholder 들 comment-out 처리 ---
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
        {
            MergePoint->RecordCommonPlaceholder(Placeholder);
            Placeholder->bIsCommentOut = true;
        }

        return MergePoint;
    }

    // Create fallthrough merge point (switch-case special)
    // fallthrough 머지 포인트 생성 (switch-case 특수 처리)
    TSharedPtr<N2CFlow::Step> CreateFallthroughMergePoint(
        TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey,
        const TSharedPtr<N2CFlow::Step>& MergingPointCandidate,
        const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders,
        const TSharedPtr<N2CFlow::Step>& CommonStep)
    {
        if (!MergingPointCandidate.IsValid() || !CommonStep.IsValid())
        {
            return nullptr;
        }

        // 그루핑된 인덱스 검색
        TArray<int32> Indices;
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
        {
            int32 Index = CommonStep->CommonPlaceholders.IndexOfByKey(Placeholder);
            Indices.Add(Index);
        }

        TArray<FString> IndexStrings;
        for (int32 Index : Indices)
        {
            IndexStrings.Add(FString::FromInt(Index));
        }
        const FString GroupedIdxes = FString::Join(IndexStrings, TEXT("|"));

        // 이 경우 마지막 placeholder가 candidate로 오며 next는 없다.
        // --- 2) merge point 생성 ---
        TSharedPtr<N2CFlow::Step> MergePoint = MakeShared<N2CFlow::Step>();
        MergePoint->Key = FString::Printf(TEXT("%s_Merging_[%s]"), *CommonStep->Key, *GroupedIdxes);
        MergePoint->Node = CommonStep->Node;
        MergePoint->bIsMergingPoint = true;
        MergePoint->FromPins.Add(N2CFlow::Pin(TEXT(""), TEXT(""), MergingPointCandidate->Key, TEXT("")));

        // logic depth
        MergePoint->LogicDepth = MergingPointCandidate->LogicDepth + 1;

        // step registry 등록
        StepsByKey.Add(MergePoint->Key, MergePoint);

        MergingPointCandidate->Next = MergePoint;

        // --- 6) placeholder 들 comment-out 처리 ---
        for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
        {
            MergePoint->RecordCommonPlaceholder(Placeholder);
            Placeholder->bIsCommentOut = true;
            Placeholder->bIsFallthrough = true;
        }

        return MergePoint;
    }

    // 실행 트리에서 common placeholder 수집
    void CollectCommonPlaceholders(
        const TSharedPtr<N2CFlow::Step>& Step,
        const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey,
        TMap<FString, TArray<TSharedPtr<N2CFlow::Step>>>& PlaceholdersByCommonKey)
    {
        // placeholders_by_commonkey: Common step key별 placeholder list를 담는 통
        if (!Step.IsValid())
        {
            return;
        }

        // placeholder 발견
        if (Step->bIsCommonPlaceholder)
        {
            if (Step->Node.IsValid())
            {
                TSharedPtr<N2CFlow::Step> CommonStep = FindStepByKey(StepsByKey, Step->Node->Name);
                if (CommonStep.IsValid())
                {
                    PlaceholdersByCommonKey.FindOrAdd(CommonStep->Key).Add(Step);
                }
            }
        }

        // branches
        for (const TSharedPtr<N2CFlow::Step>& Branch : Step->Branches)
        {
            CollectCommonPlaceholders(Branch, StepsByKey, PlaceholdersByCommonKey);
        }

        // next
        if (Step->Next.IsValid())
        {
            CollectCommonPlaceholders(Step->Next, StepsByKey, PlaceholdersByCommonKey);
        }
    }
}

bool FN2CFlowBuilder::BuildFlowDataFromGraph(UEdGraph* Graph, FN2CFlowData& OutData, FString& OutError)
{
    if (!Graph)
    {
        OutError = TEXT("Invalid graph");
        return false;
    }

    // 그래프의 K2 노드를 수집
    TArray<UK2Node*> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node* K2Node = Cast<UK2Node>(Node))
        {
            Nodes.Add(K2Node);
        }
    }

    return BuildFlowDataFromNodes(Nodes, OutData, OutError);
}

bool FN2CFlowBuilder::BuildFlowDataFromNodes(const TArray<UK2Node*>& Nodes, FN2CFlowData& OutData, FString& OutError)
{
    OutData = FN2CFlowData();

    // 1) Node/Pin/Link 생성
    if (!BuildNodesFromK2Nodes(Nodes, OutData.NodesByName))
    {
        OutError = TEXT("Failed to build nodes");
        return false;
    }

    // 2) Step 생성 및 entry 찾기
    BuildStepsFromNodes(OutData.NodesByName, OutData.StepsByKey);
    OutData.EntryStep = FindEntryStep(OutData.StepsByKey);
    if (!OutData.EntryStep.IsValid())
    {
        OutError = TEXT("No entry node found");
        return false;
    }

    OutData.EntryStep->MakeAsEntry(0);
    // 3) 실행 흐름 구성 (DFS)
    if (!BuildExecFlow(OutData.NodesByName, OutData.StepsByKey, OutData.CommonSteps, OutData.EntryStep, OutError))
    {
        return false;
    }

    // 4) 머지 포인트 처리
    ResolveMergingPoints(OutData.EntryStep, OutData.StepsByKey, false);
    for (const TPair<FString, TSharedPtr<N2CFlow::Step>>& Pair : OutData.CommonSteps)
    {
        ResolveMergingPoints(Pair.Value, OutData.StepsByKey, false);
    }

    return true;
}

bool FN2CFlowBuilder::BuildFlowJsonFromGraph(UEdGraph* Graph, FString& OutJson, FString& OutError)
{
    FN2CFlowData Data;
    if (!BuildFlowDataFromGraph(Graph, Data, OutError))
    {
        return false;
    }

    // flow 데이터 -> JSON 직렬화
    TSharedPtr<FJsonObject> RootObject = FlowDataToJsonObject(Data);
    if (!RootObject.IsValid())
    {
        OutError = TEXT("Failed to serialize flow data");
        return false;
    }

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
    return true;
}

bool FN2CFlowBuilder::BuildFlowJsonFromNodes(const TArray<UK2Node*>& Nodes, FString& OutJson, FString& OutError)
{
    FN2CFlowData Data;
    if (!BuildFlowDataFromNodes(Nodes, Data, OutError))
    {
        return false;
    }

    // flow 데이터 -> JSON 직렬화
    TSharedPtr<FJsonObject> RootObject = FlowDataToJsonObject(Data);
    if (!RootObject.IsValid())
    {
        OutError = TEXT("Failed to serialize flow data");
        return false;
    }

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
    return true;
}

bool FN2CFlowBuilder::BuildNodesFromK2Nodes(const TArray<UK2Node*>& Nodes, TMap<FString, TSharedPtr<N2CFlow::Node>>& OutNodesByName)
{
    OutNodesByName.Reset();
    // 1) 먼저 모든 노드/핀 정보를 수집하고
    // 2) 그 다음 링크를 구성한다.
    for (UK2Node* K2Node : Nodes)
    {
        if (!K2Node)
        {
            continue;
        }

        const FString NodeName = K2Node->GetName();
        const FString NodeGuid = GuidToString(K2Node->NodeGuid);

        // 노드 생성
        TSharedPtr<N2CFlow::Node> FlowNode = MakeShared<N2CFlow::Node>(NodeName, NodeGuid);

        // 핀 정보 순회
        for (UEdGraphPin* Pin : K2Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            const FString PinName = PinDisplayName(Pin);
            const FString PinGuid = GuidToString(Pin->PinId);
            const bool bIsExec = (Pin->PinType.PinCategory == TEXT("exec"));

            // 로컬 핀 복구
            N2CFlow::Pin LocalPin(PinName, PinGuid, NodeName, NodeGuid);

            // 로컬 핀 정보 캐시(링크 여부 상관 없음)
            if (bIsExec)
            {
                if (Pin->Direction == EGPD_Output)
                {
                    FlowNode->ExecOutPins.Add(LocalPin);
                }
                else
                {
                    FlowNode->ExecInPins.Add(LocalPin);
                }
            }
            else
            {
                if (Pin->Direction == EGPD_Output)
                {
                    FlowNode->DataOutPins.Add(LocalPin);
                }
                else
                {
                    FlowNode->DataInPins.Add(LocalPin);
                }
            }

            // 핀의 링크 정보 순회
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin || !LinkedPin->GetOwningNode())
                {
                    continue;
                }

                UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                FString LinkedNodeName = LinkedNode->GetName();
                FString LinkedNodeGuid = GuidToString(LinkedNode->NodeGuid);
                FString LinkedPinName = PinDisplayName(LinkedPin);
                FString LinkedPinGuid = GuidToString(LinkedPin->PinId);

                // 리모트 핀 정보 복구
                N2CFlow::Pin RemotePin(LinkedPinName, LinkedPinGuid, LinkedNodeName, LinkedNodeGuid);

                // 방향에 따라 Link 생성
                if (bIsExec)
                {
                    if (Pin->Direction == EGPD_Output)
                    {
                        FlowNode->ExecOutLinks.Add(N2CFlow::Link(LocalPin, RemotePin));
                    }
                    else
                    {
                        FlowNode->ExecInLinks.Add(N2CFlow::Link(RemotePin, LocalPin));
                    }
                }
                else
                {
                    if (Pin->Direction == EGPD_Output)
                    {
                        FlowNode->DataOutLinks.Add(N2CFlow::Link(LocalPin, RemotePin));
                    }
                    else
                    {
                        FlowNode->DataInLinks.Add(N2CFlow::Link(RemotePin, LocalPin));
                    }
                }
            }
        }

        OutNodesByName.Add(NodeName, FlowNode);
    }

    return OutNodesByName.Num() > 0;
}

void FN2CFlowBuilder::BuildStepsFromNodes(const TMap<FString, TSharedPtr<N2CFlow::Node>>& NodesByName,
                                         TMap<FString, TSharedPtr<N2CFlow::Step>>& OutStepsByKey)
{
    OutStepsByKey.Reset();
    // 모든 노드에 대한 step을 미리 생성하여 name을 키로 매핑
    for (const TPair<FString, TSharedPtr<N2CFlow::Node>>& Pair : NodesByName)
    {
        TSharedPtr<N2CFlow::Step> Step = MakeShared<N2CFlow::Step>();
        Step->Node = Pair.Value;
        Step->Key = Pair.Key;
        OutStepsByKey.Add(Pair.Key, Step);
    }
}

TSharedPtr<N2CFlow::Step> FN2CFlowBuilder::FindEntryStep(const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey)
{
    // FunctionEntry 노드를 entry로 사용
    for (const TPair<FString, TSharedPtr<N2CFlow::Step>>& Pair : StepsByKey)
    {
        if (Pair.Key.Contains(TEXT("K2Node_FunctionEntry")))
        {
            return Pair.Value;
        }
    }
    return nullptr;
}

bool FN2CFlowBuilder::BuildExecFlow(
    const TMap<FString, TSharedPtr<N2CFlow::Node>>& NodesByName,
    TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey,
    TMap<FString, TSharedPtr<N2CFlow::Step>>& CommonSteps,
    const TSharedPtr<N2CFlow::Step>& EntryStep,
    FString& OutError)
{
    // DFS 상태를 유지하는 스택
    TArray<TSharedPtr<N2CFlow::Step>> Stack;
    Stack.Add(EntryStep);
    int32 LogicDepth = EntryStep->LogicDepth;

    while (Stack.Num() > 0)
    {
        TSharedPtr<N2CFlow::Step> CurrentStep = Stack.Last();
        // current step의 outlink idx를 1 증가
        CurrentStep->NextOutlinkIdx();

        // current step의 outlink를 모두 처리했다면 스택에서 제거
        if (CurrentStep->HasAllOutlinkProcessed())
        {
            Stack.Pop();
            LogicDepth = CurrentStep->PopLogicDepth(LogicDepth);
            continue;
        }

        // 처리해야 할 outlink가 남아 있다면,
        const N2CFlow::Link* Link = CurrentStep->GetOutlink();
        if (!Link)
        {
            OutError = TEXT("Broken link: missing outlink");
            return false;
        }

        const FString NextNodeName = Link->ToPin.NodeName;
        TSharedPtr<N2CFlow::Step> NextStep = FindStepByKey(StepsByKey, NextNodeName);
        // 다음 Step이 유효하지 않을 때, 예외 처리.
        if (!NextStep.IsValid())
        {
            OutError = FString::Printf(TEXT("Broken link: %s -> %s"), *CurrentStep->Node->Name, *NextNodeName);
            return false;
        }

        // next step 업데이트
        NextStep->AppendFromPin(*Link);

        // 다음이 머지되는 스텝이라면, Common Logic 표시용 placeholder로 대체
        if (NextStep->IsCommonStep())
        {
            // 이미 common steps로 등록된 경우는 재사용
            if (!CommonSteps.Contains(NextNodeName))
            {
                // 아직 common flow로 등록되지 않은 경우
                CommonSteps.Add(NextNodeName, NextStep);
                NextStep->MakeAsEntry(0);
                if (!BuildExecFlow(NodesByName, StepsByKey, CommonSteps, NextStep, OutError))
                {
                    return false;
                }
            }

            // Common Logic 표시용 placeholder 생성
            TSharedPtr<N2CFlow::Step> CommonPlaceholder = MakeShared<N2CFlow::Step>();
            CommonPlaceholder->Key = FString::Printf(TEXT("%s_PlaceHolder_%d"), *NextNodeName, NextStep->FromPins.Num() - 1);
            CommonPlaceholder->Node = NextStep->Node;
            CommonPlaceholder->AppendFromPin(*Link);
            CommonPlaceholder->bIsCommonPlaceholder = true;
            CommonPlaceholder->bIsBranched = CurrentStep->HasBranches();
            CommonPlaceholder->LogicDepth = LogicDepth;

            StepsByKey.Add(CommonPlaceholder->Key, CommonPlaceholder);
            NextStep->RecordCommonPlaceholder(CommonPlaceholder);

            // 실행 흐름 연결
            if (CommonPlaceholder->bIsBranched)
            {
                CurrentStep->AppendBranch(CommonPlaceholder);
            }
            else
            {
                CurrentStep->SetNext(CommonPlaceholder);
            }
        }
        else
        {
            // 다음이 머지되는 스텝이 아니라면,
            NextStep->LogicDepth = LogicDepth;
            NextStep->bIsBranched = CurrentStep->HasBranches();

            // 실행 흐름 연결
            if (NextStep->bIsBranched)
            {
                CurrentStep->AppendBranch(NextStep);
            }
            else
            {
                CurrentStep->SetNext(NextStep);
            }

            // 자식 노드를 스택에 push
            Stack.Add(NextStep);
            LogicDepth = NextStep->PushLogicDepth(LogicDepth);
        }
    }

    return true;
}

void FN2CFlowBuilder::ResolveMergingPoints(const TSharedPtr<N2CFlow::Step>& EntryStep,
                                          TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey,
                                          bool bDebug)
{
    // { commonstep_key : [common_placeholder_step, ...] } 형태
    TMap<FString, TArray<TSharedPtr<N2CFlow::Step>>> PlaceholdersByCommonKey;
    CollectCommonPlaceholders(EntryStep, StepsByKey, PlaceholdersByCommonKey);

    for (const TPair<FString, TArray<TSharedPtr<N2CFlow::Step>>>& Pair : PlaceholdersByCommonKey)
    {
        const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders = Pair.Value;
        // placeholder들 중에 서로 묶일 수 있을 만한 그루핑
        TArray<FMergingGroup> Groups = BuildPlaceholderGroups(Placeholders, StepsByKey);

        // todo. 부모가 스위치이면 중단해야 할지 고려 필요
        // (예외적으로 switch는 fallthrough 되어야 함)
        // 그루핑 별로 머지 포인트 찾기/생성
        for (const FMergingGroup& Group : Groups)
        {
            // todo. 모든 placeholder 경로가 같고, default도 같은 경로인지 추가 조건 고려 필요
            if (Group.bIsFallthrough)
            {
                CreateFallthroughMergePoint(StepsByKey, Group.MergingPointStep, Group.Placeholders, Group.CommonStep);
            }
            else
            {
                CreateNormalMergePoint(StepsByKey, Group.MergingPointStep, Group.Placeholders, Group.CommonStep);
            }
        }
    }
}

TSharedPtr<FJsonObject> FN2CFlowBuilder::FlowDataToJsonObject(const FN2CFlowData& Data)
{
    if (!Data.EntryStep.IsValid())
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    // entry step key 저장
    RootObject->SetStringField(TEXT("entry_step_key"), Data.EntryStep->Key);

    // common step 키 목록
    TArray<TSharedPtr<FJsonValue>> CommonKeys;
    for (const TPair<FString, TSharedPtr<N2CFlow::Step>>& Pair : Data.CommonSteps)
    {
        CommonKeys.Add(MakeShared<FJsonValueString>(Pair.Key));
    }
    RootObject->SetArrayField(TEXT("common_step_keys"), CommonKeys);

    // 모든 step을 JSON으로 저장
    TSharedPtr<FJsonObject> StepsObject = MakeShared<FJsonObject>();
    for (const TPair<FString, TSharedPtr<N2CFlow::Step>>& Pair : Data.StepsByKey)
    {
        if (Pair.Value.IsValid())
        {
            StepsObject->SetObjectField(Pair.Key, Pair.Value->ToJsonObject());
        }
    }
    RootObject->SetObjectField(TEXT("steps"), StepsObject);

    // 모든 node를 JSON으로 저장
    TSharedPtr<FJsonObject> NodesObject = MakeShared<FJsonObject>();
    for (const TPair<FString, TSharedPtr<N2CFlow::Node>>& Pair : Data.NodesByName)
    {
        if (Pair.Value.IsValid())
        {
            NodesObject->SetObjectField(Pair.Key, Pair.Value->ToJsonObject());
        }
    }
    RootObject->SetObjectField(TEXT("nodes"), NodesObject);

    return RootObject;
}

bool FN2CFlowBuilder::BuildFlowTextFromGraph(UEdGraph* Graph, FString& OutText, FString& OutError)
{
    FN2CFlowData Data;
    if (!BuildFlowDataFromGraph(Graph, Data, OutError))
    {
        return false;
    }

    TArray<FString> Lines = FlowDataToTextLines(Data);
    OutText = FString::Join(Lines, TEXT("\n"));
    return true;
}

bool FN2CFlowBuilder::BuildFlowTextFromNodes(const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError)
{
    FN2CFlowData Data;
    if (!BuildFlowDataFromNodes(Nodes, Data, OutError))
    {
        return false;
    }

    TArray<FString> Lines = FlowDataToTextLines(Data);
    OutText = FString::Join(Lines, TEXT("\n"));
    return true;
}

TArray<FString> FN2CFlowBuilder::FlowDataToTextLines(const FN2CFlowData& Data)
{
    TArray<FString> Lines;
    if (!Data.EntryStep.IsValid())
    {
        return Lines;
    }

    // 결과 문자열 리스트 생성
    Lines.Add(TEXT("=========== Steps ==========="));
    Lines.Append(PrintSteps(Data.EntryStep));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("=========== Common Steps ==========="));
    Lines.Append(PrintCommonSteps(Data.CommonSteps));
    return Lines;
}
#pragma endregion