// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CEditorIntegration.h"

#include "BlueprintEditorModes.h"
#include "Core/N2CNodeCollector.h"
#include "BlueprintEditorModule.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CEditorWindow.h"
#include "Core/N2CFlowBuilder.h"
#include "Core/N2CParsedDumpBuilder.h"
#include "Core/N2CMcpGraphTextBuilder.h"
#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/N2CToolbarCommand.h"
#include "MCP/N2CMcpModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/IN2CLLMService.h"
#include "LLM/N2CResponseParserBase.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ISourceControlModule.h"
#include "ISourceControlChangelist.h"
#include "ISourceControlProvider.h"
#include "ISourceControlRevision.h"
#include "ISourceControlState.h"
#include "SourceControlOperations.h"
#include "Core/N2CFlowBuilder_01.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformApplicationMisc.h"
#endif

#if PLATFORM_MAC
#include "Mac/MacPlatformApplicationMisc.h"
#endif

FN2CEditorIntegration& FN2CEditorIntegration::Get()
{
    static FN2CEditorIntegration Instance;
    return Instance;
}

#pragma region ODS
namespace
{
    // N2C 확장: 소스컨트롤 CL 식별자 추출
    FString GetBlueprintChangeListNumber(const UBlueprint* Blueprint)
    {
        if (!Blueprint || !Blueprint->GetOutermost())
        {
            FN2CLogger::Get().LogWarning(TEXT("CL check: invalid Blueprint or package"));
            return FString();
        }

        if (!ISourceControlModule::Get().IsEnabled())
        {
            FN2CLogger::Get().LogWarning(TEXT("CL check: SourceControl disabled"));
            return FString();
        }

        ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
        if (!Provider.IsAvailable())
        {
            FN2CLogger::Get().LogWarning(TEXT("CL check: SourceControl provider unavailable"));
            return FString();
        }

        const FString PackageName = Blueprint->GetOutermost()->GetName();
        const FString Filename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetAssetPackageExtension()
        );
        FN2CLogger::Get().Log(FString::Printf(TEXT("CL check: filename=%s"), *Filename), EN2CLogSeverity::Debug);

        const FSourceControlStatePtr State = Provider.GetState(Filename, EStateCacheUsage::ForceUpdate);
        if (!State.IsValid())
        {
            FN2CLogger::Get().LogWarning(TEXT("CL check: state invalid"));
            return FString();
        }

        const auto TryGetIdentifierFromRevision = [](const TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe>& Revision) -> FString
        {
            if (!Revision.IsValid())
            {
                return FString();
            }

            const int32 CheckInIdentifier = Revision->GetCheckInIdentifier();
            if (CheckInIdentifier > 0)
            {
                return FString::FromInt(CheckInIdentifier);
            }

            const int32 RevisionNumber = Revision->GetRevisionNumber();
            if (RevisionNumber > 0)
            {
                return FString::FromInt(RevisionNumber);
            }

            return FString();
        };

        const FSourceControlChangelistPtr CheckInIdentifier = State->GetCheckInIdentifier();
        if (CheckInIdentifier.IsValid())
        {
            const FString Identifier = CheckInIdentifier->GetIdentifier();
            if (!Identifier.IsEmpty())
            {
                FString NumericIdentifier;
                for (const TCHAR Char : Identifier)
                {
                    if (FChar::IsDigit(Char))
                    {
                        NumericIdentifier.AppendChar(Char);
                    }
                }

                if (!NumericIdentifier.IsEmpty())
                {
                    FN2CLogger::Get().Log(FString::Printf(TEXT("CL check: check-in identifier=%s"), *NumericIdentifier), EN2CLogSeverity::Debug);
                    return NumericIdentifier;
                }

                if (!Identifier.Equals(TEXT("default"), ESearchCase::IgnoreCase))
                {
                    FN2CLogger::Get().Log(FString::Printf(TEXT("CL check: check-in identifier=%s"), *Identifier), EN2CLogSeverity::Debug);
                    return Identifier;
                }

                FN2CLogger::Get().LogWarning(TEXT("CL check: check-in identifier is default changelist"));
            }
            else
            {
                FN2CLogger::Get().LogWarning(TEXT("CL check: check-in identifier empty"));
            }
        }
        else
        {
            FN2CLogger::Get().LogWarning(TEXT("CL check: check-in identifier unavailable"));
        }

        const FString CurrentRevisionIdentifier = TryGetIdentifierFromRevision(State->GetCurrentRevision());
        if (!CurrentRevisionIdentifier.IsEmpty())
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("CL check: current revision=%s"), *CurrentRevisionIdentifier), EN2CLogSeverity::Debug);
            return CurrentRevisionIdentifier;
        }

        auto TryHistoryLookup = [&](const FSourceControlStatePtr& InState) -> FString
        {
            if (!InState.IsValid())
            {
                return FString();
            }

            const int32 HistorySize = InState->GetHistorySize();
            FN2CLogger::Get().Log(FString::Printf(TEXT("CL check: history size=%d"), HistorySize), EN2CLogSeverity::Debug);
            if (HistorySize <= 0)
            {
                return FString();
            }

            const TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> HistoryItem = InState->GetHistoryItem(0);
            return TryGetIdentifierFromRevision(HistoryItem);
        };

        FString HistoryIdentifier = TryHistoryLookup(State);
        if (!HistoryIdentifier.IsEmpty())
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("CL check: history revision=%s"), *HistoryIdentifier), EN2CLogSeverity::Debug);
            return HistoryIdentifier;
        }

        // GetState() alone may not populate history for some providers; explicitly request history.
        TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> UpdateStatusOp = ISourceControlOperation::Create<FUpdateStatus>();
        UpdateStatusOp->SetUpdateHistory(true);
        UpdateStatusOp->SetForceUpdate(true);
        UpdateStatusOp->SetQuiet(true);
        Provider.Execute(UpdateStatusOp, Filename);

        const FSourceControlStatePtr RefreshedState = Provider.GetState(Filename, EStateCacheUsage::Use);
        HistoryIdentifier = TryHistoryLookup(RefreshedState);
        if (!HistoryIdentifier.IsEmpty())
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("CL check: history revision(after update)=%s"), *HistoryIdentifier), EN2CLogSeverity::Debug);
            return HistoryIdentifier;
        }

        FN2CLogger::Get().LogWarning(TEXT("CL check: history empty (unsubmitted or no history)"));

        return FString();
    }

    bool PrepareSaveContext(
        TWeakPtr<FBlueprintEditor> InEditor,
        TArray<UK2Node*>& OutCollectedNodes,
        FString& OutSafeGraphName,
        FString& OutRootPath,
        FString& OutFlowDir)
    {
        TSharedPtr<FBlueprintEditor> Editor = InEditor.Pin();
        if (!Editor.IsValid())
        {
            FN2CLogger::Get().LogError(TEXT("Invalid Blueprint Editor pointer"));
            return false;
        }

        UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
        if (!FocusedGraph)
        {
            FN2CLogger::Get().LogError(TEXT("No focused graph in Blueprint Editor"));
            return false;
        }

        FString BlueprintName = TEXT("Unknown");
        FString ChangeList;
        if (UBlueprint* Blueprint = Cast<UBlueprint>(FocusedGraph->GetOuter()))
        {
            BlueprintName = Blueprint->GetName();
            ChangeList = GetBlueprintChangeListNumber(Blueprint);
            if (!ChangeList.IsEmpty())
            {
                UN2CLLMModule::Get()->SetPendingBlueprintChangeList(ChangeList);
            }
        }

        FN2CNodeCollector& Collector = FN2CNodeCollector::Get();
        if (!Collector.CollectNodesFromGraph(FocusedGraph, OutCollectedNodes))
        {
            FN2CLogger::Get().LogError(TEXT("Failed to collect nodes for save operation"));
            return false;
        }

        const UN2CSettings* Settings = GetDefault<UN2CSettings>();
        FString BasePath;
        if (Settings && !Settings->CustomTranslationOutputDirectory.Path.IsEmpty())
        {
            BasePath = Settings->CustomTranslationOutputDirectory.Path;
        }
        else
        {
            BasePath = FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Translations");
        }

        OutSafeGraphName = FPaths::MakeValidFileName(FocusedGraph->GetName());

        FString Suffix;
        if (!ChangeList.IsEmpty())
        {
            Suffix = FString::Printf(TEXT("CL%s"), *ChangeList);
        }
        if (Suffix.IsEmpty())
        {
            Suffix = FDateTime::Now().ToString(TEXT("%Y-%m-%d-%H.%M.%S"));
        }

        OutRootPath = FPaths::Combine(
            BasePath,
            FString::Printf(TEXT("%s_%s_%s"), *BlueprintName, *OutSafeGraphName, *Suffix));
        OutFlowDir = OutRootPath;

        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.CreateDirectoryTree(*OutFlowDir))
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to create flow output directory: %s"), *OutFlowDir));
            return false;
        }

        return true;
    }
}
#pragma endregion

