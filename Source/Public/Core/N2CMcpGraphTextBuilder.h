// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Models/Python/N2CFlowModel.h"

class UK2Node;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Builds the MCP-style graph text file from already collected Blueprint nodes.
 * This is intentionally separate from existing parsed/flow exporters so their output stays unchanged.
 */
class NODETOCODE_API FN2CMcpGraphTextBuilder
{
public:
    /** Build the human-readable graph text from collected K2 nodes. */
    static bool BuildGraphTextFromNodes(const struct FN2CFlowData& FlowData, const UEdGraph* Graph, const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError);

private:
    static void AppendNodeGraphTextLines(UK2Node* Node, TArray<FString>& OutLines, const N2CFlow::FGUIDAlias* GuidAlias, bool bUseCompactAliasNotation);
    static void AppendPinGraphTextLines(const UEdGraphPin* Pin, TArray<FString>& OutLines, const N2CFlow::FGUIDAlias* GuidAlias, bool bUseCompactAliasNotation);
    static FString FormatNodePath(const UEdGraphNode* Node);
    static FString FormatSourcePinName(const UEdGraphPin* Pin, const N2CFlow::FGUIDAlias* GuidAlias, bool bUseCompactAliasNotation);
    static FString FormatTargetPinName(const UEdGraphPin* Pin, const N2CFlow::FGUIDAlias* GuidAlias, bool bUseCompactAliasNotation);
    static FString FormatCompactNodeRef(const UEdGraphNode* Node, const N2CFlow::FGUIDAlias* GuidAlias);
    static FString FormatCompactPinRef(const UEdGraphPin* Pin, const N2CFlow::FGUIDAlias* GuidAlias);
    static FString FormatDefaultValue(const UEdGraphPin* Pin);
    static bool IsGenericExecPinName(const FString& PinName);
    static FString GetK2NodeTypeName(const UEdGraphNode* Node);
};
