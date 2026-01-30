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

namespace
{
FString GuidToString(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::Digits);
}

FString PinDisplayName(const UEdGraphPin* Pin)
{
    return Pin ? Pin->GetDisplayName().ToString() : FString();
}

TSharedPtr<N2CFlow::Step> FindStepByKey(const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey, const FString& Key)
{
    const TSharedPtr<N2CFlow::Step>* Found = StepsByKey.Find(Key);
    return Found ? *Found : nullptr;
}

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

TSharedPtr<N2CFlow::Step> FindLCA(const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders,
                                 const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey)
{
    if (Placeholders.Num() == 0)
    {
        return nullptr;
    }

    TArray<TArray<TSharedPtr<N2CFlow::Step>>> Chains;
    Chains.Reserve(Placeholders.Num());
    for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
    {
        TArray<TSharedPtr<N2CFlow::Step>> Chain = GetParentChain(Placeholder, StepsByKey);
        Algo::Reverse(Chain);
        Chains.Add(MoveTemp(Chain));
    }

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

bool IsSwitchFallthroughCase(const TSharedPtr<N2CFlow::Step>& Lca,
                             const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders,
                             const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey)
{
    if (!Lca.IsValid() || !Lca->Node.IsValid())
    {
        return false;
    }

    if (!Lca->Node->Name.ToLower().Contains(TEXT("switch")))
    {
        return false;
    }

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

    if (Callstacks.Num() != 1)
    {
        return false;
    }

    for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
    {
        for (const N2CFlow::Pin& Pin : Placeholder->FromPins)
        {
            if (Pin.Name.ToLower() == TEXT("default"))
            {
                return false;
            }
        }
    }

    return true;
}

struct FMergingGroup
{
    TSharedPtr<N2CFlow::Step> CommonStep;
    TSharedPtr<N2CFlow::Step> MergingPointStep;
    TArray<TSharedPtr<N2CFlow::Step>> Placeholders;
    bool bIsFallthrough = false;
};

TArray<FMergingGroup> BuildPlaceholderGroups(const TArray<TSharedPtr<N2CFlow::Step>>& AllPlaceholders,
                                             const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey)
{
    TMap<FString, FString> CallstackByKey;
    for (const TSharedPtr<N2CFlow::Step>& Placeholder : AllPlaceholders)
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
        CallstackByKey.Add(Placeholder->Key, FString::Join(Names, TEXT("/")));
    }

    TArray<TSharedPtr<N2CFlow::Step>> LeaderPlaceholders;
    for (const TSharedPtr<N2CFlow::Step>& Candidate : AllPlaceholders)
    {
        const FString& CandidateStack = CallstackByKey[Candidate->Key];
        bool bIsLeader = true;
        for (const TSharedPtr<N2CFlow::Step>& Other : AllPlaceholders)
        {
            if (Other == Candidate)
            {
                continue;
            }
            const FString& OtherStack = CallstackByKey[Other->Key];
            if (OtherStack.Contains(CandidateStack))
            {
                bIsLeader = false;
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

    TMap<TSharedPtr<N2CFlow::Step>, TArray<TSharedPtr<N2CFlow::Step>>> Groups;
    for (const TSharedPtr<N2CFlow::Step>& Leader : LeaderPlaceholders)
    {
        Groups.Add(Leader, {});
    }

    for (const TSharedPtr<N2CFlow::Step>& Placeholder : AllPlaceholders)
    {
        if (LeaderPlaceholders.Contains(Placeholder))
        {
            Groups[Placeholder].Add(Placeholder);
            continue;
        }

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
}

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

    TSharedPtr<N2CFlow::Step> OldNext = MergingPointCandidate->Next;

    TSharedPtr<N2CFlow::Step> MergePoint = MakeShared<N2CFlow::Step>();
    MergePoint->Key = FString::Printf(TEXT("%s_Merging_[%s]"), *CommonStep->Key, *GroupedIdxes);
    MergePoint->Node = CommonStep->Node;
    MergePoint->bIsMergingPoint = true;
    MergePoint->FromPins.Add(N2CFlow::Pin(TEXT(""), TEXT(""), MergingPointCandidate->Key, TEXT("")));

    MergePoint->LogicDepth = OldNext.IsValid() ? OldNext->LogicDepth : MergingPointCandidate->LogicDepth;

    StepsByKey.Add(MergePoint->Key, MergePoint);

    MergingPointCandidate->Next = MergePoint;
    MergePoint->Next = OldNext;

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

    for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
    {
        MergePoint->RecordCommonPlaceholder(Placeholder);
        Placeholder->bIsCommentOut = true;
    }

    return MergePoint;
}

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

    TSharedPtr<N2CFlow::Step> MergePoint = MakeShared<N2CFlow::Step>();
    MergePoint->Key = FString::Printf(TEXT("%s_Merging_[%s]"), *CommonStep->Key, *GroupedIdxes);
    MergePoint->Node = CommonStep->Node;
    MergePoint->bIsMergingPoint = true;
    MergePoint->FromPins.Add(N2CFlow::Pin(TEXT(""), TEXT(""), MergingPointCandidate->Key, TEXT("")));

    MergePoint->LogicDepth = MergingPointCandidate->LogicDepth + 1;

    StepsByKey.Add(MergePoint->Key, MergePoint);

    MergingPointCandidate->Next = MergePoint;

    for (const TSharedPtr<N2CFlow::Step>& Placeholder : Placeholders)
    {
        MergePoint->RecordCommonPlaceholder(Placeholder);
        Placeholder->bIsCommentOut = true;
        Placeholder->bIsFallthrough = true;
    }

    return MergePoint;
}

void CollectCommonPlaceholders(
    const TSharedPtr<N2CFlow::Step>& Step,
    const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey,
    TMap<FString, TArray<TSharedPtr<N2CFlow::Step>>>& PlaceholdersByCommonKey)
{
    if (!Step.IsValid())
    {
        return;
    }

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

    for (const TSharedPtr<N2CFlow::Step>& Branch : Step->Branches)
    {
        CollectCommonPlaceholders(Branch, StepsByKey, PlaceholdersByCommonKey);
    }

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

    if (!BuildNodesFromK2Nodes(Nodes, OutData.NodesByName))
    {
        OutError = TEXT("Failed to build nodes");
        return false;
    }

    BuildStepsFromNodes(OutData.NodesByName, OutData.StepsByKey);
    OutData.EntryStep = FindEntryStep(OutData.StepsByKey);
    if (!OutData.EntryStep.IsValid())
    {
        OutError = TEXT("No entry node found");
        return false;
    }

    OutData.EntryStep->MakeAsEntry(0);
    if (!BuildExecFlow(OutData.NodesByName, OutData.StepsByKey, OutData.CommonSteps, OutData.EntryStep, OutError))
    {
        return false;
    }

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
    for (UK2Node* K2Node : Nodes)
    {
        if (!K2Node)
        {
            continue;
        }

        const FString NodeName = K2Node->GetName();
        const FString NodeGuid = GuidToString(K2Node->NodeGuid);

        TSharedPtr<N2CFlow::Node> FlowNode = MakeShared<N2CFlow::Node>(NodeName, NodeGuid);

        for (UEdGraphPin* Pin : K2Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            const FString PinName = PinDisplayName(Pin);
            const FString PinGuid = GuidToString(Pin->PinId);
            const bool bIsExec = (Pin->PinType.PinCategory == TEXT("exec"));

            N2CFlow::Pin LocalPin(PinName, PinGuid, NodeName, NodeGuid);

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

                N2CFlow::Pin RemotePin(LinkedPinName, LinkedPinGuid, LinkedNodeName, LinkedNodeGuid);

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
    TArray<TSharedPtr<N2CFlow::Step>> Stack;
    Stack.Add(EntryStep);
    int32 LogicDepth = EntryStep->LogicDepth;

    while (Stack.Num() > 0)
    {
        TSharedPtr<N2CFlow::Step> CurrentStep = Stack.Last();
        CurrentStep->NextOutlinkIdx();

        if (CurrentStep->HasAllOutlinkProcessed())
        {
            Stack.Pop();
            LogicDepth = CurrentStep->PopLogicDepth(LogicDepth);
            continue;
        }

        const N2CFlow::Link* Link = CurrentStep->GetOutlink();
        if (!Link)
        {
            OutError = TEXT("Broken link: missing outlink");
            return false;
        }

        const FString NextNodeName = Link->ToPin.NodeName;
        TSharedPtr<N2CFlow::Step> NextStep = FindStepByKey(StepsByKey, NextNodeName);
        if (!NextStep.IsValid())
        {
            OutError = FString::Printf(TEXT("Broken link: %s -> %s"), *CurrentStep->Node->Name, *NextNodeName);
            return false;
        }

        NextStep->AppendFromPin(*Link);

        if (NextStep->IsCommonStep())
        {
            if (!CommonSteps.Contains(NextNodeName))
            {
                CommonSteps.Add(NextNodeName, NextStep);
                NextStep->MakeAsEntry(0);
                if (!BuildExecFlow(NodesByName, StepsByKey, CommonSteps, NextStep, OutError))
                {
                    return false;
                }
            }

            TSharedPtr<N2CFlow::Step> CommonPlaceholder = MakeShared<N2CFlow::Step>();
            CommonPlaceholder->Key = FString::Printf(TEXT("%s_PlaceHolder_%d"), *NextNodeName, NextStep->FromPins.Num() - 1);
            CommonPlaceholder->Node = NextStep->Node;
            CommonPlaceholder->AppendFromPin(*Link);
            CommonPlaceholder->bIsCommonPlaceholder = true;
            CommonPlaceholder->bIsBranched = CurrentStep->HasBranches();
            CommonPlaceholder->LogicDepth = LogicDepth;

            StepsByKey.Add(CommonPlaceholder->Key, CommonPlaceholder);
            NextStep->RecordCommonPlaceholder(CommonPlaceholder);

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
            NextStep->LogicDepth = LogicDepth;
            NextStep->bIsBranched = CurrentStep->HasBranches();

            if (NextStep->bIsBranched)
            {
                CurrentStep->AppendBranch(NextStep);
            }
            else
            {
                CurrentStep->SetNext(NextStep);
            }

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
    TMap<FString, TArray<TSharedPtr<N2CFlow::Step>>> PlaceholdersByCommonKey;
    CollectCommonPlaceholders(EntryStep, StepsByKey, PlaceholdersByCommonKey);

    for (const TPair<FString, TArray<TSharedPtr<N2CFlow::Step>>>& Pair : PlaceholdersByCommonKey)
    {
        const TArray<TSharedPtr<N2CFlow::Step>>& Placeholders = Pair.Value;
        TArray<FMergingGroup> Groups = BuildPlaceholderGroups(Placeholders, StepsByKey);

        for (const FMergingGroup& Group : Groups)
        {
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
    RootObject->SetStringField(TEXT("entry_step_key"), Data.EntryStep->Key);

    TArray<TSharedPtr<FJsonValue>> CommonKeys;
    for (const TPair<FString, TSharedPtr<N2CFlow::Step>>& Pair : Data.CommonSteps)
    {
        CommonKeys.Add(MakeShared<FJsonValueString>(Pair.Key));
    }
    RootObject->SetArrayField(TEXT("common_step_keys"), CommonKeys);

    TSharedPtr<FJsonObject> StepsObject = MakeShared<FJsonObject>();
    for (const TPair<FString, TSharedPtr<N2CFlow::Step>>& Pair : Data.StepsByKey)
    {
        if (Pair.Value.IsValid())
        {
            StepsObject->SetObjectField(Pair.Key, Pair.Value->ToJsonObject());
        }
    }
    RootObject->SetObjectField(TEXT("steps"), StepsObject);

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