void FN2CEditorIntegration::ExecuteCopyJsonForEditor(TWeakPtr<FBlueprintEditor> InEditor)
{
    FN2CLogger::Get().Log(TEXT("ExecuteCopyJsonForEditor called"), EN2CLogSeverity::Debug);

    // Get the editor pointer
    TSharedPtr<FBlueprintEditor> Editor = InEditor.Pin();
    if (!Editor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid Blueprint Editor pointer"));
        return;
    }
    FN2CLogger::Get().Log(TEXT("Successfully obtained Blueprint Editor pointer"), EN2CLogSeverity::Info);

    // Get focused graph
    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
    if (!FocusedGraph)
    {
        FN2CLogger::Get().LogError(TEXT("No focused graph in Blueprint Editor"));
        return;
    }

    FString GraphName = FocusedGraph->GetName();
    FString BlueprintName = TEXT("Unknown");
    if (UBlueprint* Blueprint = Cast<UBlueprint>(FocusedGraph->GetOuter()))
    {
        BlueprintName = Blueprint->GetName();
    }
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Found focused graph: %s in Blueprint: %s"), 
        *GraphName, *BlueprintName), 
        EN2CLogSeverity::Info
    );

    // Get collector instance
    FN2CNodeCollector& Collector = FN2CNodeCollector::Get();

    // Collect nodes using the specific editor
    TArray<UK2Node*> CollectedNodes;
    if (Collector.CollectNodesFromGraph(FocusedGraph, CollectedNodes))
    {
        FString Context = FString::Printf(TEXT("Collected %d nodes"), CollectedNodes.Num());
        FN2CLogger::Get().Log(TEXT("Node collection successful"), EN2CLogSeverity::Info, Context);

        // Get translator instance                                                                                                                                                                        
        FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();

        // Generate N2CStruct from collected nodes
        if (Translator.GenerateN2CStruct(CollectedNodes))
        {
            FN2CLogger::Get().Log(TEXT("Node translation successful"), EN2CLogSeverity::Info);

            // Get the Blueprint structure
            const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();

            // Validate the generated Blueprint
            if (Blueprint.IsValid())
            {
                FN2CLogger::Get().Log(TEXT("Node translation validation successful"), EN2CLogSeverity::Info);

                // Serialize to JSON with pretty printing enabled for clipboard                                                                                                                                   
                FN2CSerializer::SetPrettyPrint(true);
                FString JsonOutput = FN2CSerializer::ToJson(Blueprint);                                                                                                                                       

                // Copy JSON to clipboard if not empty                                                                                                                                                         
                if (!JsonOutput.IsEmpty())                                                                                                                                                                    
                {                                                                                                                                                                                             
                    FPlatformApplicationMisc::ClipboardCopy(*JsonOutput);

                    // Show notification
                    FNotificationInfo Info(NSLOCTEXT("NodeToCode", "BlueprintJsonCopied", "Blueprint JSON copied to clipboard"));
                    Info.bFireAndForget = true;
                    Info.FadeInDuration = 0.2f;
                    Info.FadeOutDuration = 0.5f;
                    Info.ExpireDuration = 2.0f;
                    FSlateNotificationManager::Get().AddNotification(Info);

                    FN2CLogger::Get().Log(TEXT("Blueprint JSON copied to clipboard successfully"), EN2CLogSeverity::Info);
                }
                else
                {
                    FN2CLogger::Get().LogError(TEXT("JSON serialization failed"));
                }
            }
            else
            {
                FN2CLogger::Get().LogError(TEXT("Node translation validation failed"));
            }
        }
        else
        {
            FN2CLogger::Get().LogError(TEXT("Failed to translate nodes"));
        }
    }
}

