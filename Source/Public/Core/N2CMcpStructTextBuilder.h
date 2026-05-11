// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphPin;
class UBlueprint;
class UK2Node;
class UK2Node_BreakStruct;
class UK2Node_MakeStruct;
class UK2Node_SetFieldsInStruct;
class UK2Node_StructOperation;
class UScriptStruct;
class FProperty;

/**
 * Builds the MCP-style struct text file from already collected Blueprint nodes.
 * This is intentionally separate from existing parsed/flow exporters so their output stays unchanged.
 */
class NODETOCODE_API FN2CMcpStructTextBuilder
{
public:
    /** Build the human-readable struct text from collected K2 nodes. */
    static bool BuildStructTextFromNodes(UEdGraph* Graph, const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError);

private:
    struct FCollectedStructInfo
    {
        FString Path;
        TArray<FString> Members;
    };

    static void AppendStructSection(const FString& Header, const TMap<FString, FCollectedStructInfo>& Structs, TArray<FString>& OutLines);
    static void CollectStructsFromNode(UK2Node* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructsFromBlueprintAsset(const UBlueprint* Blueprint, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructsFromNodeMetadata(UK2Node* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructFromPin(const UEdGraphPin* Pin, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructAndDependencies(UScriptStruct* Struct, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructPropertyDependencies(const FProperty* Property, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructsFromClassProperties(const UClass* Class, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);

    static void CollectStructFromNodeProperty(const UK2Node_MakeStruct* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructFromNodeProperty(const UK2Node_BreakStruct* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructFromNodeProperty(const UK2Node_StructOperation* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);
    static void CollectStructFromNodeProperty(const UK2Node_SetFieldsInStruct* Node, TMap<FString, FCollectedStructInfo>& InOutStructs, TSet<FString>& InOutVisitedStructPaths);

    static FString FormatPropertyType(const FProperty* Property);
};
