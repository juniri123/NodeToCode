#include "Core/N2CFlowBuilder_01.h"

#include "Models/Python/N2CFlowModel.h"
#include "Utils/N2CLogger.h"

#pragma region ODS
namespace
{
    // 로직 depth만큼 indent prefix 생성
    static FString MakeIndentPrefix_01(int32 Depth)
    {
        FString Result;
        for (int32 i = 0; i < Depth; ++i)
        {
            Result += TEXT("│   ");
        }
        return Result;
    }

    // 단일 Step을 텍스트 라인으로 변환
    TArray<FString> PrintSingleStep_01(const TSharedPtr<N2CFlow::Step>& Step, bool bWithIndent)
    {
        TArray<FString> Lines;
        if (!Step.IsValid() || !Step->Node.IsValid())
        {
            return Lines;
        }

        const FString IndentPrefix = bWithIndent ? MakeIndentPrefix_01(Step->LogicDepth) : TEXT("");

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
            const FString BranchedIndent = MakeIndentPrefix_01(Step->LogicDepth + 1);
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