#pragma region ODS
void FN2CEditorIntegration::ExecuteSaveJson(TWeakPtr<FBlueprintEditor> InEditor)
{
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
    if (!Translator.GenerateN2CStruct(CollectedNodes))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to translate nodes for Blueprint JSON save"));
        return;
    }

    const FN2CBlueprint& Blueprint = Translator.GetN2CBlueprint();
    if (!Blueprint.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Generated Blueprint JSON data is invalid"));
        return;
    }

    FN2CSerializer::SetPrettyPrint(true);
    const FString JsonOutput = FN2CSerializer::ToJson(Blueprint);
    if (JsonOutput.IsEmpty())
    {
        FN2CLogger::Get().LogError(TEXT("Blueprint JSON serialization failed"));
        return;
    }

    const FString BlueprintJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_blueprint.json"), *SafeGraphName));
    if (!FFileHelper::SaveStringToFile(JsonOutput, *BlueprintJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save Blueprint JSON: %s"), *BlueprintJsonPath));
        return;
    }

    FNotificationInfo Info(NSLOCTEXT("NodeToCode", "BlueprintJsonSaved", "Blueprint JSON file saved"));
    Info.bFireAndForget = true;
    Info.FadeInDuration = 0.2f;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = 2.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

void FN2CEditorIntegration::ExecuteOpenSaveFolder(TWeakPtr<FBlueprintEditor> InEditor)
{
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    FPlatformProcess::ExploreFolder(*RootPath);
}

void FN2CEditorIntegration::ExecuteBp2CppUsingMCP(TWeakPtr<FBlueprintEditor> InEditor)
{
    // Check if translation is already in progress
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (LLMModule && LLMModule->GetSystemStatus() == EN2CSystemStatus::Processing)
    {
        FN2CLogger::Get().LogWarning(TEXT("Translation already in progress, please wait"));
        return;
    }

    FN2CLogger::Get().Log(TEXT("ExecuteBp2CppUsingMCP called"), EN2CLogSeverity::Debug);

    // Show the window as a tab (match common flow)
    FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);
    FN2CLogger::Get().Log(TEXT("Node to Code window shown"), EN2CLogSeverity::Debug);

    TSharedPtr<FBlueprintEditor> Editor = InEditor.Pin();
    if (!Editor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid Blueprint Editor pointer"));
        return;
    }

    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
    if (!FocusedGraph)
    {
        FN2CLogger::Get().LogError(TEXT("No focused graph in Blueprint Editor"));
        return;
    }

    FString BlueprintName = TEXT("Unknown");
    if (UBlueprint* Blueprint = Cast<UBlueprint>(FocusedGraph->GetOuter()))
    {
        BlueprintName = Blueprint->GetName();
    }

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to load N2C settings for MCP workflow"));
        return;
    }

    if (!ExecuteSaveParsedFlowFiles(InEditor))
    {
        return;
    }

    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    const FString ParsedJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_parsed.json"), *SafeGraphName));
    const FString FlowJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow.json"), *SafeGraphName));
    const FString FlowTextPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow.txt"), *SafeGraphName));

    FString FlowJson;
    FString ParsedJson;
    FString FlowText;
    FString BlueprintJson;

    if (Settings->bMcpIncludeFlowJson && !FFileHelper::LoadFileToString(FlowJson, *FlowJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to read flow JSON file: %s"), *FlowJsonPath));
        return;
    }

    if (Settings->bMcpIncludeParsedJson && !FFileHelper::LoadFileToString(ParsedJson, *ParsedJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to read parsed JSON file: %s"), *ParsedJsonPath));
        return;
    }

    if (Settings->bMcpIncludeFlowText && !FFileHelper::LoadFileToString(FlowText, *FlowTextPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to read flow text file: %s"), *FlowTextPath));
        return;
    }

    if (Settings->bMcpIncludeBlueprintJson)
    {
        FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
        if (!Translator.GenerateN2CStruct(CollectedNodes))
        {
            FN2CLogger::Get().LogError(TEXT("Failed to translate nodes for Blueprint JSON (MCP)"));
            return;
        }

        const FN2CBlueprint& Blueprint = Translator.GetN2CBlueprint();
        if (!Blueprint.IsValid())
        {
            FN2CLogger::Get().LogError(TEXT("Generated Blueprint JSON data is invalid (MCP)"));
            return;
        }

        FN2CSerializer::SetPrettyPrint(true);
        BlueprintJson = FN2CSerializer::ToJson(Blueprint);
        if (BlueprintJson.IsEmpty())
        {
            FN2CLogger::Get().LogError(TEXT("Blueprint JSON serialization failed (MCP)"));
            return;
        }
    }

    FString PromptText;
    if (!Settings->McpPromptFilePath.FilePath.IsEmpty())
    {
        if (!FFileHelper::LoadFileToString(PromptText, *Settings->McpPromptFilePath.FilePath))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to load MCP prompt file: %s"), *Settings->McpPromptFilePath.FilePath));
        }
    }

    FN2CMcpSessionRequest Request;
    Request.FlowJson = FlowJson;
    Request.ParsedJson = ParsedJson;
    Request.FlowText = FlowText;
    Request.BlueprintJson = BlueprintJson;
    Request.PromptText = PromptText;
    Request.GraphName = SafeGraphName;
    Request.BlueprintName = BlueprintName;
    Request.PayloadMode = Settings->McpPayloadMode;
    Request.ServerBaseUrl = Settings->McpServerBaseUrl;
    Request.SessionCreateEndpoint = Settings->McpSessionCreateEndpoint;

    if (Settings->McpPayloadMode == EN2CMcpPayloadMode::FilePaths)
    {
        Request.FlowFilename = FPaths::GetCleanFilename(FlowJsonPath);
        Request.ParsedFilename = FPaths::GetCleanFilename(ParsedJsonPath);
    }

    // --- LLM payload: flow text + inspect graph/structs (independent of MCP server transmit) ---
    UBlueprint* BlueprintForInspect = Cast<UBlueprint>(FocusedGraph->GetOuter());
    if (!BlueprintForInspect)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to resolve Blueprint for inspect file lookup (MCP)"));
        return;
    }
    const FString InspectPrefix = FPaths::GetCleanFilename(BlueprintForInspect->GetPathName());
    const FString McpToolDir = FPaths::Combine(FlowDir, TEXT("mcp_tool_response"));
    const FString InspectGraphPath = FPaths::Combine(McpToolDir, InspectPrefix + TEXT("_graph.txt"));
    const FString InspectStructsPath = FPaths::Combine(McpToolDir, InspectPrefix + TEXT("_structs.txt"));

    FString InspectGraphText;
    if (!FFileHelper::LoadFileToString(InspectGraphText, *InspectGraphPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Inspect graph file not found: %s. Run 'Inspect Blueprint (Aura MCP API)' first."), *InspectGraphPath));
        return;
    }

    FString InspectStructsText;
    if (!FFileHelper::LoadFileToString(InspectStructsText, *InspectStructsPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Inspect structs file not found: %s. Run 'Inspect Blueprint (Aura MCP API)' first."), *InspectStructsPath));
        return;
    }

    // FlowText is needed for LLM regardless of bMcpIncludeFlowText; load if not already populated.
    FString FlowTextForLLM = FlowText;
    if (FlowTextForLLM.IsEmpty() && !FFileHelper::LoadFileToString(FlowTextForLLM, *FlowTextPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Flow text file not found for LLM payload: %s"), *FlowTextPath));
        return;
    }

    PendingMcpContext = MakeShared<FMcpLlmContext>();
    PendingMcpContext->BlueprintName = BlueprintName;
    PendingMcpContext->PromptText = PromptText;
    PendingMcpContext->FlowText = FlowTextForLLM;
    PendingMcpContext->InspectGraphText = InspectGraphText;
    PendingMcpContext->InspectStructsText = InspectStructsText;
    PendingMcpContext->FlowDir = FlowDir;
    PendingMcpContext->GraphName = SafeGraphName;

    // Unify storage: route SaveRequestJsonToDisk/SaveTranslationToDisk into the same per-graph folder
    // so request, llm_response, and translated outputs sit together in BP_Graph_CL... directory.
    LLMModule->SetLatestTranslationPath(FlowDir);

    UN2CMcpModule::Get()->CreateSessionAsync(
        Request,
        UN2CMcpModule::FN2CMcpSessionComplete::CreateRaw(
            this,
            &FN2CEditorIntegration::OnMcpSessionComplete
        )
    );
}

void FN2CEditorIntegration::ExecuteInspectBlueprintAuraMCP(TWeakPtr<FBlueprintEditor> InEditor)
{
    TSharedPtr<FBlueprintEditor> Editor = InEditor.Pin();
    if (!Editor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid Blueprint Editor pointer"));
        return;
    }

    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
    if (!FocusedGraph)
    {
        FN2CLogger::Get().LogError(TEXT("No focused graph in Blueprint Editor"));
        return;
    }

    UBlueprint* Blueprint = Cast<UBlueprint>(FocusedGraph->GetOuter());
    if (!Blueprint)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to resolve Blueprint for inspect request"));
        return;
    }

    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to load N2C settings for inspect blueprint workflow"));
        return;
    }

    FN2CMcpInspectBlueprintRequest Request;
    Request.AssetPath = Blueprint->GetPathName();
    Request.Strands.Add(FocusedGraph->GetName());
    Request.bRefresh = true;
    Request.ServerBaseUrl = Settings->McpServerBaseUrl;
    Request.InspectBlueprintEndpoint = Settings->McpInspectBlueprintEndpoint;

    // MCP tool outputs (inspect_blueprint, meta/graph/structs) are kept under mcp_tool_response/.
    const FString McpToolDir = FPaths::Combine(FlowDir, TEXT("mcp_tool_response"));
    IFileManager::Get().MakeDirectory(*McpToolDir, /*Tree=*/true);
    const FString InspectOutputPath = FPaths::Combine(McpToolDir, FString::Printf(TEXT("%s_inspect_blueprint.json"), *SafeGraphName));

    UN2CMcpModule::Get()->InspectBlueprintAsync(
        Request,
        UN2CMcpModule::FN2CMcpInspectBlueprintComplete::CreateLambda(
            [InspectOutputPath, McpToolDir, ServerBaseUrl = Settings->McpServerBaseUrl, FileEndpoint = Settings->McpInspectBlueprintFileEndpoint]
            (bool bSuccess, const FString& ResponseBody, const FString& Error)
            {
                if (!bSuccess)
                {
                    FN2CLogger::Get().LogError(Error);
                    return;
                }

                if (!FFileHelper::SaveStringToFile(ResponseBody, *InspectOutputPath))
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save inspect blueprint response: %s"), *InspectOutputPath));
                    return;
                }

                FN2CLogger::Get().Log(
                    FString::Printf(TEXT("Inspect blueprint response saved: %s"), *InspectOutputPath),
                    EN2CLogSeverity::Info
                );

                FNotificationInfo Info(NSLOCTEXT("NodeToCode", "InspectBlueprintSaved", "Inspect blueprint response saved"));
                Info.bFireAndForget = true;
                Info.FadeInDuration = 0.2f;
                Info.FadeOutDuration = 0.5f;
                Info.ExpireDuration = 2.0f;
                FSlateNotificationManager::Get().AddNotification(Info);

                // Parse JSON and download cached files (meta/graph/structs).
                TSharedPtr<FJsonObject> JsonObject;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
                if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
                {
                    FN2CLogger::Get().LogWarning(TEXT("Inspect blueprint response is not parseable JSON; skipping file downloads."));
                    return;
                }

                static const TArray<FString> Keys = { TEXT("meta_path"), TEXT("graph_path"), TEXT("structs_path") };
                for (const FString& Key : Keys)
                {
                    FString RemotePath;
                    if (!JsonObject->TryGetStringField(Key, RemotePath) || RemotePath.IsEmpty())
                    {
                        continue;
                    }

                    const FString LocalFilename = FPaths::GetCleanFilename(RemotePath);
                    const FString LocalPath = FPaths::Combine(McpToolDir, LocalFilename);

                    UN2CMcpModule::Get()->DownloadInspectFileAsync(
                        ServerBaseUrl,
                        FileEndpoint,
                        RemotePath,
                        UN2CMcpModule::FN2CMcpInspectBlueprintComplete::CreateLambda(
                            [LocalPath](bool bDownloaded, const FString& Body, const FString& DlError)
                            {
                                if (!bDownloaded)
                                {
                                    FN2CLogger::Get().LogError(DlError);
                                    return;
                                }

                                if (!FFileHelper::SaveStringToFile(Body, *LocalPath))
                                {
                                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save inspect file: %s"), *LocalPath));
                                    return;
                                }

                                FN2CLogger::Get().Log(
                                    FString::Printf(TEXT("Inspect file saved: %s"), *LocalPath),
                                    EN2CLogSeverity::Info
                                );
                            }
                        )
                    );
                }
            }
        )
    );
}

