// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CSettings.h"
#include "N2CMcpModule.generated.h"

#pragma region ODS
USTRUCT()
struct FN2CMcpSessionRequest
{
    GENERATED_BODY()

    FString FlowJson;
    FString ParsedJson;
    FString FlowText;
    FString BlueprintJson;
    FString PromptText;

    FString FlowFilename;
    FString ParsedFilename;

    FString GraphName;
    FString BlueprintName;

    EN2CMcpPayloadMode PayloadMode = EN2CMcpPayloadMode::RawContent;
    FString ServerBaseUrl;
    FString SessionCreateEndpoint;
};

/**
 * @class UN2CMcpModule
 * @brief MCP integration module for BP -> C++ workflow
 */
UCLASS()
class NODETOCODE_API UN2CMcpModule : public UObject
{
    GENERATED_BODY()

public:
    DECLARE_DELEGATE_ThreeParams(FN2CMcpSessionComplete, bool /*bSuccess*/, const FString& /*SessionId*/, const FString& /*Error*/);

    /** Get the singleton instance */
    static UN2CMcpModule* Get();

    /** Create a new MCP session */
    void CreateSessionAsync(const FN2CMcpSessionRequest& Request, FN2CMcpSessionComplete OnComplete);
};
#pragma endregion