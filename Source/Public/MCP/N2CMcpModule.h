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

USTRUCT()
struct FN2CMcpInspectBlueprintRequest
{
    GENERATED_BODY()

    FString AssetPath;
    TArray<FString> Strands;
    bool bRefresh = true;

    FString ServerBaseUrl;
    FString InspectBlueprintEndpoint;
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
    DECLARE_DELEGATE_ThreeParams(FN2CMcpInspectBlueprintComplete, bool /*bSuccess*/, const FString& /*ResponseBody*/, const FString& /*Error*/);

    /** Get the singleton instance */
    static UN2CMcpModule* Get();

    /** Create a new MCP session */
    void CreateSessionAsync(const FN2CMcpSessionRequest& Request, FN2CMcpSessionComplete OnComplete);

    /** Call the inspect-blueprint MCP API */
    void InspectBlueprintAsync(const FN2CMcpInspectBlueprintRequest& Request, FN2CMcpInspectBlueprintComplete OnComplete);

    /** Download a cached inspect-blueprint file by absolute server-side path */
    void DownloadInspectFileAsync(
        const FString& ServerBaseUrl,
        const FString& FileEndpoint,
        const FString& AbsoluteFilePath,
        FN2CMcpInspectBlueprintComplete OnComplete);
};
#pragma endregion