bool FN2CEditorIntegration::ExecuteSaveParsedFlowFiles(TWeakPtr<FBlueprintEditor> InEditor)
{
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return false;
    }

    FN2CFlowData FlowData;
    FString FlowDataError;
    if (!FN2CFlowBuilder::BuildFlowDataFromNodes(CollectedNodes, FlowData, FlowDataError))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build flow data: %s"), *FlowDataError));
        return false;
    }

    FString ParsedJson;
    FString ParsedJsonError;
    if (!FN2CParsedDumpBuilder::BuildParsedJsonFromNodes(CollectedNodes, ParsedJson, ParsedJsonError, &FlowData.GuidAlias))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build parsed JSON: %s"), *ParsedJsonError));
        return false;
    }

    FString FlowJson;
    FString FlowJsonError;
    if (!FN2CFlowBuilder::BuildFlowJsonFromNodes(CollectedNodes, FlowJson, FlowJsonError))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build flow JSON: %s"), *FlowJsonError));
        return false;
    }

    FString FlowTextV1;
    FString FlowTextErrorV1;
    if (!FN2CFlowBuilder::BuildFlowTextFromNodes(CollectedNodes, FlowTextV1, FlowTextErrorV1))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build flow text v1: %s"), *FlowTextErrorV1));
        return false;
    }

    FString FlowTextV2;
    FString FlowTextErrorV2;
    if (!FN2CFlowBuilder_01::BuildFlowTextFromNodes_01(CollectedNodes, FlowTextV2, FlowTextErrorV2))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build flow text v2: %s"), *FlowTextErrorV2));
        return false;
    }

    const FString ParsedJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_parsed.json"), *SafeGraphName));
    const FString FlowJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow.json"), *SafeGraphName));
    const FString FlowTextPathV1 = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow_v1.txt"), *SafeGraphName));
    const FString FlowTextPathV2 = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow_v2.txt"), *SafeGraphName));
    const FString FlowTextPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow.txt"), *SafeGraphName));
    if (!FFileHelper::SaveStringToFile(ParsedJson, *ParsedJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save parsed JSON: %s"), *ParsedJsonPath));
        return false;
    }
    if (!FFileHelper::SaveStringToFile(FlowJson, *FlowJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save flow JSON: %s"), *FlowJsonPath));
        return false;
    }
    if (!FFileHelper::SaveStringToFile(FlowTextV1, *FlowTextPathV1))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save flow text v1: %s"), *FlowTextPathV1));
        return false;
    }
    if (!FFileHelper::SaveStringToFile(FlowTextV2, *FlowTextPathV2))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save flow text v2: %s"), *FlowTextPathV2));
        return false;
    }
    if (!FFileHelper::SaveStringToFile(FlowTextV1, *FlowTextPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save flow text: %s"), *FlowTextPath));
        return false;
    }
    if (!SaveMcpGraphTextFile(CollectedNodes, SafeGraphName, FlowDir))
    {
        return false;
    }

    FNotificationInfo Info(NSLOCTEXT("NodeToCode", "ParsedFlowSaved", "Parsed/Flow files saved"));
    Info.bFireAndForget = true;
    Info.FadeInDuration = 0.2f;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = 2.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return true;
}

