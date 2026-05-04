#include "Core/N2CFlowBuilder_01.h"

#include "Models/Python/N2CFlowModel.h"
#include "Utils/N2CLogger.h"

#pragma region ODS
namespace
{
    // 로직 depth만큼 indent prefix 생성
    static FString MakeIndentPrefix_01(int32 Depth, bool bWithIndent)
    {
		if (bWithIndent)
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

    // 단일 Step을 텍스트 라인으로 변환
    // Step 그래프의 연결 정보는 이미 만들어져 있다고 보고,
    // 여기서는 Step의 상태 플래그에 따라 "어떻게 보일지"만 결정한다.
    TArray<FString> PrintSingleStep_01(const TSharedPtr<N2CFlow::Step>& Step, bool bWithIndent)
    {
        TArray<FString> Lines;

        // 출력할 실제 노드가 없으면 이 Step은 텍스트로 표현할 수 없다.
        if (!Step.IsValid() || !Step->Node.IsValid())
        {
            return Lines;
        }

        // 순회 단계에서 계산된 LogicDepth를 사용해 분기 깊이를 시각화한다.
        const FString IndentPrefix = bWithIndent ? MakeIndentPrefix_01(Step->LogicDepth, bWithIndent);

        // 이 Step으로 들어온 exec 핀들을 한 줄 라벨로 만든다.
        // 분기에서 들어온 Step이면 branch entry처럼 보이도록 화살표 아이콘을 붙인다.
        FString BranchLabel;
        if (Step->FromPins.Num() > 0)
        {
            const FString BranchIcon = Step->bIsBranched ? TEXT("➡️ ") : TEXT("");
            TArray<FString> Parts;
            for (const N2CFlow::Pin& Pin : Step->FromPins)
            {
                Parts.Add(FString::Printf(TEXT("%s📌%s::%s from (📋%s::%s)"),
                                        *BranchIcon,
                                        *Pin.Name,
                                        *Pin.Guid,
                                        *Pin.NodeName,
                                        *Pin.NodeGuid));
            }
            BranchLabel = FString::Join(Parts, TEXT(", "));
        }

        // 머지로 인해 실제 공통 노드를 다른 섹션에서 출력하는 경우,
        // 현재 위치의 placeholder는 주석 처리된 흐름처럼 표시한다.
        const FString CommentOut = Step->bIsCommentOut ? TEXT("//") : TEXT("");

        // Common Logic Placeholder 처리
        // 여러 흐름이 같은 노드로 합쳐질 때, 본문 흐름에는 실제 노드 대신 placeholder를 둔다.
        // 실제 공통 노드와 그 이후 흐름은 Common Steps 섹션에서 별도로 출력된다.
        if (Step->bIsCommonPlaceholder)
        {
            // 분기 엔트리 자체가 placeholder인 경우:
            // 1) 현재 depth에 어떤 branch 핀에서 왔는지 출력
            // 2) 한 depth 안쪽에 placeholder 대상 노드를 출력
            if (Step->bIsBranched)
            {
                Lines.Add(IndentPrefix + BranchLabel);
                const FString CommonIndent = MakeIndentPrefix_01(Step->LogicDepth + 1);
                Lines.Add(FString::Printf(TEXT("%s%s↪️ Placeholder::%s for 📋%s::%s"),
                                        *CommonIndent,
                                        *CommentOut,
                                        *Step->Key,
                                        *Step->Node->Name,
                                        *Step->Node->Guid));
            }
            else
            {
                // 일반 흐름 중간에서 공통 노드로 합쳐지는 경우는 한 줄로 표현한다.
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
        // 실제로 여러 exec 입력을 받는 공통 노드다.
        // 어떤 핀들이 합쳐졌는지, 본문에서 어떤 placeholder로 대체됐는지 먼저 보여주고,
        // 마지막에 실제 노드를 출력한다.
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
        // 본문 흐름에서 placeholder들이 합쳐지는 지점을 표시한다.
        // 여기서는 실제 노드를 출력하지 않고, 어떤 placeholder들이 merge됐는지만 보여준다.
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
        // 분기 핀에서 시작하는 실제 노드다.
        // branch 라벨은 현재 depth에 두고, 노드 본문은 한 depth 안쪽에 둔다.
        if (Step->bIsBranched)
        {
            Lines.Add(IndentPrefix + BranchLabel);
            const FString BranchedIndent = MakeIndentPrefix_01(Step->LogicDepth + 1);
            Lines.Add(FString::Printf(TEXT("%s%s📋%s::%s"),
                                    *BranchedIndent,
                                    *CommentOut,
                                    *Step->Node->Name,
                                    *Step->Node->Guid));
            return Lines;
        }

        // 일반적인 경우
        // 직렬 실행 흐름은 "입력 핀 라벨 -> 노드"를 한 줄로 출력한다.
        Lines.Add(FString::Printf(TEXT("%s%s%s → 📋%s::%s"),
                                *CommentOut,
                                *IndentPrefix,
                                *BranchLabel,
                                *Step->Node->Name,
                                *Step->Node->Guid));
        return Lines;
    }

    TArray<TSharedPtr<N2CFlow::Step>> CollectPrintOrder_01(const TSharedPtr<N2CFlow::Step>& RootStep)
    {
        TArray<TSharedPtr<N2CFlow::Step>> OrderedSteps;
        if (!RootStep.IsValid())
        {
            return OrderedSteps;
        }

        TArray<TSharedPtr<N2CFlow::Step>> Stack;
        Stack.Add(RootStep);

        while (Stack.Num() > 0)
        {
            TSharedPtr<N2CFlow::Step> Current = Stack.Pop();
            OrderedSteps.Add(Current);

            // Next는 Branches가 모두 처리된 후 출력되어야 하므로 먼저 push한다.
            if (Current->Next.IsValid())
            {
                Stack.Add(Current->Next);
            }

            // LIFO 스택이므로 Branches는 역순 push해야 branch[0]부터 출력된다.
            for (int32 BranchIdx = Current->Branches.Num() - 1; BranchIdx >= 0; --BranchIdx)
            {
                if (Current->Branches[BranchIdx].IsValid())
                {
                    Stack.Add(Current->Branches[BranchIdx]);
                }
            }
        }

        return OrderedSteps;
    }

    struct FPrintFrame
    {
        TSharedPtr<N2CFlow::Step> Step;
        int32 NextBranchIdx = 0;
        bool bNextProcessed = false;

        explicit FPrintFrame(const TSharedPtr<N2CFlow::Step>& InStep)
            : Step(InStep)
        {
        }

        TSharedPtr<N2CFlow::Step> NextBranch()
        {
            if (NextBranchIdx < Step->Branches.Num())
            {
                const TSharedPtr<N2CFlow::Step> Branch = Step->Branches[NextBranchIdx++];
                if (!Branch.IsValid())
                {
                    FN2CLogger::Get().LogError(TEXT("Invalid branch in print traversal"));
                }
                return Branch;
            }
            return nullptr;
        }

        TSharedPtr<N2CFlow::Step> Next()
        {
            if (bNextProcessed)
            {
                return nullptr;
            }

            bNextProcessed = true;
            return Step->Next;
        }
    };

    TArray<TSharedPtr<N2CFlow::Step>> CollectPrintOrder_02(const TSharedPtr<N2CFlow::Step>& RootStep)
    {
        TArray<TSharedPtr<N2CFlow::Step>> OutOrderedSteps;
        if (!RootStep.IsValid())
        {
            return OutOrderedSteps;
        }

        TArray<FPrintFrame> TraverseStack;
        TraverseStack.Emplace(RootStep);
		OutOrderedSteps.Add(RootStep);

        while (TraverseStack.Num() > 0)
        {
            FPrintFrame& Frame = TraverseStack.Last();

            const TSharedPtr<N2CFlow::Step> Branch = Frame.NextBranch();
            if (Branch.IsValid())
            {
                TraverseStack.Emplace(Branch);
                OutOrderedSteps.Add(Branch);
                continue;
            }

            const TSharedPtr<N2CFlow::Step> Next = Frame.Next();
            if (Next.IsValid())
            {
                TraverseStack.Emplace(Next);
                OutOrderedSteps.Add(Next);
                continue;
            }

            TraverseStack.Pop();
        }

        return OutOrderedSteps;
    }

    // 비재귀 버전
    TArray<FString> PrintSteps_01(const TSharedPtr<N2CFlow::Step>& RootStep)
    {
        TArray<FString> Lines;
        const TArray<TSharedPtr<N2CFlow::Step>> OrderedSteps = CollectPrintOrder_02(RootStep);
        for (const TSharedPtr<N2CFlow::Step>& Current : OrderedSteps)
        {
            Lines.Append(PrintSingleStep_01(Current, true));
        }
        return Lines;
    }

    // common_steps에 저장된 모든 step을 순회하며 출력
    TArray<FString> PrintCommonSteps_01(const TMap<FString, TSharedPtr<N2CFlow::Step>>& CommonSteps)
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
            Lines.Append(PrintSteps_01(Pair.Value));
            Lines.Add(TEXT(""));
            ++Num;
        }
        return Lines;
    }
}

bool FN2CFlowBuilder_01::BuildFlowTextFromNodes_01(const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError)
{
    FN2CFlowData Data;
    if (!FN2CFlowBuilder::BuildFlowDataFromNodes(Nodes, Data, OutError))
    {
        return false;
    }

    TArray<FString> Lines = FlowDataToTextLines_01(Data);
    OutText = FString::Join(Lines, TEXT("\n"));
    return true;
}

bool FN2CFlowBuilder_01::BuildFlowTextFromGraph_01(UEdGraph* Graph, FString& OutText, FString& OutError)
{
    FN2CFlowData Data;
    if (!FN2CFlowBuilder::BuildFlowDataFromGraph(Graph, Data, OutError))
    {
        return false;
    }

    TArray<FString> Lines = FlowDataToTextLines_01(Data);
    OutText = FString::Join(Lines, TEXT("\n"));
    return true;
}

TArray<FString> FN2CFlowBuilder_01::FlowDataToTextLines_01(const FN2CFlowData& Data)
{
    TArray<FString> Lines;
    if (!Data.EntryStep.IsValid())
    {
        return Lines;
    }

    Lines.Add(TEXT("=========== Steps ==========="));
    Lines.Append(PrintSteps_01(Data.EntryStep));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("=========== Common Steps ==========="));
    Lines.Append(PrintCommonSteps_01(Data.CommonSteps));
    return Lines;
}
#pragma endregion
