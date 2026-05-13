// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CMcpGraphTextBuilder.h"

#include "Core/N2CFlowBuilder.h"
#include "Core/N2CSettings.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "Utils/Processors/N2CNodeProcessorFactory.h"

namespace
{
    FString BuildProcessorDescription(UK2Node* Node)
    {
        if (!Node)
        {
            return FString();
        }

        EN2CNodeType NodeType = FN2CNodeTypeRegistry::Get().GetNodeType(Node);

        const TSharedPtr<IN2CNodeProcessor> Processor = FN2CNodeProcessorFactory::Get().GetProcessor(NodeType);
        if (!Processor.IsValid())
        {
            return FString();
        }
        
        return Processor->GetNodeDesciption(Node);
    }
}

bool FN2CMcpGraphTextBuilder::BuildGraphTextFromNodes(const FN2CFlowData& FlowData, const UEdGraph* Graph, const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError)
{
    if (!Graph)
    {
        OutError = TEXT("No graph provided");
        return false;
    }
    
    if (Nodes.Num() == 0)
    {
        OutError = TEXT("No nodes provided");
        return false;
    }

    const FString GraphName = Graph->GetName();
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    const bool bAddCompactAliasNotation = Settings && Settings->bAddCompactAliasNotation;
    const N2CFlow::FGUIDAlias* GuidAlias = &FlowData.GuidAlias;

    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("Strand: %s"), *GraphName));
    if (const UObject* GraphOuter = Graph->GetOuter())
    {
        Lines.Add(FString::Printf(TEXT("Graph Path: %s:%s"), *GraphOuter->GetPathName(), *GraphName));
    }

    if (bAddCompactAliasNotation)
    {
        Lines.Add(TEXT("Identifier Mode: CompactAlias"));
    }
    else
    {
        Lines.Add(TEXT("Identifier Mode: LegacyPath"));
    }
    Lines.Add(TEXT("Notation:"));
    Lines.Add(TEXT("- (xxx) = node name"));
    Lines.Add(TEXT("- [xxx] = node desc"));
    Lines.Add(TEXT("- `xxx` = pin name"));
    Lines.Add(TEXT("- N# = node id"));
    Lines.Add(TEXT("- P# = pin id"));
    Lines.Add(TEXT("- P#@N# = 'pin id' at 'node id'"));    

    Lines.Add(TEXT(""));
    Lines.Add(FString::Printf(TEXT("Function Graph: %s"), *GraphName));

    for (UK2Node* Node : Nodes)
    {
        AppendNodeGraphTextLines(Node, Lines, GuidAlias, bAddCompactAliasNotation);
    }

    OutText = FString::Join(Lines, TEXT("\n"));
    return true;
}

void FN2CMcpGraphTextBuilder::AppendNodeGraphTextLines(UK2Node* Node, TArray<FString>& OutLines, const N2CFlow::FGUIDAlias* GuidAlias, bool bUseCompactAliasNotation)
{
    if (!Node)
    {
        return;
    }

    const FString CompactNodeRef = bUseCompactAliasNotation ? FormatCompactNodeRef(Node, GuidAlias) : FString();
    const FString ProcessorDescription = BuildProcessorDescription(Node);
    OutLines.Add(FString::Printf(TEXT("  Node: %s [%s] %s(%s)"), *GetK2NodeTypeName(Node), *ProcessorDescription, *CompactNodeRef, *Node->GetName()));

    for (UEdGraphPin* Pin : Node->Pins)
    {
        AppendPinGraphTextLines(Pin, OutLines, GuidAlias, bUseCompactAliasNotation);
    }
}