void FN2CEditorIntegration::ExecuteSaveMcpGraphText(TWeakPtr<FBlueprintEditor> InEditor)
{
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    if (!SaveMcpGraphTextFile(CollectedNodes, SafeGraphName, FlowDir))
    {
        return;
    }

    FNotificationInfo Info(NSLOCTEXT("NodeToCode", "McpGraphTextSaved", "MCP graph text file saved"));
    Info.bFireAndForget = true;
    Info.FadeInDuration = 0.2f;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = 2.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

bool FN2CEditorIntegration::SaveMcpGraphTextFile(const TArray<UK2Node*>& CollectedNodes, const FString& SafeGraphName, const FString& FlowDir) const
{
    FString GraphText;
    FString GraphTextError;
    if (!FN2CMcpGraphTextBuilder::BuildGraphTextFromNodes(CollectedNodes, GraphText, GraphTextError))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build MCP graph text: %s"), *GraphTextError));
        return false;
    }

    const FString GraphTextPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_graph.txt"), *SafeGraphName));
    if (!FFileHelper::SaveStringToFile(GraphText, *GraphTextPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save MCP graph text: %s"), *GraphTextPath));
        return false;
    }

    return true;
}

void FN2CEditorIntegration::ExecuteSaveParsedJson(TWeakPtr<FBlueprintEditor> InEditor)
{
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    FString ParsedJson;
    FString ParsedJsonError;
    if (!FN2CParsedDumpBuilder::BuildParsedJsonFromNodes(CollectedNodes, ParsedJson, ParsedJsonError))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build parsed JSON: %s"), *ParsedJsonError));
        return;
    }

    const FString ParsedJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_parsed.json"), *SafeGraphName));
    if (!FFileHelper::SaveStringToFile(ParsedJson, *ParsedJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save parsed JSON: %s"), *ParsedJsonPath));
        return;
    }

    FNotificationInfo Info(NSLOCTEXT("NodeToCode", "ParsedSaved", "Parsed JSON file saved"));
    Info.bFireAndForget = true;
    Info.FadeInDuration = 0.2f;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = 2.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

void FN2CEditorIntegration::ExecuteSaveFlowJson(TWeakPtr<FBlueprintEditor> InEditor)
{
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    FString FlowJson;
    FString FlowJsonError;
    if (!FN2CFlowBuilder::BuildFlowJsonFromNodes(CollectedNodes, FlowJson, FlowJsonError))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build flow JSON: %s"), *FlowJsonError));
        return;
    }

    const FString FlowJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow.json"), *SafeGraphName));
    if (!FFileHelper::SaveStringToFile(FlowJson, *FlowJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save flow JSON: %s"), *FlowJsonPath));
        return;
    }

    FNotificationInfo Info(NSLOCTEXT("NodeToCode", "FlowJsonSaved", "Flow JSON file saved"));
    Info.bFireAndForget = true;
    Info.FadeInDuration = 0.2f;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = 2.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

void FN2CEditorIntegration::ExecuteSaveFlowText(TWeakPtr<FBlueprintEditor> InEditor)
{
     // v1 (기존) + v2 (개선) 비교 모드
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    // v1: 기존 버전
    FString FlowTextV1;
    FString FlowTextErrorV1;
    if (!FN2CFlowBuilder::BuildFlowTextFromNodes(CollectedNodes, FlowTextV1, FlowTextErrorV1))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build flow text v1: %s"), *FlowTextErrorV1));
        return;
    }

    // v2: 개선 버전 (스택 기반 비재귀)
    FString FlowTextV2;
    FString FlowTextErrorV2;
    if (!FN2CFlowBuilder_01::BuildFlowTextFromNodes_01(CollectedNodes, FlowTextV2, FlowTextErrorV2))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to build flow text v2: %s"), *FlowTextErrorV2));
        return;
    }

    // v1 저장
    const FString FlowTextPathV1 = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow_v1.txt"), *SafeGraphName));
    if (!FFileHelper::SaveStringToFile(FlowTextV1, *FlowTextPathV1))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save flow text v1: %s"), *FlowTextPathV1));
        return;
    }

    // v2 저장
    const FString FlowTextPathV2 = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow_v2.txt"), *SafeGraphName));
    if (!FFileHelper::SaveStringToFile(FlowTextV2, *FlowTextPathV2))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save flow text v2: %s"), *FlowTextPathV2));
        return;
    }

    // 결과 비교 로그
    const bool bMatch = (FlowTextV1 == FlowTextV2);
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Flow text v1 vs v2 match: %s"), bMatch ? TEXT("YES") : TEXT("NO")),
        EN2CLogSeverity::Info
    );

    FNotificationInfo Info(FText::Format(
        NSLOCTEXT("NodeToCode", "FlowTextSavedV1V2", "Flow text v1/v2 saved (match: {0})"),
        bMatch ? FText::FromString(TEXT("YES")) : FText::FromString(TEXT("NO"))));
    Info.bFireAndForget = true;
    Info.FadeInDuration = 0.2f;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = 2.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

