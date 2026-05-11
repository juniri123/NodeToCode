// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditor.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Utils/N2CLogger.h"
#include "LLM/IN2CLLMService.h"

class UK2Node;
class UEdGraph;

/**
 * @class FN2CEditorIntegration
 * @brief Handles integration with the Blueprint Editor
 *
 * Manages Blueprint Editor toolbar extensions and provides
 * access to the active Blueprint Editor instance.
 */
class FN2CEditorIntegration
{
public:
    static FN2CEditorIntegration& Get();

    /** Initialize integration with Blueprint Editor */
    void Initialize();

    /** Cleanup integration */
    void Shutdown();

    /** Get available themes for a language */
    TArray<FName> GetAvailableThemes(EN2CCodeLanguage Language) const;

    /** Get the default theme for a language */
    FName GetDefaultTheme(EN2CCodeLanguage Language) const;

private:
    /** Constructor */
    FN2CEditorIntegration() = default;

public:
    /** Get Blueprint Editor from active tab */
    TSharedPtr<FBlueprintEditor> GetBlueprintEditorFromTab() const;

    /** Register for Blueprint Editor callbacks */
    void RegisterBlueprintEditorCallback();

private:
    /** Map of Blueprint Editor instances to their command lists */
    TMap<TWeakPtr<FBlueprintEditor>, TSharedPtr<FUICommandList>> EditorCommandLists;

    /** Register toolbar for a specific Blueprint Editor */
    void RegisterToolbarForEditor(TSharedPtr<FBlueprintEditor> InEditor);

    /** Execute collect nodes for a specific editor */
    void ExecuteCollectNodesForEditor(TWeakPtr<FBlueprintEditor> InEditor);
    
    /** Execute copy blueprint JSON to clipboard for a specific editor */
    void ExecuteCopyJsonForEditor(TWeakPtr<FBlueprintEditor> InEditor);
    
#pragma region ODS
    /** Execute save blueprint JSON to file for a specific editor */
    void ExecuteSaveJson(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute save all analysis files for a specific editor */
    bool ExecuteSaveAnalysisFiles(TWeakPtr<FBlueprintEditor> InEditor);

    /** Save parsed/flow files for a specific collected graph */
    bool SaveParsedFlowFiles(const TArray<UK2Node*>& CollectedNodes, const FString& SafeGraphName, const FString& FlowDir) const;

    /** Save independent MCP-style graph text for a specific collected graph */
    bool SaveMcpGraphTextFile(UEdGraph* Graph, const TArray<UK2Node*>& CollectedNodes, const FString& SafeGraphName, const FString& FlowDir) const;

    /** Save independent MCP-style struct text for a specific collected graph */
    bool SaveMcpStructTextFile(UEdGraph* Graph, const TArray<UK2Node*>& CollectedNodes, const FString& SafeGraphName, const FString& FlowDir) const;

    /** Execute save parsed JSON file for a specific editor */
    void ExecuteSaveParsedJson(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute save flow JSON file for a specific editor */
    void ExecuteSaveFlowJson(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute save flow text file for a specific editor */
    void ExecuteSaveFlowText(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute open save folder for a specific editor */
    void ExecuteOpenSaveFolder(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute BP to C++ conversion using MCP workflow */
    void ExecuteBp2CppUsingMCP(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute inspect-blueprint MCP API request and save the response to file */
    void ExecuteInspectBlueprintAuraMCP(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute copy flow text to clipboard for a specific editor */
    void ExecuteCopyFlowText(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute copy flow JSON to clipboard for a specific editor */
    void ExecuteCopyFlowJson(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute copy parsed JSON to clipboard for a specific editor */
    void ExecuteCopyParsedJson(TWeakPtr<FBlueprintEditor> InEditor);
#pragma endregion

    /** Handle asset editor opened callback */
    void HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance);

    
    #pragma region - ODS
    struct FMcpLlmContext
    {
        FString BlueprintName;
        FString PromptText;
        FString FlowText;
        FString InspectGraphText;
        FString InspectStructsText;
        FString FlowDir;
        FString GraphName;
    };

    void OnMcpSessionComplete(bool bSuccess, const FString& SessionId, const FString& Error);
    void SendMcpRequestToLLM(const FString& SessionId);
    void OnMcpLlmResponse(const FString& Response);

    TSharedPtr<FMcpLlmContext> PendingMcpContext;

    /** Long-lived progress notification shown while the LLM request is in flight. */
    TSharedPtr<class SNotificationItem> PendingLlmProgressNotification;
    #pragma endregion

};
