// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CParsedDumpBuilder.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#pragma region ODS
namespace
{
// bool 값을 파이썬 덤프 스타일 문자열로 변환
FString BoolString(bool bValue)
{
    return bValue ? TEXT("True") : TEXT("False");
}

// EGPD 방향 문자열화
FString PinDirectionToString(EEdGraphPinDirection Direction)
{
    return (Direction == EGPD_Input) ? TEXT("EGPD_Input") : TEXT("EGPD_Output");
}

// PinType의 MemberReference를 덤프 스타일 문자열로 변환
FString MemberRefToString(const FMemberReference& Ref)
{
    if (Ref.GetMemberName().IsNone() && !Ref.GetMemberGuid().IsValid())
    {
        return TEXT("()");
    }

    const FString ParentPath = Ref.GetMemberParentClass()
        ? Ref.GetMemberParentClass()->GetPathName()
        : TEXT("None");

    return FString::Printf(
        TEXT("(MemberName=\"%s\",MemberGuid=%s,MemberParent=%s,bSelfContext=%s)"),
        *Ref.GetMemberName().ToString(),
        Ref.GetMemberGuid().IsValid()
            ? *Ref.GetMemberGuid().ToString(EGuidFormats::Digits)
            : TEXT("None"),
        *ParentPath,
        *BoolString(Ref.IsSelfContext())
    );
}

FString MemberRefToString(const FSimpleMemberReference& Ref)
{
    if (Ref.MemberName.IsNone() && !Ref.MemberGuid.IsValid())
    {
        return TEXT("()");
    }

    const FString ParentPath = Ref.MemberParent
        ? Ref.MemberParent->GetPathName()
        : TEXT("None");

    return FString::Printf(
        TEXT("(MemberName=\"%s\",MemberGuid=%s,MemberParent=%s)"),
        *Ref.MemberName.ToString(),
        Ref.MemberGuid.IsValid()
            ? *Ref.MemberGuid.ToString(EGuidFormats::Digits)
            : TEXT("None"),
        *ParentPath
    );
}

// FMemberReference → JSON
TSharedPtr<FJsonObject> MemberRefToJson(const FMemberReference& Ref)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    if (!Ref.GetMemberName().IsNone())
    {
        Obj->SetStringField(TEXT("MemberName"), Ref.GetMemberName().ToString());
    }
    if (Ref.GetMemberGuid().IsValid())
    {
        Obj->SetStringField(TEXT("MemberGuid"), Ref.GetMemberGuid().ToString(EGuidFormats::Digits));
    }
    if (Ref.GetMemberParentClass())
    {
        Obj->SetStringField(TEXT("MemberParent"), Ref.GetMemberParentClass()->GetPathName());
    }
    Obj->SetBoolField(TEXT("bSelfContext"), Ref.IsSelfContext());
    return Obj;
}

// PinType → JSON (덤프 스타일 필드 구성)
TSharedPtr<FJsonObject> PinTypeToJson(const FEdGraphPinType& PinType)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("PinCategory"), PinType.PinCategory.ToString());
    Obj->SetStringField(TEXT("PinSubCategory"), PinType.PinSubCategory.ToString());
    Obj->SetStringField(TEXT("PinSubCategoryObject"), PinType.PinSubCategoryObject.IsValid() ? PinType.PinSubCategoryObject->GetPathName() : TEXT("None"));
    Obj->SetStringField(TEXT("PinSubCategoryMemberReference"), MemberRefToString(PinType.PinSubCategoryMemberReference));
    Obj->SetStringField(TEXT("PinValueType"), TEXT("()"));
    const UEnum* ContainerEnum = StaticEnum<EPinContainerType>();
    Obj->SetStringField(
        TEXT("ContainerType"),
        ContainerEnum
            ? ContainerEnum->GetNameStringByValue(static_cast<int64>(PinType.ContainerType))
            : TEXT("None")
    );
    Obj->SetStringField(TEXT("bIsReference"), BoolString(PinType.bIsReference));
    Obj->SetStringField(TEXT("bIsConst"), BoolString(PinType.bIsConst));
    Obj->SetStringField(TEXT("bIsWeakPointer"), BoolString(PinType.bIsWeakPointer));
    Obj->SetStringField(TEXT("bIsUObjectWrapper"), BoolString(PinType.bIsUObjectWrapper));
    Obj->SetStringField(TEXT("bSerializeAsSinglePrecisionFloat"), BoolString(PinType.bSerializeAsSinglePrecisionFloat));
    return Obj;
}
}

bool FN2CParsedDumpBuilder::BuildParsedJsonFromGraph(UEdGraph* Graph, FString& OutJson, FString& OutError)
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

    return BuildParsedJsonFromNodes(Nodes, OutJson, OutError);
}