void FN2CEditorIntegration::ExecuteCopyFlowText(TWeakPtr<FBlueprintEditor> InEditor)
{
    // Flow 텍스트를 클립보드에 복사하는 툴바 액션
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    const FString FlowTextPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow.txt"), *SafeGraphName));

    FString FlowText;
    if (!FPaths::FileExists(FlowTextPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Flow text file not found: %s"), *FlowTextPath));
        return;
    }
    if (!FFileHelper::LoadFileToString(FlowText, *FlowTextPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to read flow text file: %s"), *FlowTextPath));
        return;
    }

    if (!FlowText.IsEmpty())
    {
        FPlatformApplicationMisc::ClipboardCopy(*FlowText);

        FNotificationInfo Info(NSLOCTEXT("NodeToCode", "FlowTextCopied", "Flow text copied to clipboard"));
        Info.bFireAndForget = true;
        Info.FadeInDuration = 0.2f;
        Info.FadeOutDuration = 0.5f;
        Info.ExpireDuration = 2.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}

void FN2CEditorIntegration::ExecuteCopyFlowJson(TWeakPtr<FBlueprintEditor> InEditor)
{
    // Flow JSON을 클립보드에 복사하는 툴바 액션
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    const FString FlowJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_flow.json"), *SafeGraphName));

    FString FlowJson;
    if (!FPaths::FileExists(FlowJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Flow JSON file not found: %s"), *FlowJsonPath));
        return;
    }
    if (!FFileHelper::LoadFileToString(FlowJson, *FlowJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to read flow JSON file: %s"), *FlowJsonPath));
        return;
    }

    if (!FlowJson.IsEmpty())
    {
        FPlatformApplicationMisc::ClipboardCopy(*FlowJson);

        FNotificationInfo Info(NSLOCTEXT("NodeToCode", "FlowJsonCopied", "Flow JSON copied to clipboard"));
        Info.bFireAndForget = true;
        Info.FadeInDuration = 0.2f;
        Info.FadeOutDuration = 0.5f;
        Info.ExpireDuration = 2.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}

void FN2CEditorIntegration::ExecuteCopyParsedJson(TWeakPtr<FBlueprintEditor> InEditor)
{
    // Parsed JSON을 클립보드에 복사하는 툴바 액션
    TArray<UK2Node*> CollectedNodes;
    FString SafeGraphName;
    FString RootPath;
    FString FlowDir;
    if (!PrepareSaveContext(InEditor, CollectedNodes, SafeGraphName, RootPath, FlowDir))
    {
        return;
    }

    const FString ParsedJsonPath = FPaths::Combine(FlowDir, FString::Printf(TEXT("%s_parsed.json"), *SafeGraphName));

    FString ParsedJson;
    if (!FPaths::FileExists(ParsedJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Parsed JSON file not found: %s"), *ParsedJsonPath));
        return;
    }
    if (!FFileHelper::LoadFileToString(ParsedJson, *ParsedJsonPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to read parsed JSON file: %s"), *ParsedJsonPath));
        return;
    }

    if (!ParsedJson.IsEmpty())
    {
        FPlatformApplicationMisc::ClipboardCopy(*ParsedJson);

        FNotificationInfo Info(NSLOCTEXT("NodeToCode", "ParsedJsonCopied", "Parsed JSON copied to clipboard"));
        Info.bFireAndForget = true;
        Info.FadeInDuration = 0.2f;
        Info.FadeOutDuration = 0.5f;
        Info.ExpireDuration = 2.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}

// ExecuteBp2CppUsingMCP에서 MCP 세션이 생성된 후 호출되는 콜백 함수
void FN2CEditorIntegration::OnMcpSessionComplete(bool bSuccess, const FString& SessionId, const FString& Error)
{
    if (!bSuccess)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP session creation failed: %s"), *Error));
        return;
    }

    const FString BlueprintName = PendingMcpContext ? PendingMcpContext->BlueprintName : TEXT("Unknown");
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("MCP session created for Blueprint %s: %s"), *BlueprintName, *SessionId),
        EN2CLogSeverity::Info
    );

    SendMcpRequestToLLM(SessionId);
}

// MCP 세션이 생성된 후 LLM에 요청을 보내는 함수
void FN2CEditorIntegration::SendMcpRequestToLLM(const FString& SessionId)
{
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (!LLMModule)
    {
        FN2CLogger::Get().LogError(TEXT("LLM Module not available for MCP request"));
        return;
    }

    if (!LLMModule->Initialize())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to initialize LLM Module for MCP request"));
        return;
    }

    // Build LLM user-message payload (separate from MCP server transmission).
    TSharedRef<FJsonObject> PayloadObj = MakeShared<FJsonObject>();
    PayloadObj->SetStringField(TEXT("session_id"), SessionId);
    if (PendingMcpContext.IsValid())
    {
        // Provide identity fields so the response schema can echo blueprint_name back
        // (consumed by N2CResponseParserBase / SaveTranslationToDisk fallback).
        if (!PendingMcpContext->BlueprintName.IsEmpty())
        {
            PayloadObj->SetStringField(TEXT("blueprint_name"), PendingMcpContext->BlueprintName);
        }
        if (!PendingMcpContext->GraphName.IsEmpty())
        {
            PayloadObj->SetStringField(TEXT("graph_name"), PendingMcpContext->GraphName);
        }
        if (!PendingMcpContext->FlowText.IsEmpty())
        {
            PayloadObj->SetStringField(TEXT("flow_text"), PendingMcpContext->FlowText);
        }
        if (!PendingMcpContext->InspectGraphText.IsEmpty())
        {
            PayloadObj->SetStringField(TEXT("inspect_graph"), PendingMcpContext->InspectGraphText);
        }
        if (!PendingMcpContext->InspectStructsText.IsEmpty())
        {
            PayloadObj->SetStringField(TEXT("inspect_structs"), PendingMcpContext->InspectStructsText);
        }
    }

    FString McpPayload;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&McpPayload);
    FJsonSerializer::Serialize(PayloadObj, Writer);

    const FString PromptText = PendingMcpContext ? PendingMcpContext->PromptText : FString();

    TScriptInterface<IN2CLLMService> ActiveService = LLMModule->GetActiveService();
    if (!ActiveService.GetInterface())
    {
        FN2CLogger::Get().LogError(TEXT("No active LLM service for MCP request"));
        return;
    }

    // Show in-flight progress notification (cleared in OnMcpLlmResponse).
    {
        const FText ProgressText = FText::Format(
            NSLOCTEXT("NodeToCode", "LlmInFlight", "Translating Blueprint to C++ ({0})..."),
            FText::FromString(PendingMcpContext.IsValid() ? PendingMcpContext->GraphName : TEXT("")));

        FNotificationInfo Info(ProgressText);
        Info.bFireAndForget = false;
        Info.bUseThrobber = true;
        Info.bUseSuccessFailIcons = true;
        Info.FadeInDuration = 0.2f;
        Info.FadeOutDuration = 0.5f;
        Info.ExpireDuration = 0.0f;

        if (PendingLlmProgressNotification.IsValid())
        {
            PendingLlmProgressNotification->SetCompletionState(SNotificationItem::CS_None);
            PendingLlmProgressNotification->ExpireAndFadeout();
            PendingLlmProgressNotification.Reset();
        }
        PendingLlmProgressNotification = FSlateNotificationManager::Get().AddNotification(Info);
        if (PendingLlmProgressNotification.IsValid())
        {
            PendingLlmProgressNotification->SetCompletionState(SNotificationItem::CS_Pending);
        }
    }

    ActiveService->SendRequest(
        McpPayload,
        PromptText,
        FOnLLMResponseReceived::CreateRaw(
            this,
            &FN2CEditorIntegration::OnMcpLlmResponse
        )
    );
}

// LLM으로부터 MCP 관련 응답을 받는 콜백 함수
void FN2CEditorIntegration::OnMcpLlmResponse(const FString& Response)
{
    // Finalize the in-flight progress notification.
    if (PendingLlmProgressNotification.IsValid())
    {
        const bool bHasResponse = !Response.IsEmpty();
        PendingLlmProgressNotification->SetText(
            bHasResponse
                ? NSLOCTEXT("NodeToCode", "LlmDone", "Blueprint to C++ translation complete")
                : NSLOCTEXT("NodeToCode", "LlmFailed", "Blueprint to C++ translation failed"));
        PendingLlmProgressNotification->SetCompletionState(
            bHasResponse ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
        PendingLlmProgressNotification->ExpireAndFadeout();
        PendingLlmProgressNotification.Reset();
    }

    // (a) Save raw response to disk next to flow/parsed outputs.
    if (PendingMcpContext.IsValid() && !PendingMcpContext->FlowDir.IsEmpty() && !PendingMcpContext->GraphName.IsEmpty())
    {
        // Mirror SaveTranslationToDisk: store all LLM artifacts under llm_response/.
        const FString LlmResponseDir = FPaths::Combine(PendingMcpContext->FlowDir, TEXT("llm_response"));
        IFileManager::Get().MakeDirectory(*LlmResponseDir, /*Tree=*/true);
        const FString ResponsePath = FPaths::Combine(
            LlmResponseDir,
            FString::Printf(TEXT("%s_llm_response.txt"), *PendingMcpContext->GraphName));

        if (FFileHelper::SaveStringToFile(Response, *ResponsePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Saved MCP LLM response: %s"), *ResponsePath),
                EN2CLogSeverity::Info);
        }
        else
        {
            FN2CLogger::Get().LogError(
                FString::Printf(TEXT("Failed to save MCP LLM response: %s"), *ResponsePath));
        }
    }

    // (b) Parse via active service's response parser and broadcast (mirrors UN2CLLMModule::ProcessN2CJson flow).
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (!LLMModule)
    {
        FN2CLogger::Get().LogError(TEXT("LLM Module unavailable while processing MCP response"));
        return;
    }

    FN2CTranslationResponse TranslationResponse;

    TScriptInterface<IN2CLLMService> ActiveService = LLMModule->GetActiveService();
    if (!ActiveService.GetInterface())
    {
        FN2CLogger::Get().LogError(TEXT("No active LLM service for MCP response parsing"));
        LLMModule->OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
        return;
    }

    UN2CResponseParserBase* Parser = ActiveService->GetResponseParser();
    if (!Parser)
    {
        FN2CLogger::Get().LogError(TEXT("No response parser available for MCP response"));
        LLMModule->OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
        return;
    }

    if (Parser->ParseLLMResponse(Response, TranslationResponse))
    {
        const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
        if (LLMModule->SaveTranslationToDisk(TranslationResponse, Blueprint))
        {
            FN2CLogger::Get().Log(TEXT("Successfully saved MCP translation to disk"), EN2CLogSeverity::Info);
        }

        LLMModule->OnTranslationResponseReceived.Broadcast(TranslationResponse, true);
        FN2CLogger::Get().Log(TEXT("Successfully parsed MCP LLM response"), EN2CLogSeverity::Info);
    }
    else
    {
        FN2CLogger::Get().LogError(TEXT("Failed to parse MCP LLM response"));
        LLMModule->OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
    }
}
#pragma endregion

void FN2CEditorIntegration::Initialize()
{
    // Register commands
    FN2CToolbarCommand::Register();

    // Register tab spawner
    SN2CEditorWindow::RegisterTabSpawner();

    // Subscribe to asset editor opened events
    if (GEditor)
    {
        UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (ensure(AssetEditorSubsystem))
        {
            AssetEditorSubsystem->OnAssetEditorOpened().AddLambda([this](UObject* Asset)
            {
                if (IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Asset, false))
                {
                    HandleAssetEditorOpened(Asset, EditorInstance);
                }
            });
            FN2CLogger::Get().Log(TEXT("N2C Editor Integration: Subscribed to OnAssetEditorOpened via AssetEditorSubsystem"), EN2CLogSeverity::Info);
        }
    }

    FN2CLogger::Get().Log(TEXT("N2C Editor Integration initialized"), EN2CLogSeverity::Info);
}

void FN2CEditorIntegration::Shutdown()
{
    // Unregister tab spawner
    SN2CEditorWindow::UnregisterTabSpawner();

    // Clear editor command lists
    EditorCommandLists.Empty();

    // Unsubscribe from asset editor events
    if (GEditor)
    {
        UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (AssetEditorSubsystem)
        {
            AssetEditorSubsystem->OnAssetEditorOpened().RemoveAll(this);
        }
    }

    FN2CLogger::Get().Log(TEXT("N2C Editor Integration shutdown"), EN2CLogSeverity::Info);
}


TSharedPtr<FBlueprintEditor> FN2CEditorIntegration::GetBlueprintEditorFromTab() const
{
    // Mark as deprecated
    FN2CLogger::Get().LogWarning(TEXT("GetBlueprintEditorFromTab is deprecated - editors should be accessed directly"));
    return nullptr;
}

void FN2CEditorIntegration::HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance)
{
    if (!Asset || !EditorInstance)
    {
        return;
    }

    // Check if the asset is a Blueprint or a child class of Blueprint
    UBlueprint* OpenedBlueprint = Cast<UBlueprint>(Asset);
    if (!OpenedBlueprint)
    {
        return; // Not a Blueprint, so ignore
    }

    // Convert the EditorInstance to the correct type
    FBlueprintEditor* BlueprintEditorPtr = static_cast<FBlueprintEditor*>(EditorInstance);
    if (!BlueprintEditorPtr)
    {
        return;
    }

    // Convert to SharedPtr so it matches our existing RegisterToolbarForEditor() signature
    TSharedPtr<FBlueprintEditor> BlueprintEditorShared = StaticCastSharedRef<FBlueprintEditor>(BlueprintEditorPtr->AsShared());
    if (BlueprintEditorShared.IsValid())
    {
        // Check if we already have this editor registered
        TWeakPtr<FBlueprintEditor> WeakEditor(BlueprintEditorShared);
        if (!EditorCommandLists.Contains(WeakEditor))
        {
            FString BlueprintPath = OpenedBlueprint->GetPathName();
            
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Registering toolbar for Blueprint Editor: %s"), 
                *BlueprintPath), 
                EN2CLogSeverity::Info
            );
            
            RegisterToolbarForEditor(BlueprintEditorShared);
        }
        else
        {
            FN2CLogger::Get().Log(
                TEXT("Blueprint Editor already registered"), 
                EN2CLogSeverity::Debug
            );
        }
    }
}


void FN2CEditorIntegration::RegisterToolbarForEditor(TSharedPtr<FBlueprintEditor> InEditor)
{
    FN2CLogger::Get().Log(TEXT("Starting toolbar registration for editor"), EN2CLogSeverity::Info);

    if (!InEditor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid editor pointer provided to RegisterToolbarForEditor"));
        return;
    }

    // Get Blueprint name for context
    FString BlueprintName = TEXT("Unknown");
    if (InEditor->GetBlueprintObj())
    {
        BlueprintName = InEditor->GetBlueprintObj()->GetName();
    }

    // Check if we already have a command list for this editor
    TWeakPtr<FBlueprintEditor> WeakEditor(InEditor);
    if (EditorCommandLists.Contains(WeakEditor))
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Editor already has command list registered: %s"), *BlueprintName),
            EN2CLogSeverity::Warning
        );
        return;
    }

    // Create command list for this editor
    TSharedPtr<FUICommandList> CommandList = MakeShareable(new FUICommandList);
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Created command list for Blueprint: %s"), *BlueprintName), 
        EN2CLogSeverity::Info
    );

    // Map the Open Window command
    CommandList->MapAction(
        FN2CToolbarCommand::Get().OpenWindowCommand,
        FExecuteAction::CreateLambda([this]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);
            FN2CLogger::Get().Log(TEXT("Node to Code window opened"), EN2CLogSeverity::Info);
        }),
        FCanExecuteAction::CreateLambda([]() { return true; })
    );

    // Map the Collect Nodes command
    CommandList->MapAction(
        FN2CToolbarCommand::Get().CollectNodesCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Node to Code collection triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteCollectNodesForEditor(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );
    
    #pragma region ODS
    CommandList->MapAction(
        FN2CToolbarCommand::Get().SaveBlueprintJsonCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Save Blueprint JSON triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteSaveJson(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );
    #pragma endregion

    // Map the Copy JSON command
    CommandList->MapAction(
        FN2CToolbarCommand::Get().CopyJsonCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Copy Blueprint JSON triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteCopyJsonForEditor(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    #pragma region ODS
    CommandList->MapAction(
        FN2CToolbarCommand::Get().SaveParsedFlowFilesCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Save Parsed/Flow Files triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteSaveParsedFlowFiles(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    CommandList->MapAction(
        FN2CToolbarCommand::Get().SaveMcpGraphTextCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Save MCP Graph Text triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteSaveMcpGraphText(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    CommandList->MapAction(
        FN2CToolbarCommand::Get().InspectBlueprintAuraMcpCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Inspect Blueprint (MCP API) triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteInspectBlueprintAuraMCP(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    // GUID :: Simple ID 알리아스가 생겨서 개별 파일로는 의미가 없어짐.
    // CommandList->MapAction(
    //     FN2CToolbarCommand::Get().SaveParsedJsonCommand,
    //     FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
    //     {
    //         FN2CLogger::Get().Log(
    //             FString::Printf(TEXT("Save Parsed Json triggered for Blueprint: %s"), *BlueprintName),
    //             EN2CLogSeverity::Info
    //         );
    //         ExecuteSaveParsedJson(WeakEditor);
    //     }),
    //     FCanExecuteAction::CreateLambda([WeakEditor]()
    //     {
    //         TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
    //         if (!Editor.IsValid())
    //         {
    //             return false;
    //         }
    //         return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
    //     })
    // );

    // CommandList->MapAction(
    //     FN2CToolbarCommand::Get().SaveFlowJsonCommand,
    //     FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
    //     {
    //         FN2CLogger::Get().Log(
    //             FString::Printf(TEXT("Save Flow Json triggered for Blueprint: %s"), *BlueprintName),
    //             EN2CLogSeverity::Info
    //         );
    //         ExecuteSaveFlowJson(WeakEditor);
    //     }),
    //     FCanExecuteAction::CreateLambda([WeakEditor]()
    //     {
    //         TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
    //         if (!Editor.IsValid())
    //         {
    //             return false;
    //         }
    //         return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
    //     })
    // );

    // CommandList->MapAction(
    //     FN2CToolbarCommand::Get().SaveFlowTextCommand,
    //     FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
    //     {
    //         FN2CLogger::Get().Log(
    //             FString::Printf(TEXT("Save Flow Text triggered for Blueprint: %s"), *BlueprintName),
    //             EN2CLogSeverity::Info
    //         );
    //         ExecuteSaveFlowText(WeakEditor);
    //     }),
    //     FCanExecuteAction::CreateLambda([WeakEditor]()
    //     {
    //         TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
    //         if (!Editor.IsValid())
    //         {
    //             return false;
    //         }
    //         return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
    //     })
    // );

    CommandList->MapAction(
        FN2CToolbarCommand::Get().CopyFlowJsonCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Copy Flow Json triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteCopyFlowJson(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    CommandList->MapAction(
        FN2CToolbarCommand::Get().CopyFlowTextCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Copy Flow Text triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteCopyFlowText(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    CommandList->MapAction(
        FN2CToolbarCommand::Get().OpenSaveFolderCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Open Save Folder triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteOpenSaveFolder(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    CommandList->MapAction(
        FN2CToolbarCommand::Get().Bp2CppMcpCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("BP --> CPP (MCP) triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteBp2CppUsingMCP(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    CommandList->MapAction(
        FN2CToolbarCommand::Get().CopyParsedJsonCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Copy Parsed Json triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteCopyParsedJson(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );
    #pragma endregion
    
    // Store in our map
    EditorCommandLists.Add(WeakEditor, CommandList);
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Added command list to map for Blueprint: %s"), *BlueprintName),
        EN2CLogSeverity::Info
    );

    // Add toolbar extension
    TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
    ToolbarExtender->AddToolBarExtension(
        "Asset",
        EExtensionHook::After,
        CommandList,
        FToolBarExtensionDelegate::CreateLambda([CommandList](FToolBarBuilder& Builder)
        {
            Builder.BeginSection("NodeToCode");
            
            // Add dropdown button
            Builder.AddComboButton(
                FUIAction(),
                FOnGetContent::CreateLambda([CommandList]() -> TSharedRef<SWidget>
                {
                    FMenuBuilder MenuBuilder(true, CommandList);
                    
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().OpenWindowCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().CollectNodesCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().CopyJsonCommand);
                    #pragma region ODS
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().SaveBlueprintJsonCommand);
                    MenuBuilder.AddMenuSeparator();
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().SaveParsedFlowFilesCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().SaveMcpGraphTextCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().InspectBlueprintAuraMcpCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().OpenSaveFolderCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().Bp2CppMcpCommand);
                    MenuBuilder.AddMenuSeparator();
                    MenuBuilder.AddMenuSeparator();
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().CopyParsedJsonCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().CopyFlowJsonCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().CopyFlowTextCommand);
                    #pragma endregion

                    return MenuBuilder.MakeWidget();
                }),
                NSLOCTEXT("NodeToCode", "NodeToCodeActions", "Node to Code"),
                NSLOCTEXT("NodeToCode", "NodeToCodeTooltip", "Node to Code Actions"),
                FSlateIcon("NodeToCodeStyle", "NodeToCode.ToolbarButton")
            );
            
            Builder.EndSection();
        })
    );

    // Add the extender to this specific editor
    InEditor->AddToolbarExtender(ToolbarExtender);

    InEditor->RegenerateMenusAndToolbars();
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Completed toolbar registration for Blueprint: %s"), *BlueprintName), 
        EN2CLogSeverity::Info
    );
}

TArray<FName> FN2CEditorIntegration::GetAvailableThemes(EN2CCodeLanguage Language) const
{
    TArray<FName> ThemeNames;
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            Settings->CPPThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::Python:
            Settings->PythonThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::JavaScript:
            Settings->JavaScriptThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::CSharp:
            Settings->CSharpThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::Swift:
            Settings->SwiftThemes.Themes.GetKeys(ThemeNames);
            break;
    }
    
    return ThemeNames;
}

FName FN2CEditorIntegration::GetDefaultTheme(EN2CCodeLanguage Language) const
{
    return TEXT("Unreal Engine");
}

void FN2CEditorIntegration::ExecuteCollectNodesForEditor(TWeakPtr<FBlueprintEditor> InEditor)
{
    // Check if translation is already in progress
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (LLMModule && LLMModule->GetSystemStatus() == EN2CSystemStatus::Processing)
    {
        FN2CLogger::Get().LogWarning(TEXT("Translation already in progress, please wait"));
        return;
    }

    FN2CLogger::Get().Log(TEXT("ExecuteCollectNodesForEditor called"), EN2CLogSeverity::Debug);

    // Show the window as a tab
    FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);
    FN2CLogger::Get().Log(TEXT("Node to Code window shown"), EN2CLogSeverity::Debug);

    // Get the editor pointer
    TSharedPtr<FBlueprintEditor> Editor = InEditor.Pin();
    if (!Editor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid Blueprint Editor pointer"));
        return;
    }
    FN2CLogger::Get().Log(TEXT("Successfully obtained Blueprint Editor pointer"), EN2CLogSeverity::Info);

    // Get focused graph
    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
    if (!FocusedGraph)
    {
        FN2CLogger::Get().LogError(TEXT("No focused graph in Blueprint Editor"));
        return;
    }

    FString GraphName = FocusedGraph->GetName();
    FString BlueprintName = TEXT("Unknown");
    if (UBlueprint* Blueprint = Cast<UBlueprint>(FocusedGraph->GetOuter()))
    {
        BlueprintName = Blueprint->GetName();
    }
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Found focused graph: %s in Blueprint: %s"), 
        *GraphName, *BlueprintName), 
        EN2CLogSeverity::Info
    );

    // Get collector instance
    FN2CNodeCollector& Collector = FN2CNodeCollector::Get();
    
    // Collect nodes using the specific editor
    TArray<UK2Node*> CollectedNodes;
    if (Collector.CollectNodesFromGraph(FocusedGraph, CollectedNodes))
    {
        FString Context = FString::Printf(TEXT("Collected %d nodes"), CollectedNodes.Num());
        FN2CLogger::Get().Log(TEXT("Node collection successful"), EN2CLogSeverity::Info, Context);
        
        // Get translator instance                                                                                                                                                                        
        FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();

        // Generate N2CStruct from collected nodes
        if (Translator.GenerateN2CStruct(CollectedNodes))
        {
            FN2CLogger::Get().Log(TEXT("Node translation successful"), EN2CLogSeverity::Info);

            // Get the Blueprint structure
            const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
            
            // Validate the generated Blueprint
            if (Blueprint.IsValid())
            {
                FN2CLogger::Get().Log(TEXT("Node translation validation successful"), EN2CLogSeverity::Info);

                // Serialize to JSON with pretty printing enabled                                                                                                                                             
                FN2CSerializer::SetPrettyPrint(false);
                FString JsonOutput = FN2CSerializer::ToJson(Blueprint);                                                                                                                                       
                                                                                                                                                                                                           
                // Log the JSON output                                                                                                                                                                        
                if (!JsonOutput.IsEmpty())                                                                                                                                                                    
                {                                                                                                                                                                                             
                    FN2CLogger::Get().Log(TEXT("JSON Output:"), EN2CLogSeverity::Debug);                                                                                                                       
                    FN2CLogger::Get().Log(JsonOutput, EN2CLogSeverity::Debug);

                    if (LLMModule->Initialize())
                    {
                        // Send JSON to LLM service                                                                                                                                                        
                        LLMModule->ProcessN2CJson(JsonOutput, FOnLLMResponseReceived::CreateLambda(
                            [](const FString& Response)                                                                                                                                                   
                            {                                                                                                                                                                             
                                FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Response:\n\n%s"), *Response), EN2CLogSeverity::Debug);                                                                                                      
                            
                                // Create translation response struct
                                FN2CTranslationResponse TranslationResponse;
                            
                                // Get active service's response parser
                                TScriptInterface<IN2CLLMService> ActiveService = UN2CLLMModule::Get()->GetActiveService();
                                if (ActiveService.GetInterface())
                                {
                                    UN2CResponseParserBase* Parser = ActiveService->GetResponseParser();
                                    if (Parser)
                                    {
                                        if (Parser->ParseLLMResponse(Response, TranslationResponse))
                                        {
                                            // Log successful parsing
                                            FN2CLogger::Get().Log(TEXT("Successfully parsed LLM response"), EN2CLogSeverity::Info);
                                        }
                                        else
                                        {
                                            FN2CLogger::Get().LogError(TEXT("Failed to parse LLM response"));
                                        }
                                    }
                                    else
                                    {
                                        FN2CLogger::Get().LogError(TEXT("No response parser available"));
                                    }
                                }
                                else
                                {
                                    FN2CLogger::Get().LogError(TEXT("No active LLM service"));
                                }
                            }));
                    }
                    else
                    {
                        FN2CLogger::Get().LogError(TEXT("Failed to initialize LLM Module"));
                    }
                }
                else
                {
                    FN2CLogger::Get().LogError(TEXT("JSON serialization failed"));
                }
            }
            else
            {
                FN2CLogger::Get().LogError(TEXT("Node translation validation failed"));
            }
        }
        else
        {
            FN2CLogger::Get().LogError(TEXT("Failed to translate nodes"));
        }
    }
}
