// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CMcpGraphTextBuilder.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "Utils/Processors/N2CNodeProcessorFactory.h"

bool FN2CMcpGraphTextBuilder::BuildGraphTextFromNodes(const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError)
{
    if (Nodes.Num() == 0)
    {
        OutError = TEXT("No nodes provided");
        return false;
    }

    UEdGraph* Graph = nullptr;
    for (UK2Node* Node : Nodes)
    {
        if (Node && Node->GetGraph())
        {
            Graph = Node->GetGraph();
            break;
        }
    }

    if (!Graph)
    {
        OutError = TEXT("No valid graph found for nodes");
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
        if (!Node)
        {
            continue;
        }

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

    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
    {
        return TEXT("(ThisNode)");
    }

    FString PinName = Pin->GetDisplayName().ToString();
    if (PinName.IsEmpty())
    {
        PinName = Pin->PinName.ToString();
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
        return PinName.IsEmpty() ? FString() : PinName;
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
        return TEXT("Unknown");
    }

    const EN2CNodeType NodeType = FN2CNodeTypeRegistry::Get().GetNodeType(Node);
    if (TSharedPtr<IN2CNodeProcessor> Processor = FN2CNodeProcessorFactory::Get().GetProcessor(NodeType))
    {
        const FString Label = Processor->GetGraphTextLabel(Node);
        if (!Label.IsEmpty())
        {
            return Label;
        }
    }

    const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
    return Title.IsEmpty() ? Node->GetClass()->GetName() : Title;
}
