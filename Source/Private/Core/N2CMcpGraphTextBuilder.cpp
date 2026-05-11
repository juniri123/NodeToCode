// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CMcpGraphTextBuilder.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "Utils/Processors/N2CNodeProcessorFactory.h"

bool FN2CMcpGraphTextBuilder::BuildGraphTextFromNodes(UEdGraph* Graph, const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError)
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

    const FString GraphName = Graph->GetName();
    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("Strand: %s"), *GraphName));
    if (const UObject* GraphOuter = Graph->GetOuter())
    {
        Lines.Add(FString::Printf(TEXT("Graph Path: %s:%s"), *GraphOuter->GetPathName(), *GraphName));
    }
    Lines.Add(TEXT(""));
    Lines.Add(FString::Printf(TEXT("Function Graph: %s"), *GraphName));

    for (UK2Node* Node : Nodes)
    {
        AppendNodeGraphTextLines(Node, Lines);
    }

    OutText = FString::Join(Lines, TEXT("\n"));
    return true;
}

void FN2CMcpGraphTextBuilder::AppendNodeGraphTextLines(UK2Node* Node, TArray<FString>& OutLines)
{
    if (!Node)
    {
        return;
    }

    OutLines.Add(FString::Printf(TEXT("  Node: %s (%s)"), *GetNodeLabel(Node), *FormatNodePath(Node)));

    for (UEdGraphPin* Pin : Node->Pins)
    {
        AppendPinGraphTextLines(Pin, OutLines);
    }
}

void FN2CMcpGraphTextBuilder::AppendPinGraphTextLines(const UEdGraphPin* Pin, TArray<FString>& OutLines)
{
    if (!Pin)
    {
        return;
    }

    const bool bIsOutputPin = Pin->Direction == EGPD_Output;
    const bool bHasLinks = Pin->LinkedTo.Num() > 0;

    if (!bIsOutputPin && !bHasLinks)
    {
        const FString DefaultValue = FormatDefaultValue(Pin);
        if (!DefaultValue.IsEmpty())
        {
            OutLines.Add(FString::Printf(TEXT("    %s=%s"), *FormatTargetPinName(Pin), *DefaultValue));
        }
    }

    if (!bIsOutputPin)
    {
        return;
    }

    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
    {
        if (!LinkedPin || !LinkedPin->GetOwningNode())
        {
            continue;
        }

        OutLines.Add(FString::Printf(
            TEXT("    %s->%s(%s)"),
            *FormatSourcePinName(Pin),
            *FormatTargetPinName(LinkedPin),
            *FormatNodePath(LinkedPin->GetOwningNode())
        ));
    }
}

FString FN2CMcpGraphTextBuilder::FormatNodePath(const UEdGraphNode* Node)
{
    if (!Node)
    {
        return FString();
    }

    const UEdGraph* Graph = Node->GetGraph();
    if (!Graph)
    {
        return Node->GetName();
    }

    return FString::Printf(TEXT("%s.%s"), *Graph->GetName(), *Node->GetName());
}

FString FN2CMcpGraphTextBuilder::FormatSourcePinName(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return TEXT("(UnknownPin)");
    }

    FString PinName = Pin->GetDisplayName().ToString();
    if (PinName.IsEmpty())
    {
        PinName = Pin->PinName.ToString();
    }

    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
    {
        if (PinName.IsEmpty() || IsGenericExecPinName(PinName))
        {
            return TEXT("(ThisNode)");
        }

        return PinName.Contains(TEXT(" "))
            ? FString::Printf(TEXT("`%s`"), *PinName)
            : PinName;
    }

    if (PinName.IsEmpty())
    {
        PinName = TEXT("UnknownPin");
    }

    return FString::Printf(TEXT("`%s`"), *PinName);
}

FString FN2CMcpGraphTextBuilder::FormatTargetPinName(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return FString();
    }

    FString PinName = Pin->GetDisplayName().ToString();
    if (PinName.IsEmpty())
    {
        PinName = Pin->PinName.ToString();
    }

    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
    {
        return IsGenericExecPinName(PinName) ? FString() : PinName;
    }

    if (PinName.IsEmpty())
    {
        PinName = TEXT("UnknownPin");
    }

    if (PinName == TEXT("Target"))
    {
        return TEXT("Target");
    }

    return FString::Printf(TEXT("`%s`"), *PinName);
}

bool FN2CMcpGraphTextBuilder::IsGenericExecPinName(const FString& PinName)
{
    return PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase)
        || PinName.Equals(TEXT("then"), ESearchCase::IgnoreCase);
}

FString FN2CMcpGraphTextBuilder::FormatDefaultValue(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return FString();
    }

    if (!Pin->DefaultValue.IsEmpty())
    {
        return Pin->DefaultValue;
    }

    if (Pin->DefaultObject)
    {
        return Pin->DefaultObject->GetName();
    }

    if (!Pin->DefaultTextValue.IsEmpty())
    {
        return Pin->DefaultTextValue.ToString();
    }

    return FString();
}

FString FN2CMcpGraphTextBuilder::GetNodeLabel(UK2Node* Node)
{
    if (!Node)
    {
        return FString("Unknown");
    }

    FString NodeLabel = Node->GetName();
    int32 LastUnderscoreIndex = INDEX_NONE;
    if (NodeLabel.FindLastChar(TEXT('_'), LastUnderscoreIndex) && LastUnderscoreIndex < NodeLabel.Len() - 1)
    {
        const FString Suffix = NodeLabel.Mid(LastUnderscoreIndex + 1);
        if (Suffix.IsNumeric())
        {
            NodeLabel.LeftInline(LastUnderscoreIndex);
        }
    }

    NodeLabel.RemoveFromStart(TEXT("K2Node_"));
    return NodeLabel;
}
