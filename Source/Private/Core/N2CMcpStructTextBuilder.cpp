// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CMcpStructTextBuilder.h"

#include "Containers/Map.h"
#include "Containers/Set.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_StructOperation.h"
#include "UObject/Field.h"
#include "UObject/UnrealType.h"

bool FN2CMcpStructTextBuilder::BuildStructTextFromNodes(UEdGraph* Graph, const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError)
{
    if (Nodes.Num() == 0)
    {
        OutError = TEXT("No nodes provided");
        return false;
    }

    if (!Graph)
    {
        OutError = TEXT("No graph provided");
        return false;
    }

    TMap<FString, FCollectedStructInfo> GraphStructs;
    TMap<FString, FCollectedStructInfo> AssetStructs;
    TSet<FString> GraphVisitedStructPaths;
    TSet<FString> AssetVisitedStructPaths;

    if (const UBlueprint* Blueprint = Cast<UBlueprint>(Graph->GetOuter()))
    {
        CollectStructsFromBlueprintAsset(Blueprint, AssetStructs, AssetVisitedStructPaths);
    }

    for (UK2Node* Node : Nodes)
    {
        CollectStructsFromNode(Node, GraphStructs, GraphVisitedStructPaths);
    }

    TArray<FString> GraphStructPaths;
    GraphStructs.GetKeys(GraphStructPaths);
    for (const FString& GraphStructPath : GraphStructPaths)
    {
        AssetStructs.Remove(GraphStructPath);
    }

    TArray<FString> Lines;
    Lines.Add(TEXT(""));
    Lines.Add(TEXT(""));
    AppendStructSection(TEXT("Used Structure Types (Current Graph):"), GraphStructs, Lines);
    AppendStructSection(TEXT("Used Structure Types (Blueprint Asset Only):"), AssetStructs, Lines);

    OutText = FString::Join(Lines, TEXT("\n"));
    return true;
}

void FN2CMcpStructTextBuilder::AppendStructSection(const FString& Header, const TMap<FString, FCollectedStructInfo>& Structs, TArray<FString>& OutLines)
{
    OutLines.Add(Header);

    TArray<FString> StructPaths;
    Structs.GetKeys(StructPaths);
    StructPaths.Sort();

    for (const FString& StructPath : StructPaths)
    {
        const FCollectedStructInfo* StructInfo = Structs.Find(StructPath);
        if (!StructInfo)
        {
            continue;
        }

        OutLines.Add(TEXT(""));
        OutLines.Add(StructInfo->Path + TEXT(":"));

        for (const FString& MemberLine : StructInfo->Members)
        {
            OutLines.Add(FString::Printf(TEXT("  - %s"), *MemberLine));
        }
    }

    OutLines.Add(TEXT(""));
}

void FN2CMcpStructTextBuilder::CollectStructsFromNode(UK2Node* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (!Node)
    {
        return;
    }

    // 노드 전체 reflected property를 훑으면 에디터 메타데이터 struct가 과하게 수집된다.
    // MCP 결과와 맞추기 위해 여기서는 멤버 참조 계열 struct만 선택적으로 수집한다.
    CollectStructsFromNodeMetadata(Node, InOutStructs, InOutVisitedStructPaths);

    if (const UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
    {
        CollectStructFromNodeProperty(MakeStructNode, InOutStructs, InOutVisitedStructPaths);
    }

    if (const UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node))
    {
        CollectStructFromNodeProperty(BreakStructNode, InOutStructs, InOutVisitedStructPaths);
    }

    if (const UK2Node_StructOperation* StructOperationNode = Cast<UK2Node_StructOperation>(Node))
    {
        CollectStructFromNodeProperty(StructOperationNode, InOutStructs, InOutVisitedStructPaths);
    }

    if (const UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(Node))
    {
        CollectStructFromNodeProperty(SetFieldsNode, InOutStructs, InOutVisitedStructPaths);
    }

    for (UEdGraphPin* Pin : Node->Pins)
    {
        CollectStructFromPin(Pin, InOutStructs, InOutVisitedStructPaths);
    }
}

void FN2CMcpStructTextBuilder::CollectStructsFromBlueprintAsset(const UBlueprint* Blueprint, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (!Blueprint)
    {
        return;
    }

    CollectStructsFromClassProperties(Blueprint->GeneratedClass, InOutStructs, InOutVisitedStructPaths);
    CollectStructsFromClassProperties(Blueprint->SkeletonGeneratedClass, InOutStructs, InOutVisitedStructPaths);
}

