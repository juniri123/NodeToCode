#include "Models/Python/N2CFlowModel.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#pragma region ODS
namespace N2CFlow
{
    Pin::Pin(FString InPinName, FString InPinGuid, FString InNodeName, FString InNodeGuid, bool bInIsExec, bool bInIsOutput)
    {
        Name = MoveTemp(InPinName);
        Guid = MoveTemp(InPinGuid);
        NodeName = MoveTemp(InNodeName);
        NodeGuid = MoveTemp(InNodeGuid);
        bIsExec = bInIsExec;
        bIsOutput = bInIsOutput;
    }

    Link::Link(Pin InFrom, Pin InTo)
    {
        FromPin = MoveTemp(InFrom);
        ToPin = MoveTemp(InTo);
    }

    Node::Node(FString InName, FString InGuid)
    {
        Name = MoveTemp(InName);
        Guid = MoveTemp(InGuid);
    }

    MergingGroup::MergingGroup(TSharedPtr<Step> InCommonStep, TSharedPtr<Step> InMergingPointStep, TArray<TSharedPtr<Step>> InPlaceholders, bool bInIsFallthrough)
    {
        CommonStep = MoveTemp(InCommonStep);
        MergingPointStep = MoveTemp(InMergingPointStep);
        Placeholders = MoveTemp(InPlaceholders);
        bIsFallthrough = bInIsFallthrough;
    }

