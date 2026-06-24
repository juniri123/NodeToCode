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
#include "Utils/N2CLogger.h"

#pragma region ODS
namespace
{
    // placeholder 그룹 1개를 표현하는 데이터 구조
    // merge point 생성에 필요한 모든 정보 포함
    struct FMergingGroup
    {
        SharedStepPtr CommonStep;
        SharedStepPtr MergingPointStep;
        StepArray Placeholders;
        bool bIsFallthrough = false;
    };

    // 같은 분기 트리를 공유하는 callstack들을 
    // 가장 깊은 callstack을 leader로 지정하고 
    // 그룹으로 묶는다
    struct FLeaderCallstackGroup
    {
        FString Callstack;
        SharedStepPtr Leader;
        StepArray Steps;
    };

    // 같은 callstack을 가진 step들을 하나의 그룹으로 묶는다.
    struct FCallstackGroup
    {
        FString Callstack;
        StepArray Steps;

        SharedStepPtr GetFirstStep() const
        {
            return Steps.Num() > 0 ? Steps[0] : nullptr;
        }
    };

    // Step과 그 Step의 callstack 문자열
    struct FStepCallstack
    {
        SharedStepPtr Step;
        FString Callstack;
    };

    // 로직 depth만큼 indent prefix 생성
    FString MakeIndentPrefix(int32 Depth, bool bNoIndent)
    {
        if (bNoIndent)
        {
            return TEXT("");
        }

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

    // Flow 출력용 핀 이름
    // exec 핀인데 표시 이름이 비어 있으면 빈 라벨 대신 exec로 보여준다.
    FString FlowPinDisplayName(const N2CFlow::Pin& Pin)
    {
        if (!Pin.Name.IsEmpty())
        {
            return Pin.Name;
        }

        return Pin.bIsExec ? TEXT("exec") : TEXT("");
    }

    FString SimpleNodeGuid(const FString& Guid, const N2CFlow::FGUIDAlias& GuidAlias)
    {
        return GuidAlias.ResolveNodeID(Guid);
    }

    FString SimplePinGuid(const FString& Guid, const N2CFlow::FGUIDAlias& GuidAlias)
    {
        return GuidAlias.ResolvePinID(Guid);
    }

    // Step 키로 찾기
    SharedStepPtr FindStepByKey(const StepMap& StepsByKey, const FString& Key)
    {
        const SharedStepPtr* Found = StepsByKey.Find(Key);
        return Found ? *Found : nullptr;
    }

    // 단일 Step을 텍스트 라인으로 변환
    TArray<FString> PrintSingleStep(const SharedStepPtr& Step, bool bNoIndent, const N2CFlow::FGUIDAlias& GuidAlias)
    {
        TArray<FString> Lines;
        if (!Step.IsValid() || !Step->Node.IsValid())
        {
            return Lines;
        }

        const FString IndentPrefix = MakeIndentPrefix(Step->LogicDepth, bNoIndent);

        FString BranchLabel;
        if (Step->FromPins.Num() > 0)
        {
            const FString PrefixIcon = Step->bIsBranched ? TEXT("➡️ ") : TEXT("");
            TArray<FString> Parts;
            for (const N2CFlow::Pin& Pin : Step->FromPins)
            {
                const FString PinName = FlowPinDisplayName(Pin);
                const FString PinGuid = SimplePinGuid(Pin.Guid, GuidAlias);
                const FString NodeGuid = SimpleNodeGuid(Pin.NodeGuid, GuidAlias);
                Parts.Add(FString::Printf(TEXT("%s📌%s::%s from (📋%s::%s)"),
                                        *PrefixIcon,
                                        *PinName,
                                        *PinGuid,
                                        *Pin.NodeName,
                                        *NodeGuid));
            }
            BranchLabel = FString::Join(Parts, TEXT(", "));
        }

        const FString CommentOut = Step->bIsCommentOut ? TEXT("//") : TEXT("");
        const FString StepNodeGuid = SimpleNodeGuid(Step->Node->Guid, GuidAlias);

        // Common Logic Placeholder 처리
        if (Step->bIsCommonPlaceholder)
        {
            if (Step->bIsBranched)
            {
                Lines.Add(IndentPrefix + BranchLabel);
                const FString CommonIndent = MakeIndentPrefix(Step->LogicDepth + 1, false);
                Lines.Add(FString::Printf(TEXT("%s%s↪️ Placeholder::%s for 📋%s::%s"),
                                        *CommonIndent,
                                        *CommentOut,
                                        *Step->Key,
                                        *Step->Node->Name,
                                        *StepNodeGuid));
            }
            else
            {
                Lines.Add(FString::Printf(TEXT("%s%s%s → ↪️ Placeholder::%s for 📋%s::%s"),
                                        *IndentPrefix,
                                        *CommentOut,
                                        *BranchLabel,
                                        *Step->Key,
                                        *Step->Node->Name,
                                        *StepNodeGuid));
            }
            return Lines;
        }

        // common step 출력
        if (Step->IsCommonStep())
        {
            Lines.Add(IndentPrefix + TEXT("----- From Pins -----"));
            for (const N2CFlow::Pin& Pin : Step->FromPins)
            {
                const FString PinName = FlowPinDisplayName(Pin);
                const FString PinGuid = SimplePinGuid(Pin.Guid, GuidAlias);
                const FString NodeGuid = SimpleNodeGuid(Pin.NodeGuid, GuidAlias);
                Lines.Add(FString::Printf(TEXT("%s📌%s::%s from (📋%s::%s)"),
                                        *IndentPrefix,
                                        *PinName,
                                        *PinGuid,
                                        *Pin.NodeName,
                                        *NodeGuid));
            }
            Lines.Add(IndentPrefix + TEXT("----- Placeholders -----"));
            for (const SharedStepPtr& Placeholder : Step->CommonPlaceholders)
            {
                if (Placeholder.IsValid())
                {
                    Lines.Add(IndentPrefix + Placeholder->Key);
                }
            }
            Lines.Add(IndentPrefix + TEXT("---------------------"));
            Lines.Add(FString::Printf(TEXT("%s📋%s::%s"), *IndentPrefix, *Step->Node->Name, *StepNodeGuid));
            return Lines;
        }

        // merging point 출력
        if (Step->bIsMergingPoint)
        {
            Lines.Add(IndentPrefix + TEXT("Merging Point ") + Step->Key);
            Lines.Add(IndentPrefix + TEXT("----- Merged Placeholders -----"));
            for (const SharedStepPtr& Placeholder : Step->CommonPlaceholders)
            {
                if (Placeholder.IsValid() && Placeholder->Node.IsValid())
                {
                    const FString PlaceholderNodeGuid = SimpleNodeGuid(Placeholder->Node->Guid, GuidAlias);
                    Lines.Add(FString::Printf(TEXT("%s%s (📋%s::%s)"),
                                            *IndentPrefix,
                                            *Placeholder->Key,
                                            *Placeholder->Node->Name,
                                            *PlaceholderNodeGuid));
                }
            }
            Lines.Add(IndentPrefix + TEXT("---------------------"));
            return Lines;
        }

        // branched step 출력
        if (Step->bIsBranched)
        {
            Lines.Add(IndentPrefix + BranchLabel);
            const FString BranchedIndent = MakeIndentPrefix(Step->LogicDepth + 1, false);
            Lines.Add(FString::Printf(TEXT("%s%s📋%s::%s"),
                                    *BranchedIndent,
                                    *CommentOut,
                                    *Step->Node->Name,
                                    *StepNodeGuid));
            return Lines;
        }

        // 일반적인 경우
        Lines.Add(FString::Printf(TEXT("%s%s%s → 📋%s::%s"),
                                *CommentOut,
                                *IndentPrefix,
                                *BranchLabel,
                                *Step->Node->Name,
                                *StepNodeGuid));
        return Lines;
    }

    // 출력 순회 중인 Step 1개를 명시적인 스택 프레임으로 표현한다.
    // Step의 branch들을 어디까지 출력했는지, Next를 이미 출력했는지를 저장해서
    // "현재 Step -> 모든 Branch -> Next" 순서를 비재귀로 유지한다.
    struct FPrintFrame
    {
        SharedStepPtr Step;
        int32 NextBranchIdx = 0;
        bool bNextProcessed = false;

        explicit FPrintFrame(const SharedStepPtr& InStep)
            : Step(InStep)
        {
        }

        SharedStepPtr NextBranch()
        {
            if (NextBranchIdx < Step->Branches.Num())
            {
                const SharedStepPtr Branch = Step->Branches[NextBranchIdx++];
                if (!Branch.IsValid())
                {
                    FN2CLogger::Get().LogError(TEXT("Invalid branch in print traversal"));
                }
                return Branch;
            }
            return nullptr;
        }

        SharedStepPtr Next()
        {
            if (bNextProcessed)
            {
                return nullptr;
            }

            bNextProcessed = true;
            return Step->Next;
        }
    };

    // 출력한 Step들을 순서대로 모은다.
    // 순서는 "현재 Step -> Branches 순회 -> Next"
    StepArray CollectPrintOrder(const SharedStepPtr& RootStep)
    {
        StepArray OutOrderedSteps;
        if (!RootStep.IsValid())
        {
            return OutOrderedSteps;
        }

        TArray<FPrintFrame> TraverseStack;
        // RootStep 적재
        TraverseStack.Emplace(RootStep);
        OutOrderedSteps.Add(RootStep);

        while (TraverseStack.Num() > 0)
        {
            FPrintFrame& Frame = TraverseStack.Last();

            // Branch가 남아 있으면 먼저 들어간다.
            // 새 frame을 push해 두면 해당 branch의 하위 branch/next를 모두 처리한 뒤 여기로 돌아온다.
            const SharedStepPtr Branch = Frame.NextBranch();
            if (Branch.IsValid())
            {
                TraverseStack.Emplace(Branch);
                OutOrderedSteps.Add(Branch);
                continue;
            }

            // 모든 branch를 처리한 뒤에야 Next로 진행한다.
            const SharedStepPtr Next = Frame.Next();
            if (Next.IsValid())
            {
                TraverseStack.Emplace(Next);
                OutOrderedSteps.Add(Next);
                continue;
            }

            // 더 처리할 branch/next가 없으면 이 Step의 순회를 끝낸다.
            TraverseStack.Pop();
        }

        return OutOrderedSteps;
    }

    // 실행 흐름을 문자열 리스트로 출력
    TArray<FString> PrintSteps(const SharedStepPtr& Step, const N2CFlow::FGUIDAlias& GuidAlias)
    {
        TArray<FString> Lines;
        const StepArray OrderedSteps = CollectPrintOrder(Step);
        for (const SharedStepPtr& Current : OrderedSteps)
        {
            Lines.Append(PrintSingleStep(Current, false, GuidAlias));
        }
        return Lines;
    }
    
    // common_steps에 저장된 모든 step을 순회하며 출력
    TArray<FString> PrintCommonSteps(const StepMap& CommonSteps, const N2CFlow::FGUIDAlias& GuidAlias)
    {
        TArray<FString> Lines;
        if (CommonSteps.Num() == 0)
        {
            Lines.Add(TEXT("[INFO] No common step to display."));
            return Lines;
        }

        int32 Num = 0;
        for (const TPair<FString, SharedStepPtr>& Pair : CommonSteps)
        {
            Lines.Add(FString::Printf(TEXT("[#%d]"), Num));
            Lines.Append(PrintSteps(Pair.Value, GuidAlias));
            Lines.Add(TEXT(""));
            ++Num;
        }
        return Lines;
    }

    // 주어진 Step에서 root 까지 가는 체인을 수집
    // 실행 입력 핀이 여러 개인 경우(분기된 경우)에는 첫 번째 from 핀만 따라간다.
    // Common Step의 경우에 placeholder를 만들어 연결하였기 때문에, Common Step이 이 함수로 들어오지 않는다는 가정이다.
    // 따라서 여기에 들어오는 Step은 일반 Step이고 연결된 from 핀이 하나만 있다고 가정한다.
    StepArray GetPrimaryExecChain(const SharedStepPtr& Step, const StepMap& StepsByKey, bool bIncludeSelf)
    {
        StepArray Chain;

        SharedStepPtr Cur = Step;
        while (Cur.IsValid())
        {
            if (bIncludeSelf || Cur != Step)
            {
                Chain.Add(Cur);
            }

            if (Cur->FromPins.Num() == 0)
            {
                break;
            }

            const N2CFlow::Pin& ExecFromPin = Cur->FromPins[0];
            Cur = FindStepByKey(StepsByKey, ExecFromPin.NodeName);
        }

        return Chain;
    }

    // Step->Node->Name들을 /로 연결한 문자열 생성 (callstack 표현)
    FString BuildCallstackString(const StepArray& Chain)
    {
        TArray<FString> Names;
        for (const SharedStepPtr& Step : Chain)
        {
            if (Step.IsValid() && Step->Node.IsValid())
            {
                Names.Add(Step->Node->Name);
            }
        }

        return FString::Join(Names, TEXT("/"));
    }

    // placeholder들의 LCA(최저 공통 조상) 찾기
    SharedStepPtr FindLCA(const StepArray& Placeholders, const StepMap& StepsByKey)
    {
        // 각 placeholder의 parent chain 가져오기 (child -> root)
        if (Placeholders.Num() == 0)
        {
            return nullptr;
        }

        // 각 체인을 root -> child 로 뒤집기
        TArray<StepArray> Chains;
        Chains.Reserve(Placeholders.Num());
        for (const SharedStepPtr& Placeholder : Placeholders)
        {
            StepArray Chain =  GetPrimaryExecChain(Placeholder, StepsByKey, true);
            Algo::Reverse(Chain);
            Chains.Add(MoveTemp(Chain));
        }

        // 가장 짧은 체인 길이
        int32 MinLen = MAX_int32;
        for (const StepArray& Chain : Chains)
        {
            MinLen = FMath::Min(MinLen, Chain.Num());
        }

        SharedStepPtr Lca;
        for (int32 i = 0; i < MinLen; ++i)
        {
            const SharedStepPtr& Candidate = Chains[0][i];
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

    // 모든 switch에 적용되는 일반 규칙이 아니라,
    // 여러 case placeholder가 같은 callstack을 공유하는 특정 fallthrough-like 패턴만 잡는 예외 처리.
    bool IsSwitchFallthroughCase(const SharedStepPtr& Lca,
                                const StepArray& Placeholders,
                                const StepMap& StepsByKey)
    {
        // 좁게 잡는 조건:
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
        for (const SharedStepPtr& Placeholder : Placeholders)
        {
            StepArray Chain =  GetPrimaryExecChain(Placeholder, StepsByKey, false);
            Callstacks.Add(BuildCallstackString(Chain));
        }

        // 모두 동일한지 확인
        if (Callstacks.Num() != 1)
        {
            return false;
        }

        // --- 조건 3) default branch placeholder 배제 ---
        for (const SharedStepPtr& Placeholder : Placeholders)
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

    TArray<FStepCallstack> BuildCallstacksPerPlaceholder(
        const StepArray& AllPlaceholders,
        const StepMap& StepsByKey)
    {
        TArray<FStepCallstack> StepCallstacks;
        StepCallstacks.Reserve(AllPlaceholders.Num());

        for (const SharedStepPtr& OneStep : AllPlaceholders)
        {
            StepArray Chain = GetPrimaryExecChain(OneStep, StepsByKey, false);
            StepCallstacks.Emplace(OneStep, BuildCallstackString(Chain));
        }

        return StepCallstacks;
    }

    TArray<FCallstackGroup> BuildCallstackGroups(
        const TArray<FStepCallstack>& StepCallstacks
    )
    {
        // callstack 기준으로 그룹핑
        TMap<FString, StepArray> CallstackGroup;
        for (const FStepCallstack& OneStack : StepCallstacks)
        {
            CallstackGroup.FindOrAdd(OneStack.Callstack).Add(OneStack.Step);
        }

        // 그룹 -> 결과 변환
        TArray<FCallstackGroup> Groups;
        for (auto& Pair : CallstackGroup)
        {
            Groups.Emplace(Pair.Key, Pair.Value);
        }

        return Groups;
    }

    // 다른 콜스택에 포함되지 않으면 leader 그룹이며, 이를 감지하는 헬퍼 함수
    bool IsLeaderCallstackGroup(
        const TArray<FCallstackGroup>& CallstackGroups, int32 Idx
    )
    {
        if (!CallstackGroups.IsValidIndex(Idx))
        {
            return false;
        }

        const FString& CriteriaCallstack = CallstackGroups[Idx].Callstack;
        for (int32 i = 0; i < CallstackGroups.Num(); ++i)
        {
            if (i == Idx)
            {
                continue;
            }

            const FString& OtherStack = CallstackGroups[i].Callstack;
            if (OtherStack.Contains(CriteriaCallstack))
            {
                return false;
            }
        }

        return true;
    }

    TArray<FLeaderCallstackGroup> BuildLeaderCallstackGroups(
        const TArray<FCallstackGroup>& CallstackGroups
    )
    {
        // 리더 그룹 판별 및 생성
        TArray<FLeaderCallstackGroup> LeaderGroups;
        TArray<int32> ChildGroupIndices;
        for (int32 i = 0; i < CallstackGroups.Num(); ++i)
        {
            if (IsLeaderCallstackGroup(CallstackGroups, i))
            {
                const FCallstackGroup& Group = CallstackGroups[i];
                LeaderGroups.Emplace(Group.Callstack, Group.GetFirstStep(), Group.Steps);
            }
            else
            {
                ChildGroupIndices.Add(i);
            }
        }

        // 리더가 아닌 것들은 각자의 리더의 steps에 추가
        for (int32 i = 0; i < ChildGroupIndices.Num(); ++i)
        {
            const FCallstackGroup& ChildGroup = CallstackGroups[ChildGroupIndices[i]];
            for (FLeaderCallstackGroup& LeaderGroup : LeaderGroups)
            {
                if (LeaderGroup.Callstack.Contains(ChildGroup.Callstack))
                {
                    LeaderGroup.Steps.Append(ChildGroup.Steps);
                    break;
                }
            }
        }
        return LeaderGroups;
    }

    TArray<FMergingGroup> BuildMergingGroups(
        const SharedStepPtr& CommonStep,
        const TArray<FLeaderCallstackGroup>& LeaderCallstackGroups,
        const StepMap& StepsByKey)
    {
        TArray<FMergingGroup> Result;
        if (!CommonStep.IsValid())
        {
            return Result;
        }

        for (const FLeaderCallstackGroup& LeaderGroup : LeaderCallstackGroups)
        {
            const SharedStepPtr& Leader = LeaderGroup.Leader;
            const StepArray& Placeholders = LeaderGroup.Steps;

            if (!Leader.IsValid() || !Leader->Node.IsValid() || Placeholders.Num() == 0)
            {
                continue;
            }

            SharedStepPtr Lca = FindLCA(Placeholders, StepsByKey);

            FMergingGroup Group;
            Group.CommonStep = CommonStep;
            Group.Placeholders = Placeholders;

            // 특정 switch fallthrough-like 패턴이면 마지막 placeholder가 merge 후보
            if (IsSwitchFallthroughCase(Lca, Placeholders, StepsByKey))
            {
                Group.MergingPointStep = Placeholders.Last();
                Group.bIsFallthrough = true;
            }
            else
            {
                Group.MergingPointStep = Lca;
            }

            Result.Add(MoveTemp(Group));
        }

        return Result;
    }

    // 같은 commonstep을 가르키는 placeholder들을 callstack 기준으로 그루핑 (Python build_placeholder_groups)
    TArray<FMergingGroup> BuildPlaceholderGroups(
        const SharedStepPtr& CommonStep,
        const StepArray& AllPlaceholders,
        const StepMap& StepsByKey)
    {
        // callstack 수집
        TArray<FStepCallstack> StepCallstacks = BuildCallstacksPerPlaceholder(AllPlaceholders, StepsByKey);
        // 같은 callstack끼리 먼저 묶기
        TArray<FCallstackGroup> CallstackGroups = BuildCallstackGroups(StepCallstacks);
        // callstack이 달라도 같은 실행 경로끼리 묶기 (leader 그룹)
        // 여기서 리더는 그 중에서 가장 긴 실행경로를 가진 것이고
        // callstack이 완전히 같은 sibling이면 첫번 째 것이 리더가 된다.
        TArray<FLeaderCallstackGroup> LeaderCallstackGroups = BuildLeaderCallstackGroups(CallstackGroups);
        // 같은 실행 경로 그룹에서 머징 포인트를 찾는다.
        TArray<FMergingGroup> MergingGroups = BuildMergingGroups(CommonStep, LeaderCallstackGroups, StepsByKey);
        return MergingGroups;

#pragma region 기존 로직
#if 0
        // leader placeholder 선택
        TArray<TSharedPtr<N2CFlow::Step>> LeaderPlaceholders;

        // callstack 포함 관계를 기준으로, 다른 callstack에 포함되지 않는
        // 가장 깊은 placeholder들을 leader로 고른다.
        // 동일한 callstack을 가진 placeholder들은 대표 1개만 leader로 남긴다.
        for (const TSharedPtr<N2CFlow::Step>& Candidate : AllPlaceholders)
        {
            const FString& CandidateStack = StepCallstacks[Candidate->Key].Callstack;
            bool bIsLeader = true;

            // 다른 placeholder들과 비교
            for (const TSharedPtr<N2CFlow::Step>& Other : AllPlaceholders)
            {
                // candidate가 리더가 되려면 다른 placeholder의 콜스택에 포함되지 않아야 한다.

                // 자기 자신과 비교할 필요는 없음
                if (Other == Candidate)
                {
                    continue;
                }

                const FString& OtherStack = StepCallstacks[Other->Key].Callstack;
                // 더 깊은 다른 callstack이 CandidateStack을 포함하면 Candidate는 leader가 될 수 없다.
                // 단, 동일한 callstack sibling이면 그중 대표 1개는 leader로 남겨야 한다.
                if (OtherStack.Contains(CandidateStack))
                {
                    bIsLeader = false;
                    if (OtherStack == CandidateStack)
                    {
                        bIsLeader = !LeaderPlaceholders.Contains(Other) && !LeaderPlaceholders.Contains(Candidate);
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
            const FString& Callstack = StepCallstacks[Placeholder->Key].Callstack;
            for (const TSharedPtr<N2CFlow::Step>& Leader : LeaderPlaceholders)
            {
                const FString& LeaderStack = StepCallstacks[Leader->Key].Callstack;
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

            // 특정 switch fallthrough-like 패턴이면 마지막 placeholder가 merge 후보
            FMergingGroup Group;
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
#endif
#pragma endregion
    }

    // Create normal merge point and rewire next pointers
    // 일반 머지 포인트 생성 및 next 재배선
    SharedStepPtr CreateNormalMergePoint(
        StepMap& InOutStepsByKey,
        const SharedStepPtr& MergingPointCandidate,
        const StepArray& Placeholders,
        const SharedStepPtr& CommonStep)
    {
        if (!MergingPointCandidate.IsValid() || !CommonStep.IsValid())
        {
            return nullptr;
        }

        // 그루핑된 인덱스 검색
        TArray<int32> Indices;
        for (const SharedStepPtr& Placeholder : Placeholders)
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
        SharedStepPtr OldNext = MergingPointCandidate->Next;

        // --- 2) merge point 생성 ---
        SharedStepPtr MergePoint = MakeShared<N2CFlow::Step>();
        MergePoint->Key = FString::Printf(TEXT("%s_Merging_[%s]"), *CommonStep->Key, *GroupedIdxes);
        MergePoint->Node = CommonStep->Node;
        MergePoint->bIsMergingPoint = true;
        MergePoint->FromPins.Add(N2CFlow::Pin(TEXT(""), TEXT(""), MergingPointCandidate->Key, TEXT(""), true, true));

        // logic depth: old_next 있을 때만 복사
        MergePoint->LogicDepth = OldNext.IsValid() ? OldNext->LogicDepth : MergingPointCandidate->LogicDepth;

        // step registry 등록
        InOutStepsByKey.Add(MergePoint->Key, MergePoint);

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
        for (const SharedStepPtr& Placeholder : Placeholders)
        {
            MergePoint->RecordCommonPlaceholder(Placeholder);
            Placeholder->bIsCommentOut = true;
        }

        return MergePoint;
    }

    // Create merge point for a narrow switch fallthrough-like output pattern.
    // switch 전체에 대한 일반 처리라기보다, 여러 case가 같은 출력 경로를 공유하는 특정 패턴용 예외 처리.
    SharedStepPtr CreateFallthroughMergePoint(
        StepMap& InOutStepsByKey,
        const SharedStepPtr& MergingPointCandidate,
        const StepArray& Placeholders,
        const SharedStepPtr& CommonStep)
    {
        if (!MergingPointCandidate.IsValid() || !CommonStep.IsValid())
        {
            return nullptr;
        }

        // 그루핑된 인덱스 검색
        TArray<int32> Indices;
        for (const SharedStepPtr& Placeholder : Placeholders)
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
        SharedStepPtr MergePoint = MakeShared<N2CFlow::Step>();
        MergePoint->Key = FString::Printf(TEXT("%s_Merging_[%s]"), *CommonStep->Key, *GroupedIdxes);
        MergePoint->Node = CommonStep->Node;
        MergePoint->bIsMergingPoint = true;
        MergePoint->FromPins.Add(N2CFlow::Pin(TEXT(""), TEXT(""), MergingPointCandidate->Key, TEXT(""), true, true));

        // logic depth
        MergePoint->LogicDepth = MergingPointCandidate->LogicDepth + 1;

        // step registry 등록
        InOutStepsByKey.Add(MergePoint->Key, MergePoint);

        MergingPointCandidate->Next = MergePoint;

        // --- 6) placeholder 들 comment-out 처리 ---
        for (const SharedStepPtr& Placeholder : Placeholders)
        {
            MergePoint->RecordCommonPlaceholder(Placeholder);
            Placeholder->bIsCommentOut = true;
            Placeholder->bIsFallthrough = true;
        }

        return MergePoint;
    }

    // 실행 트리에서 common placeholder 수집
    void CollectCommonPlaceholders(
        const SharedStepPtr& step,
        const StepMap& StepsByKey,
        TMap<SharedStepPtr, StepArray>& OutPlaceholdersByCommonKey /* Common step key별 placeholder list를 담는 통 */ )
    {
        if (!step.IsValid())
        {
            return;
        }

        // placeholder 발견
        if (step->bIsCommonPlaceholder)
        {
            if (step->Node.IsValid())
            {
                SharedStepPtr CommonStep = FindStepByKey(StepsByKey, step->Node->Name);
                if (CommonStep.IsValid())
                {
                    OutPlaceholdersByCommonKey.FindOrAdd(CommonStep).Add(step);
                }
            }
        }

        // branches
        for (const SharedStepPtr& Branch : step->Branches)
        {
            CollectCommonPlaceholders(Branch, StepsByKey, OutPlaceholdersByCommonKey);
        }

        // next
        if (step->Next.IsValid())
        {
            CollectCommonPlaceholders(step->Next, StepsByKey, OutPlaceholdersByCommonKey);
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
    K2NodeArray Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node* K2Node = Cast<UK2Node>(Node))
        {
            Nodes.Add(K2Node);
        }
    }

    return BuildFlowDataFromNodes(Nodes, OutData, OutError);
}

bool FN2CFlowBuilder::BuildFlowDataFromNodes(const K2NodeArray& Nodes, FN2CFlowData& OutData, FString& OutError)
{
    OutData = FN2CFlowData();

    // 1) Node/Pin/Link 생성
    if (!BuildNodesFromK2Nodes(Nodes, OutData.NodesByName, OutData.GuidAlias))
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
    for (const TPair<FString, SharedStepPtr>& Pair : OutData.CommonSteps)
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

bool FN2CFlowBuilder::BuildFlowJsonFromNodes(const K2NodeArray& Nodes, FString& OutJson, FString& OutError)
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

// N2CFlow::Node 노드 정보를 UK2Node를 이용해서 생성하고 
// {Name - Node} 맵을 생성한다. 
bool FN2CFlowBuilder::BuildNodesFromK2Nodes(const K2NodeArray& Nodes,
                                            NodeMap& OutNodesByName, /*{노드 이름 :: 노드}*/
                                            N2CFlow::FGUIDAlias& OutGuidAlias /*노드, 핀의 GUID를 간단한 ID로 사용하기 위한 객체*/)
{
    OutNodesByName.Reset();
    // K2Node들을 순회
    for (UK2Node* K2Node : Nodes)
    {
        if (!K2Node)
        {
            continue;
        }

        const FString NodeName = K2Node->GetName();
        const FString NodeGuid = OutGuidAlias.AcquireNodeID(K2Node->NodeGuid); // GUID 말고 심플 ID로 변환

        // N2CFlow::Node 노드 생성
        TSharedPtr<N2CFlow::Node> FlowNode = MakeShared<N2CFlow::Node>(NodeName, NodeGuid);

        // N2CFlow::Node에 핀징보 생성
        for (UEdGraphPin* Pin : K2Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            const FString PinName = PinDisplayName(Pin);
            const FString PinGuid = OutGuidAlias.AcquirePinID(Pin->PinId); // GUID 말고 심플 ID로 변환
            const bool bIsExec = (Pin->PinType.PinCategory == TEXT("exec"));
            const bool bIsOutput = (Pin->Direction == EGPD_Output);

            // 로컬 핀 복구
            N2CFlow::Pin LocalPin(PinName, PinGuid, NodeName, NodeGuid, bIsExec, bIsOutput);

            // 로컬 핀 정보 캐시(링크 여부 상관 없음)
            if (bIsExec)
            {
                if (bIsOutput)
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
                const bool bLinkedIsExec = (LinkedPin->PinType.PinCategory == TEXT("exec"));
                const bool bLinkedIsOutput = (LinkedPin->Direction == EGPD_Output);

                // 리모트 핀 정보 복구
                N2CFlow::Pin RemotePin(LinkedPinName, LinkedPinGuid, LinkedNodeName, LinkedNodeGuid, bLinkedIsExec, bLinkedIsOutput);

                // 방향에 따라 Link 생성
                if (bIsExec)
                {
                    if (bIsOutput)
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
                    if (bIsOutput)
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

// N2CFlow::Node 노드에 대한 N2CFlow::Step을 생성하여 
// {Name - Step} 맵을 생성한다.
void FN2CFlowBuilder::BuildStepsFromNodes(const NodeMap& NodesByName,
                                         StepMap& OutStepsByKey)
{
    OutStepsByKey.Reset();
    for (const TPair<FString, TSharedPtr<N2CFlow::Node>>& Pair : NodesByName)
    {
        SharedStepPtr Step = MakeShared<N2CFlow::Step>();
        Step->Node = Pair.Value;
        Step->Key = Pair.Key;
        OutStepsByKey.Add(Pair.Key, Step);
    }
}

// 주어진 {Name - Step} 맵에서 Entry Step을 반환
SharedStepPtr FN2CFlowBuilder::FindEntryStep(const StepMap& StepsByKey)
{
    // FunctionEntry 노드를 entry로 사용
    for (const TPair<FString, SharedStepPtr>& Pair : StepsByKey)
    {
        if (Pair.Key.Contains(TEXT("K2Node_FunctionEntry")))
        {
            return Pair.Value;
        }
    }
    return nullptr;
}

// 실행 흐름을 생성한다.
bool FN2CFlowBuilder::BuildExecFlow(
    const NodeMap& NodesByName,
    StepMap& StepsByKey,
    StepMap& CommonSteps,
    const SharedStepPtr& EntryStep,
    FString& OutError)
{
    // DFS 상태를 유지하는 스택
    StepArray Stack;
    Stack.Add(EntryStep);
    int32 LogicDepth = EntryStep->LogicDepth;

    while (Stack.Num() > 0)
    {
        SharedStepPtr CurrentStep = Stack.Last();
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
        SharedStepPtr NextStep = FindStepByKey(StepsByKey, NextNodeName);
        // 다음 Step이 유효하지 않을 때, 예외 처리.
        if (!NextStep.IsValid())
        {
            OutError = FString::Printf(TEXT("Broken link: %s -> %s"), *CurrentStep->Node->Name, *NextNodeName);
            return false;
        }

        // next step 업데이트
        NextStep->AppendFromPin(*Link);

        // 여러 step의 out execute pin --> next step 인 상황,  
        // 즉, 여러 스텝에서 공통으로 연결된 스텝이라면
        // 혹은, 여러 스텝에서 머지되는 스텝이라면, 
        // next step으로 연결하지 않고, Common Logic 표시용 placeholder step을 만들어서 대체하고
        // next step은 common step으로 분류한다.
        // IsCommonStep를 살펴보면, Step이 캐시하고 있는 N2CFlow::Node의 ExecInLinks 갯수가 1초과인지를 살핀다.
        if (NextStep->IsCommonStep())
        {
            // 아직 CommonSteps에 등록되지 않은 경우,
            // CommonSteps에 등록하고, 
            // common step의 실행흐름을 구축한다.
            if (!CommonSteps.Contains(NextNodeName))
            {
                CommonSteps.Add(NextNodeName, NextStep);
                NextStep->MakeAsEntry(0);
                if (!BuildExecFlow(NodesByName, StepsByKey, CommonSteps, NextStep, OutError))
                {
                    return false;
                }
            }
            
            // NextStep에 대한 링크 대신에 
            // Common Step으로 이어진다는 표시용 placeholder 생성하여 연결한다.
            SharedStepPtr CommonPlaceholder = MakeShared<N2CFlow::Step>();
            CommonPlaceholder->Key = FString::Printf(TEXT("%s_PlaceHolder_%d"), *NextNodeName, NextStep->FromPins.Num() - 1);
            // 이 부분이 이상할 수 있으나, 원래는 common step이  연결되어야 할 것을 placeholder로 연결하고 있으나. 
            // common step의 숏컷 정도로 생각하자. 
            // todo. 시간이 되면 Node가 아니라 다른 변수에 넣는 것이 더 명확하겠다, 좀 더 나가면 step의 타입이 달라져야 하겠다
            CommonPlaceholder->Node = NextStep->Node;
            CommonPlaceholder->AppendFromPin(*Link);
            CommonPlaceholder->bIsCommonPlaceholder = true;
            CommonPlaceholder->bIsBranched = CurrentStep->HasBranches();
            CommonPlaceholder->LogicDepth = LogicDepth;
            // 생성된 placeholder 스텝을 저장
            StepsByKey.Add(CommonPlaceholder->Key, CommonPlaceholder);
            // common step에 생성된 placeholder 스텝을 기록(나중에 참조하기 편하기 위해서)
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
            
            // common step이기 때문에 스택에 넣고 처리 하지 않는다. 
            // 이 common step의 흐름은 BuildExecFlow 함수로 이미 생성하였음.
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

            // 스택에 push하여 다음 흐름을 찾을 수 있도록 한다.
            Stack.Add(NextStep);
            LogicDepth = NextStep->PushLogicDepth(LogicDepth);
        }
    }

    return true;
}

void FN2CFlowBuilder::ResolveMergingPoints(const SharedStepPtr& EntryStep,
                                          StepMap& StepsByKey,
                                          bool bDebug)
{
    // common step은 여러 곳에서 들어오는 작은 서브 로직
    // 따라서 하나의 common step에 연결된 placeholder stepd이 여러개이다
    // { commonstep_key : [common_placeholder_step, ...] } 형태
    // StepsByKey 에서 IsCommonStep이 true인 것들을 찾아 처리 할 수 있을 것 같지만, 
    // Entry에서 연결되지 않은 step오 isCommonStep이 true 일 수 있어서 
    // entry step에서 순회를 하면서 찾고 있다. 
    // todo. 다른 방법이라면 PlaceHolder용 step을 찾아서 그것만으로 매핑을 만드는 방법이 있다, 일단 진행하고 나중에 다시 보자
    TMap<SharedStepPtr, StepArray> PlaceholdersByCommonKey;
    CollectCommonPlaceholders(EntryStep, StepsByKey, PlaceholdersByCommonKey);

    for (const TPair<SharedStepPtr, StepArray>& Pair : PlaceholdersByCommonKey)
    {
        TArray<FMergingGroup> Groups = BuildPlaceholderGroups(Pair.Key, Pair.Value, StepsByKey);

        // todo. 부모가 switch 계열일 때 일반 merge를 막아야 하는지 검토 필요.
        // 단, 아래 fallthrough 처리는 모든 switch가 아니라 특정 출력 패턴에만 적용되는 예외다.
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
    for (const TPair<FString, SharedStepPtr>& Pair : Data.CommonSteps)
    {
        CommonKeys.Add(MakeShared<FJsonValueString>(Pair.Key));
    }
    RootObject->SetArrayField(TEXT("common_step_keys"), CommonKeys);

    // guid alias 매핑 저장
    RootObject->SetObjectField(TEXT("guid_alias"), Data.GuidAlias.ToJsonObject());

    // 모든 step을 JSON으로 저장
    TSharedPtr<FJsonObject> StepsObject = MakeShared<FJsonObject>();
    for (const TPair<FString, SharedStepPtr>& Pair : Data.StepsByKey)
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

bool FN2CFlowBuilder::BuildFlowTextFromNodes(const K2NodeArray& Nodes, FString& OutText, FString& OutError)
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
    Lines.Append(PrintSteps(Data.EntryStep, Data.GuidAlias));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("=========== Common Steps ==========="));
    Lines.Append(PrintCommonSteps(Data.CommonSteps, Data.GuidAlias));
    return Lines;
}
#pragma endregion
