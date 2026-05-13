// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CCastProcessor.h"

FString FN2CCastProcessor::GetNodeDesciption(const UEdGraphNode* Node)
{
    if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
    {
        if (UClass* TargetType = CastNode->TargetType)
        {
            const FString MemberName = GetCleanClassName(TargetType->GetName());
            return FString::Printf(TEXT("Target Type: %s"), *MemberName);
        }
    }

    if (const UK2Node_ClassDynamicCast* ClassCastNode = Cast<UK2Node_ClassDynamicCast>(Node))
    {
        if (UClass* TargetType = ClassCastNode->TargetType)
        {
            const FString MemberName = GetCleanClassName(TargetType->GetName());
            return FString::Printf(TEXT("Target Type: %s"), *MemberName);
        }
    }

    if (const UK2Node_CastByteToEnum* ByteToEnumNode = Cast<UK2Node_CastByteToEnum>(Node))
    {
        if (UEnum* Enum = ByteToEnumNode->Enum)
        {
            const FString MemberName = GetCleanClassName(Enum->GetName());
            return FString::Printf(TEXT("Enum Type: %s"), *MemberName);
        }
    }

    return FString();
}

void FN2CCastProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle dynamic cast nodes
    if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
    {
        if (UClass* TargetType = CastNode->TargetType)
        {
            OutNodeDef.MemberName = GetCleanClassName(TargetType->GetName());
            
            // Log cast details
            FString CastInfo = FString::Printf(TEXT("Dynamic Cast: %s, Target Type: %s"),
                *OutNodeDef.Name,
                *OutNodeDef.MemberName);
            #pragma region ODS
            OutNodeDef.Note = CastInfo;
            #pragma endregion
            FN2CLogger::Get().Log(CastInfo, EN2CLogSeverity::Debug);
        }
        return;
    }
    
    // Handle class dynamic cast nodes
    if (UK2Node_ClassDynamicCast* ClassCastNode = Cast<UK2Node_ClassDynamicCast>(Node))
    {
        if (UClass* TargetType = ClassCastNode->TargetType)
        {
            OutNodeDef.MemberName = GetCleanClassName(TargetType->GetName());
            
            // Log class cast details
            FString CastInfo = FString::Printf(TEXT("Class Dynamic Cast: %s, Target Type: %s"),
                *OutNodeDef.Name,
                *OutNodeDef.MemberName);
            #pragma region ODS
            OutNodeDef.Note = CastInfo;
            #pragma endregion
            FN2CLogger::Get().Log(CastInfo, EN2CLogSeverity::Debug);
        }
        return;
    }
    
    // Handle byte to enum cast nodes
    if (UK2Node_CastByteToEnum* ByteToEnumNode = Cast<UK2Node_CastByteToEnum>(Node))
    {
        if (UEnum* Enum = ByteToEnumNode->Enum)
        {
            OutNodeDef.MemberName = GetCleanClassName(Enum->GetName());
            
            // Log byte to enum cast details
            FString CastInfo = FString::Printf(TEXT("Byte To Enum Cast: %s, Enum Type: %s"),
                *OutNodeDef.Name,
                *OutNodeDef.MemberName);
            #pragma region ODS
            OutNodeDef.Note = CastInfo;
            #pragma endregion
            FN2CLogger::Get().Log(CastInfo, EN2CLogSeverity::Debug);
        }
        return;
    }
}