bool FN2CParsedDumpBuilder::BuildParsedJsonFromNodes(const TArray<UK2Node*>& Nodes, FString& OutJson, FString& OutError)
{
    TArray<TSharedPtr<FJsonValue>> NodeArray;

    for (UK2Node* Node : Nodes)
    {
        if (!Node)
        {
            continue;
        }

        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

        // 덤프의 주요 필드 재구성
        NodeObj->SetStringField(TEXT("Class"), Node->GetClass()->GetPathName());
        NodeObj->SetStringField(TEXT("Name"), Node->GetName());
        NodeObj->SetStringField(TEXT("ExportPath"), Node->GetPathName());

        if (Node->NodePosX != 0)
        {
            NodeObj->SetNumberField(TEXT("NodePosX"), Node->NodePosX);
        }
        if (Node->NodePosY != 0)
        {
            NodeObj->SetNumberField(TEXT("NodePosY"), Node->NodePosY);
        }
        if (Node->NodeGuid.IsValid())
        {
            NodeObj->SetStringField(TEXT("NodeGuid"), Node->NodeGuid.ToString(EGuidFormats::Digits));
        }

        // 함수 참조/변수 참조/매크로 참조
        if (const UK2Node_CallFunction* CallFunc = Cast<UK2Node_CallFunction>(Node))
        {
            NodeObj->SetObjectField(TEXT("FunctionReference"), MemberRefToJson(CallFunc->FunctionReference));
            NodeObj->SetStringField(TEXT("bDefaultsToPureFunc"), BoolString(CallFunc->bDefaultsToPureFunc));
        }
        else if (const UK2Node_FunctionEntry* FuncEntry = Cast<UK2Node_FunctionEntry>(Node))
        {
            NodeObj->SetObjectField(TEXT("FunctionReference"), MemberRefToJson(FuncEntry->FunctionReference));
            NodeObj->SetStringField(TEXT("bIsEditable"), BoolString(FuncEntry->bIsEditable));
        }
        else if (const UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
        {
            NodeObj->SetObjectField(TEXT("VariableReference"), MemberRefToJson(VarGet->VariableReference));
        }
        else if (const UK2Node_VariableSet* VarSet = Cast<UK2Node_VariableSet>(Node))
        {
            NodeObj->SetObjectField(TEXT("VariableReference"), MemberRefToJson(VarSet->VariableReference));
        }
        else if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
        {
            if (UEdGraph* MacroGraph = Macro->GetMacroGraph())
            {
                NodeObj->SetStringField(TEXT("MacroGraphReference"), MacroGraph->GetPathName());
            }
        }

        // Pins
        TArray<TSharedPtr<FJsonValue>> PinArray;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
            PinObj->SetStringField(TEXT("PinId"), Pin->PinId.ToString(EGuidFormats::Digits));
            PinObj->SetStringField(TEXT("PinName"), Pin->PinName.ToString());
            if (!Pin->PinFriendlyName.IsEmpty())
            {
                PinObj->SetStringField(TEXT("PinFriendlyName"), Pin->PinFriendlyName.ToString());
            }
            PinObj->SetStringField(TEXT("Direction"), PinDirectionToString(Pin->Direction));
            PinObj->SetObjectField(TEXT("PinType"), PinTypeToJson(Pin->PinType));

            // LinkedTo
            if (Pin->LinkedTo.Num() > 0)
            {
                TArray<TSharedPtr<FJsonValue>> LinkedArray;
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    if (!Linked || !Linked->GetOwningNode())
                    {
                        continue;
                    }
                    TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                    LinkObj->SetStringField(TEXT("Node"), Linked->GetOwningNode()->GetName());
                    LinkObj->SetStringField(TEXT("PinId"), Linked->PinId.ToString(EGuidFormats::Digits));
                    LinkedArray.Add(MakeShared<FJsonValueObject>(LinkObj));
                }
                PinObj->SetArrayField(TEXT("LinkedTo"), LinkedArray);
            }

            // 덤프에서 사용하는 핀 메타들
            PinObj->SetStringField(TEXT("PersistentGuid"), Pin->PersistentGuid.ToString(EGuidFormats::Digits));
            PinObj->SetStringField(TEXT("bHidden"), BoolString(Pin->bHidden));
            PinObj->SetStringField(TEXT("bNotConnectable"), BoolString(Pin->bNotConnectable));
            PinObj->SetStringField(TEXT("bDefaultValueIsReadOnly"), BoolString(Pin->bDefaultValueIsReadOnly));
            PinObj->SetStringField(TEXT("bDefaultValueIsIgnored"), BoolString(Pin->bDefaultValueIsIgnored));
            PinObj->SetStringField(TEXT("bAdvancedView"), BoolString(Pin->bAdvancedView));
            PinObj->SetStringField(TEXT("bOrphanedPin"), BoolString(Pin->bOrphanedPin));

            if (!Pin->DefaultValue.IsEmpty())
            {
                PinObj->SetStringField(TEXT("DefaultValue"), Pin->DefaultValue);
            }
            if (!Pin->AutogeneratedDefaultValue.IsEmpty())
            {
                PinObj->SetStringField(TEXT("AutogeneratedDefaultValue"), Pin->AutogeneratedDefaultValue);
            }
            if (Pin->DefaultObject)
            {
                PinObj->SetStringField(TEXT("DefaultObject"), Pin->DefaultObject->GetPathName());
            }

            PinArray.Add(MakeShared<FJsonValueObject>(PinObj));
        }

        NodeObj->SetArrayField(TEXT("Pins"), PinArray);
        NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("Nodes"), NodeArray);

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    return true;
}
#pragma endregion