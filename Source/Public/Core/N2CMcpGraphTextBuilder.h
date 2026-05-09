// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UK2Node;
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
    static bool BuildGraphTextFromNodes(const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError);

private:
    static void AppendNodeGraphTextLines(UK2Node* Node, TArray<FString>& OutLines);
    static void AppendPinGraphTextLines(const UEdGraphPin* Pin, TArray<FString>& OutLines);
    static FString FormatNodePath(const UEdGraphNode* Node);
    static FString FormatPinName(const UEdGraphPin* Pin);
    static FString FormatDefaultValue(const UEdGraphPin* Pin);
    static FString FormatLinkTarget(const UEdGraphPin* LinkedPin);
    static FString GetNodeLabel(UK2Node* Node);
};