void FN2CMcpGraphTextBuilder::AppendPinGraphTextLines(const UEdGraphPin* Pin, TArray<FString>& OutLines, const N2CFlow::FGUIDAlias* GuidAlias, bool bUseCompactAliasNotation)
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
            OutLines.Add(FString::Printf(TEXT("    %s=%s"), *FormatTargetPinName(Pin, GuidAlias, bUseCompactAliasNotation), *DefaultValue));
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
            *FormatSourcePinName(Pin, GuidAlias, bUseCompactAliasNotation),
            *FormatTargetPinName(LinkedPin, GuidAlias, bUseCompactAliasNotation),
            *FormatNodePath(LinkedPin->GetOwningNode())
        ));
    }
}

FString FN2CMcpGraphTextBuilder::FormatCompactNodeRef(const UEdGraphNode* Node, const N2CFlow::FGUIDAlias* GuidAlias)
{
    if (!Node || !GuidAlias)
    {
        return FString();
    }

    return GuidAlias->ResolveNodeID(Node->NodeGuid.ToString(EGuidFormats::Digits));
}

FString FN2CMcpGraphTextBuilder::FormatCompactPinRef(const UEdGraphPin* Pin, const N2CFlow::FGUIDAlias* GuidAlias)
{
    if (!Pin || !GuidAlias)
    {
        return FString();
    }

    const FString PinAlias = GuidAlias->ResolvePinID(Pin->PinId.ToString(EGuidFormats::Digits));
    const UEdGraphNode* OwningNode = Pin->GetOwningNode();
    const FString NodeAlias = OwningNode ? FormatCompactNodeRef(OwningNode, GuidAlias) : FString();

    return FString::Printf(TEXT("%s@%s"), *PinAlias, *NodeAlias);
}

FString FN2CMcpGraphTextBuilder::FormatNodePath(const UEdGraphNode* Node)
{
    if (!Node)
    {
        return FString();
    }

    return Node->GetName();
}

FString FN2CMcpGraphTextBuilder::FormatSourcePinName(const UEdGraphPin* Pin, const N2CFlow::FGUIDAlias* GuidAlias, bool bUseCompactAliasNotation)
{
    if (!Pin)
    {
        return TEXT("(UnknownPin)");
    }

    FString PinName = Pin->GetDisplayName().ToString();
    const FString CompactPinRef = bUseCompactAliasNotation ? FormatCompactPinRef(Pin, GuidAlias) : FString();

    if (PinName.IsEmpty())
    {
        PinName = Pin->PinName.ToString();
    }

    // if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
    // {
    //     if (PinName.IsEmpty() || IsGenericExecPinName(PinName))
    //     {
    //         PinName = TEXT("(ThisNode)");
    //     }
    // }

    if (PinName.IsEmpty())
    {
        PinName = TEXT("UnknownPin");
    }

    return FString::Printf(TEXT("%s`%s`"), *CompactPinRef, *PinName);
}

FString FN2CMcpGraphTextBuilder::FormatTargetPinName(const UEdGraphPin* Pin, const N2CFlow::FGUIDAlias* GuidAlias, bool bUseCompactAliasNotation)
{
    if (!Pin)
    {
        return FString();
    }

    FString PinName = Pin->GetDisplayName().ToString();
    const FString CompactPinRef = bUseCompactAliasNotation ? FormatCompactPinRef(Pin, GuidAlias) : FString();

    if (PinName.IsEmpty())
    {
        PinName = Pin->PinName.ToString();
    }

    // if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
    // {
    //     PinName = IsGenericExecPinName(PinName) ? FString() : PinName;
    // }
    // else {
        if (PinName.IsEmpty())
        {
            PinName = TEXT("UnknownPin");
        }
    // }

    return FString::Printf(TEXT("%s`%s`"),*CompactPinRef, *PinName);
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

FString FN2CMcpGraphTextBuilder::GetK2NodeTypeName(const UEdGraphNode* Node)
{
    if (!Node)
    {
        return FString("Invalid Node");
    }

    FString Name = Node->GetClass()->GetName();
    Name.RemoveFromStart(TEXT("K2Node_"));
    return Name;
}