    // Pin 배열을 JSON 배열로 변환
    static TArray<TSharedPtr<FJsonValue>> PinsToJsonArray(const TArray<Pin>& Pins)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Pins.Num());
        for (const N2CFlow::Pin& Pin : Pins)
        {
            Out.Add(MakeShared<FJsonValueObject>(Pin.ToJsonObject()));
        }
        return Out;
    }

    // Link 배열을 JSON 배열로 변환
    static TArray<TSharedPtr<FJsonValue>> LinksToJsonArray(const TArray<Link>& Links)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Links.Num());
        for (const N2CFlow::Link& Link : Links)
        {
            Out.Add(MakeShared<FJsonValueObject>(Link.ToJsonObject()));
        }
        return Out;
    }

    // JSON 배열에서 Pin 배열 복원
    static bool PinsFromJsonArray(const TArray<TSharedPtr<FJsonValue>>* JsonArray, TArray<Pin>& OutPins)
    {
        OutPins.Reset();
        if (!JsonArray)
        {
            return false;
        }

        for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object)
            {
                return false;
            }

            N2CFlow::Pin Pin;
            if (!N2CFlow::Pin::FromJsonObject(Value->AsObject(), Pin))
            {
                return false;
            }
            OutPins.Add(MoveTemp(Pin));
        }

        return true;
    }

    // JSON 배열에서 Link 배열 복원
    static bool LinksFromJsonArray(const TArray<TSharedPtr<FJsonValue>>* JsonArray, TArray<Link>& OutLinks)
    {
        OutLinks.Reset();
        if (!JsonArray)
        {
            return false;
        }

        for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object)
            {
                return false;
            }

            N2CFlow::Link Link;
            if (!N2CFlow::Link::FromJsonObject(Value->AsObject(), Link))
            {
                return false;
            }
            OutLinks.Add(MoveTemp(Link));
        }

        return true;
    }

    // JSON 문자열 배열을 FString 배열로 변환
    static TArray<FString> StringsFromJsonArray(const TArray<TSharedPtr<FJsonValue>>* JsonArray)
    {
        TArray<FString> Out;
        if (!JsonArray)
        {
            return Out;
        }

        Out.Reserve(JsonArray->Num());
        for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
        {
            if (Value.IsValid() && Value->Type == EJson::String)
            {
                Out.Add(Value->AsString());
            }
        }

        return Out;
    }

    // -------------------- Pin --------------------
    TSharedPtr<FJsonObject> Pin::ToJsonObject() const
    {
        TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
        JsonObject->SetStringField(TEXT("name"), Name);
        JsonObject->SetStringField(TEXT("guid"), Guid);
        JsonObject->SetStringField(TEXT("node_name"), NodeName);
        JsonObject->SetStringField(TEXT("node_guid"), NodeGuid);
        JsonObject->SetBoolField(TEXT("is_exec"), bIsExec);
        JsonObject->SetBoolField(TEXT("is_output"), bIsOutput);
        return JsonObject;
    }

    bool Pin::FromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, Pin& OutPin)
    {
        if (!JsonObject.IsValid())
        {
            return false;
        }

        if (!JsonObject->TryGetStringField(TEXT("name"), OutPin.Name) ||
            !JsonObject->TryGetStringField(TEXT("guid"), OutPin.Guid) ||
            !JsonObject->TryGetStringField(TEXT("node_name"), OutPin.NodeName) ||
            !JsonObject->TryGetStringField(TEXT("node_guid"), OutPin.NodeGuid))
        {
            return false;
        }

        OutPin.bIsExec = false;
        OutPin.bIsOutput = false;
        JsonObject->TryGetBoolField(TEXT("is_exec"), OutPin.bIsExec);
        JsonObject->TryGetBoolField(TEXT("is_output"), OutPin.bIsOutput);

        return true;
    }

    // -------------------- Link --------------------
    TSharedPtr<FJsonObject> Link::ToJsonObject() const
    {
        TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
        JsonObject->SetObjectField(TEXT("from"), FromPin.ToJsonObject());
        JsonObject->SetObjectField(TEXT("to"), ToPin.ToJsonObject());
        return JsonObject;
    }

    bool Link::FromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, Link& OutLink)
    {
        if (!JsonObject.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject>* FromObject = nullptr;
        const TSharedPtr<FJsonObject>* ToObject = nullptr;
        if (!JsonObject->TryGetObjectField(TEXT("from"), FromObject) ||
            !JsonObject->TryGetObjectField(TEXT("to"), ToObject))
        {
            return false;
        }

        Pin From;
        Pin To;
        if (!Pin::FromJsonObject(*FromObject, From) ||
            !Pin::FromJsonObject(*ToObject, To))
        {
            return false;
        }

        OutLink.FromPin = MoveTemp(From);
        OutLink.ToPin = MoveTemp(To);
        return true;
    }

    // -------------------- Node --------------------
    TSharedPtr<FJsonObject> Node::ToJsonObject() const
    {
        TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
        JsonObject->SetStringField(TEXT("node_name"), Name);
        JsonObject->SetStringField(TEXT("node_guid"), Guid);

        JsonObject->SetArrayField(TEXT("exec_in_pins"), PinsToJsonArray(ExecInPins));
        JsonObject->SetArrayField(TEXT("exec_out_pins"), PinsToJsonArray(ExecOutPins));
        JsonObject->SetArrayField(TEXT("exec_in_links"), LinksToJsonArray(ExecInLinks));
        JsonObject->SetArrayField(TEXT("exec_out_links"), LinksToJsonArray(ExecOutLinks));

        JsonObject->SetArrayField(TEXT("data_in_pins"), PinsToJsonArray(DataInPins));
        JsonObject->SetArrayField(TEXT("data_out_pins"), PinsToJsonArray(DataOutPins));
        JsonObject->SetArrayField(TEXT("data_in_links"), LinksToJsonArray(DataInLinks));
        JsonObject->SetArrayField(TEXT("data_out_links"), LinksToJsonArray(DataOutLinks));

        return JsonObject;
    }

    bool Node::FromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, Node& OutNode)
    {
        if (!JsonObject.IsValid())
        {
            return false;
        }

        if (!JsonObject->TryGetStringField(TEXT("node_name"), OutNode.Name) ||
            !JsonObject->TryGetStringField(TEXT("node_guid"), OutNode.Guid))
        {
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* PinsArray = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* LinksArray = nullptr;

        if (!JsonObject->TryGetArrayField(TEXT("exec_in_pins"), PinsArray) ||
            !PinsFromJsonArray(PinsArray, OutNode.ExecInPins))
        {
            return false;
        }

        if (!JsonObject->TryGetArrayField(TEXT("exec_out_pins"), PinsArray) ||
            !PinsFromJsonArray(PinsArray, OutNode.ExecOutPins))
        {
            return false;
        }

        if (!JsonObject->TryGetArrayField(TEXT("exec_in_links"), LinksArray) ||
            !LinksFromJsonArray(LinksArray, OutNode.ExecInLinks))
        {
            return false;
        }

        if (!JsonObject->TryGetArrayField(TEXT("exec_out_links"), LinksArray) ||
            !LinksFromJsonArray(LinksArray, OutNode.ExecOutLinks))
        {
            return false;
        }

        if (!JsonObject->TryGetArrayField(TEXT("data_in_pins"), PinsArray) ||
            !PinsFromJsonArray(PinsArray, OutNode.DataInPins))
        {
            return false;
        }

        if (!JsonObject->TryGetArrayField(TEXT("data_out_pins"), PinsArray) ||
            !PinsFromJsonArray(PinsArray, OutNode.DataOutPins))
        {
            return false;
        }

        if (!JsonObject->TryGetArrayField(TEXT("data_in_links"), LinksArray) ||
            !LinksFromJsonArray(LinksArray, OutNode.DataInLinks))
        {
            return false;
        }

        if (!JsonObject->TryGetArrayField(TEXT("data_out_links"), LinksArray) ||
            !LinksFromJsonArray(LinksArray, OutNode.DataOutLinks))
        {
            return false;
        }

        return true;
    }

    // -------------------- Step --------------------
    void Step::MakeAsEntry(int32 InLogicDepth)
    {
        OutLinkIdx = -1;
        LogicDepth = InLogicDepth;
    }

    int32 Step::GetOutlinkIdx()
    {
        if (!OutLinkIdx.IsSet())
        {
            OutLinkIdx = -1;
        }
        return OutLinkIdx.GetValue();
    }

    int32 Step::NextOutlinkIdx()
    {
        if (!OutLinkIdx.IsSet())
        {
            OutLinkIdx = -1;
        }
        OutLinkIdx = OutLinkIdx.GetValue() + 1;
        return OutLinkIdx.GetValue();
    }

    const Link* Step::GetOutlink() const
    {
        if (!Node.IsValid())
        {
            return nullptr;
        }

        if (!OutLinkIdx.IsSet())
        {
            return nullptr;
        }

        const int32 Idx = OutLinkIdx.GetValue();
        if (Idx < 0 || Idx >= Node->ExecOutLinks.Num())
        {
            return nullptr;
        }

        return &Node->ExecOutLinks[Idx];
    }

    bool Step::HasAllOutlinkProcessed() const
    {
        if (!Node.IsValid())
        {
            return true;
        }

        if (!OutLinkIdx.IsSet())
        {
            return false;
        }

        return OutLinkIdx.GetValue() >= Node->ExecOutLinks.Num();
    }

    bool Step::HasMultipleExecInlink() const
    {
        if (!Node.IsValid())
        {
            return false;
        }
        return Node->ExecInLinks.Num() > 1;
    }

    bool Step::HasBranches() const
    {
        if (!Node.IsValid())
        {
            return false;
        }
        return Node->ExecOutPins.Num() > 1;
    }

    bool Step::IsCommonStep() const
    {
        return HasMultipleExecInlink() && !bIsMergingPoint;
    }

    int32 Step::PopLogicDepth(int32 GlobalLogicDepth) const
    {
        return bIsBranched ? (GlobalLogicDepth - 1) : GlobalLogicDepth;
    }

    int32 Step::PushLogicDepth(int32 GlobalLogicDepth) const
    {
        return bIsBranched ? (GlobalLogicDepth + 1) : GlobalLogicDepth;
    }

    void Step::AppendFromPin(const Link& InLink)
    {
        FromPins.Add(InLink.FromPin);
    }

    void Step::AppendBranch(const TSharedPtr<Step>& InBranch)
    {
        Branches.Add(InBranch);
    }

    void Step::SetNext(const TSharedPtr<Step>& InNext)
    {
        Next = InNext;
    }

    void Step::RecordCommonPlaceholder(const TSharedPtr<Step>& InPlaceholder)
    {
        CommonPlaceholders.Add(InPlaceholder);
    }

    TSharedPtr<FJsonObject> Step::ToJsonObject() const
    {
        TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
        JsonObject->SetStringField(TEXT("key"), Key);

        FString LocalNodeGuid = NodeGuid;
        FString LocalNodeName = NodeName;
        if (Node.IsValid())
        {
            LocalNodeGuid = Node->Guid;
            LocalNodeName = Node->Name;
        }

        JsonObject->SetStringField(TEXT("node_guid"), LocalNodeGuid);
        JsonObject->SetStringField(TEXT("node_name"), LocalNodeName);

        JsonObject->SetArrayField(TEXT("from_pins"), PinsToJsonArray(FromPins));

        TArray<FString> LocalBranchKeys = BranchKeys;
        if (Branches.Num() > 0)
        {
            LocalBranchKeys.Reset();
            for (const TSharedPtr<Step>& Branch : Branches)
            {
                if (Branch.IsValid())
                {
                    LocalBranchKeys.Add(Branch->Key);
                }
            }
        }

        TArray<TSharedPtr<FJsonValue>> BranchKeyValues;
        for (const FString& BranchKey : LocalBranchKeys)
        {
            BranchKeyValues.Add(MakeShared<FJsonValueString>(BranchKey));
        }
        JsonObject->SetArrayField(TEXT("branch_keys"), BranchKeyValues);

        FString LocalNextKey = NextKey;
        if (Next.IsValid() && Next->Node.IsValid())
        {
            LocalNextKey = Next->Key;
        }
        if (!LocalNextKey.IsEmpty())
        {
            JsonObject->SetStringField(TEXT("next_key"), LocalNextKey);
        }

        JsonObject->SetNumberField(TEXT("logic_depth"), LogicDepth);
        JsonObject->SetBoolField(TEXT("is_branched"), bIsBranched);
        JsonObject->SetBoolField(TEXT("is_common_placeholder"), bIsCommonPlaceholder);
        JsonObject->SetBoolField(TEXT("is_merging_point"), bIsMergingPoint);
        JsonObject->SetBoolField(TEXT("is_comment_out"), bIsCommentOut);
        JsonObject->SetBoolField(TEXT("is_fallthrough"), bIsFallthrough);

        TArray<FString> LocalCommonKeys = CommonPlaceholderKeys;
        if (CommonPlaceholders.Num() > 0)
        {
            LocalCommonKeys.Reset();
            for (const TSharedPtr<Step>& Placeholder : CommonPlaceholders)
            {
                if (Placeholder.IsValid())
                {
                    LocalCommonKeys.Add(Placeholder->Key);
                }
            }
        }

        TArray<TSharedPtr<FJsonValue>> CommonKeyValues;
        for (const FString& CommonKey : LocalCommonKeys)
        {
            CommonKeyValues.Add(MakeShared<FJsonValueString>(CommonKey));
        }
        JsonObject->SetArrayField(TEXT("common_placeholder_keys"), CommonKeyValues);

        return JsonObject;
    }

    bool Step::FromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, Step& OutStep)
    {
        if (!JsonObject.IsValid())
        {
            return false;
        }

        if (!JsonObject->TryGetStringField(TEXT("key"), OutStep.Key))
        {
            return false;
        }

        int32 LogicDepth = 0;
        if (!JsonObject->TryGetNumberField(TEXT("logic_depth"), LogicDepth))
        {
            return false;
        }
        OutStep.LogicDepth = LogicDepth;

        JsonObject->TryGetStringField(TEXT("node_guid"), OutStep.NodeGuid);
        JsonObject->TryGetStringField(TEXT("node_name"), OutStep.NodeName);

        const TArray<TSharedPtr<FJsonValue>>* FromPinsArray = nullptr;
        if (!JsonObject->TryGetArrayField(TEXT("from_pins"), FromPinsArray) ||
            !PinsFromJsonArray(FromPinsArray, OutStep.FromPins))
        {
            return false;
        }

        JsonObject->TryGetBoolField(TEXT("is_branched"), OutStep.bIsBranched);
        JsonObject->TryGetBoolField(TEXT("is_common_placeholder"), OutStep.bIsCommonPlaceholder);
        JsonObject->TryGetBoolField(TEXT("is_merging_point"), OutStep.bIsMergingPoint);
        JsonObject->TryGetBoolField(TEXT("is_comment_out"), OutStep.bIsCommentOut);
        JsonObject->TryGetBoolField(TEXT("is_fallthrough"), OutStep.bIsFallthrough);

        const TArray<TSharedPtr<FJsonValue>>* BranchArray = nullptr;
        if (JsonObject->TryGetArrayField(TEXT("branch_keys"), BranchArray))
        {
            OutStep.BranchKeys = StringsFromJsonArray(BranchArray);
        }

        JsonObject->TryGetStringField(TEXT("next_key"), OutStep.NextKey);

        const TArray<TSharedPtr<FJsonValue>>* CommonArray = nullptr;
        if (JsonObject->TryGetArrayField(TEXT("common_placeholder_keys"), CommonArray))
        {
            OutStep.CommonPlaceholderKeys = StringsFromJsonArray(CommonArray);
        }

        return true;
    }
    
    
    
    
    FString FGUIDAlias::AcquireNodeID(const FGuid& Guid)
    {
        const FString GuidStr = Guid.ToString();
        if (GuidStr.IsEmpty())
        {
            return GuidStr;
        }

        if (FString* Existing = GuidToNID.Find(GuidStr))
        {
            return *Existing;
        }

        const FString NID = FString::Printf(TEXT("N%d"), NextNodeIndex++);
        GuidToNID.Add(GuidStr, NID);
        return NID;
    }

    FString FGUIDAlias::AcquirePinID(const FGuid& Guid)
    {
        const FString GuidStr = Guid.ToString();
        if (GuidStr.IsEmpty())
        {
            return GuidStr;
        }

        if (FString* Existing = GuidToPID.Find(GuidStr))
        {
            return *Existing;
        }

        const FString PID = FString::Printf(TEXT("P%d"), NextPinIndex++);
        GuidToPID.Add(GuidStr, PID);
        return PID;
    }

    TSharedPtr<FJsonObject> FGUIDAlias::ToJsonObject() const
    {
        TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

        TSharedPtr<FJsonObject> NodeAliasesObject = MakeShared<FJsonObject>();
        for (const TPair<FString, FString>& Pair : GuidToNID)
        {
            NodeAliasesObject->SetStringField(Pair.Key, Pair.Value);
        }
        JsonObject->SetObjectField(TEXT("GuidToNID"), NodeAliasesObject);

        TSharedPtr<FJsonObject> PinAliasesObject = MakeShared<FJsonObject>();
        for (const TPair<FString, FString>& Pair : GuidToPID)
        {
            PinAliasesObject->SetStringField(Pair.Key, Pair.Value);
        }
        JsonObject->SetObjectField(TEXT("GuidToPID"), PinAliasesObject);

        return JsonObject;
    }

    FString FGUIDAlias::ResolveNodeID(const FString& Guid) const
    {
        if (const FString* NID = GuidToNID.Find(Guid))
        {
            return *NID;
        }
        return Guid;
    }

    FString FGUIDAlias::ResolvePinID(const FString& Guid) const
    {
        if (const FString* PID = GuidToPID.Find(Guid))
        {
            return *PID;
        }
        return Guid;
    }
} // namespace N2CFlow
#pragma endregion