void FN2CMcpStructTextBuilder::CollectStructsFromNodeMetadata(UK2Node* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (!Node)
    {
        return;
    }

    // 현재는 MCP 결과에서 실제로 필요한 메타데이터 struct만 좁혀서 수집한다.
    // broad scan을 다시 켜고 싶다면 이 함수만 교체하면 된다.
    static const FString SimpleMemberReferencePath = TEXT("/Script/Engine.SimpleMemberReference");

    if (UScriptStruct* SimpleMemberReferenceStruct = FindObject<UScriptStruct>(nullptr, *SimpleMemberReferencePath))
    {
        CollectStructAndDependencies(SimpleMemberReferenceStruct, InOutStructs, InOutVisitedStructPaths);
    }
}

void FN2CMcpStructTextBuilder::CollectStructFromPin(const UEdGraphPin* Pin, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (!Pin)
    {
        return;
    }

    const FEdGraphPinType& PinType = Pin->PinType;
    if (PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
    {
        return;
    }

    UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get());
    if (!Struct)
    {
        return;
    }

    CollectStructAndDependencies(Struct, InOutStructs, InOutVisitedStructPaths);
}

void FN2CMcpStructTextBuilder::CollectStructAndDependencies(UScriptStruct* Struct, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (!Struct)
    {
        return;
    }

    const FString StructPath = Struct->GetPathName();
    if (StructPath.IsEmpty())
    {
        return;
    }

    if (InOutVisitedStructPaths.Contains(StructPath))
    {
        return;
    }

    InOutVisitedStructPaths.Add(StructPath);

    InOutStructs.FindOrAdd(StructPath).Path = StructPath;

    for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
    {
        const FProperty* Property = *PropertyIt;
        if (!Property)
        {
            continue;
        }

        FCollectedStructInfo* StructInfo = InOutStructs.Find(StructPath);
        if (StructInfo)
        {
            StructInfo->Members.AddUnique(FString::Printf(TEXT("%s: %s"), *Property->GetName(), *FormatPropertyType(Property)));
        }
        CollectStructPropertyDependencies(Property, InOutStructs, InOutVisitedStructPaths);
    }
}

void FN2CMcpStructTextBuilder::CollectStructPropertyDependencies(const FProperty* Property, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (!Property)
    {
        return;
    }

    // struct 내부의 중첩 struct/container만 재귀적으로 따라간다.
    // UObject/class 참조는 여기서 확장하지 않는다.
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        CollectStructAndDependencies(StructProperty->Struct, InOutStructs, InOutVisitedStructPaths);
        return;
    }

    if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        CollectStructPropertyDependencies(ArrayProperty->Inner, InOutStructs, InOutVisitedStructPaths);
        return;
    }

    if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        CollectStructPropertyDependencies(SetProperty->ElementProp, InOutStructs, InOutVisitedStructPaths);
        return;
    }

    if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        CollectStructPropertyDependencies(MapProperty->KeyProp, InOutStructs, InOutVisitedStructPaths);
        CollectStructPropertyDependencies(MapProperty->ValueProp, InOutStructs, InOutVisitedStructPaths);
    }
}

void FN2CMcpStructTextBuilder::CollectStructsFromClassProperties(const UClass* Class, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (!Class)
    {
        return;
    }

    for (TFieldIterator<FProperty> PropertyIt(Class, EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
    {
        const FProperty* Property = *PropertyIt;
        if (!Property)
        {
            continue;
        }

        CollectStructPropertyDependencies(Property, InOutStructs, InOutVisitedStructPaths);
    }
}

void FN2CMcpStructTextBuilder::CollectStructFromNodeProperty(const UK2Node_MakeStruct* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (Node)
    {
        CollectStructAndDependencies(Node->StructType, InOutStructs, InOutVisitedStructPaths);
    }
}

void FN2CMcpStructTextBuilder::CollectStructFromNodeProperty(const UK2Node_BreakStruct* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (Node)
    {
        CollectStructAndDependencies(Node->StructType, InOutStructs, InOutVisitedStructPaths);
    }
}

void FN2CMcpStructTextBuilder::CollectStructFromNodeProperty(const UK2Node_StructOperation* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (Node)
    {
        CollectStructAndDependencies(Node->StructType, InOutStructs, InOutVisitedStructPaths);
    }
}

void FN2CMcpStructTextBuilder::CollectStructFromNodeProperty(const UK2Node_SetFieldsInStruct* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths)
{
    if (Node)
    {
        CollectStructAndDependencies(Node->StructType, InOutStructs, InOutVisitedStructPaths);
    }
}

FString FN2CMcpStructTextBuilder::FormatPropertyType(const FProperty* Property)
{
    if (!Property)
    {
        return TEXT("Unknown");
    }

    FString ExtendedType;
    FString Result = Property->GetCPPType(&ExtendedType, CPPF_None);
    return ExtendedType.IsEmpty() ? Result : Result + ExtendedType;
}
