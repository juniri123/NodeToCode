// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CFunctionCallProcessor.h"

FString FN2CFunctionCallProcessor::GetNodeDesciption(const UEdGraphNode* Node)
{
    const UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node);
    if (!FuncNode)
    {
        return FString();
    }

    if (UFunction* Function = FuncNode->GetTargetFunction())
    {
        const FString MemberParent = GetCleanClassName(Function->GetOwnerClass()->GetName());
        const FString MemberName = GetCleanClassName(Function->GetName());
        const bool bLatent = FuncNode->IsLatentFunction();
        return FString::Printf(TEXT("%s, Class: %s, Latent: %s"),
            *MemberName,
            *MemberParent,
            bLatent ? TEXT("true") : TEXT("false"));
    }

    return FString();
}

void FN2CFunctionCallProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node);
    if (!FuncNode)
    {
        return;
    }
    
    if (UFunction* Function = FuncNode->GetTargetFunction())
    {
        OutNodeDef.MemberParent = GetCleanClassName(Function->GetOwnerClass()->GetName());
        OutNodeDef.MemberName = GetCleanClassName(Function->GetName());
        OutNodeDef.bLatent = FuncNode->IsLatentFunction();
        
        // Log function details
        FString FunctionInfo = FString::Printf(TEXT("Function call: %s::%s, Latent: %s"),
            *OutNodeDef.MemberParent,
            *OutNodeDef.MemberName,
            OutNodeDef.bLatent ? TEXT("true") : TEXT("false"));
        #pragma region ODS
        OutNodeDef.Note = FunctionInfo;
        #pragma endregion
        FN2CLogger::Get().Log(FunctionInfo, EN2CLogSeverity::Debug);
    }
}

