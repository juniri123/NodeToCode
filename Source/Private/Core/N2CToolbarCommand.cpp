// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CToolbarCommand.h"

#include "Styling/AppStyle.h"
#include "Framework/Commands/UICommandList.h"
#include "Utils/N2CLogger.h"

#define LOCTEXT_NAMESPACE "NodeToCode"

const FName FN2CToolbarCommand::CommandName_Open = TEXT("NodeToCode_OpenWindow");
const FName FN2CToolbarCommand::CommandName_Collect = TEXT("NodeToCode_CollectNodes");
const FName FN2CToolbarCommand::CommandName_CopyJson = TEXT("NodeToCode_CopyJson");
const FText FN2CToolbarCommand::CommandLabel_Open = NSLOCTEXT("NodeToCode", "OpenWindow", "Open Node to Code");
const FText FN2CToolbarCommand::CommandLabel_Collect = NSLOCTEXT("NodeToCode", "CollectNodes", "Collect and Translate Nodes");
const FText FN2CToolbarCommand::CommandLabel_CopyJson = NSLOCTEXT("NodeToCode", "CopyJson", "Copy Blueprint JSON");
const FText FN2CToolbarCommand::CommandTooltip_Open = NSLOCTEXT("NodeToCode", "OpenWindowTooltip", "Open the Node to Code window");
const FText FN2CToolbarCommand::CommandTooltip_Collect = NSLOCTEXT("NodeToCode", "CollectNodesTooltip", "Collect nodes from current Blueprint graph and translate to code");
const FText FN2CToolbarCommand::CommandTooltip_CopyJson = NSLOCTEXT("NodeToCode", "CopyJsonTooltip", "Copy the serialized Blueprint JSON to clipboard");
#pragma region ODS
const FName FN2CToolbarCommand::CommandName_SaveBlueprintJson = TEXT("NodeToCode_SaveBlueprintJson");
const FName FN2CToolbarCommand::CommandName_SaveParsedFlowFiles = TEXT("NodeToCode_SaveParsedFlowFiles");
const FName FN2CToolbarCommand::CommandName_SaveParsedJson = TEXT("NodeToCode_SaveParsedJson");
const FName FN2CToolbarCommand::CommandName_SaveFlowJson = TEXT("NodeToCode_SaveFlowJson");
const FName FN2CToolbarCommand::CommandName_SaveFlowText = TEXT("NodeToCode_SaveFlowText");
const FName FN2CToolbarCommand::CommandName_OpenSaveFolder = TEXT("NodeToCode_OpenSaveFolder");
const FName FN2CToolbarCommand::CommandName_Bp2CppMcp = TEXT("NodeToCode_Bp2CppMcp");
const FName FN2CToolbarCommand::CommandName_InspectBlueprintAuraMcp = TEXT("NodeToCode_InspectBlueprintMcp");
const FName FN2CToolbarCommand::CommandName_CopyFlowJson = TEXT("NodeToCode_CopyFlowJson");
const FName FN2CToolbarCommand::CommandName_CopyFlowText = TEXT("NodeToCode_CopyFlowText");
const FName FN2CToolbarCommand::CommandName_CopyParsedJson = TEXT("NodeToCode_CopyParsedJson");
const FText FN2CToolbarCommand::CommandLabel_SaveBlueprintJson = NSLOCTEXT("NodeToCode", "SaveBlueprintJson", "Save Blueprint JSON");

const FText FN2CToolbarCommand::CommandLabel_SaveParsedFlowFiles = NSLOCTEXT("NodeToCode", "SaveParsedFlowFiles", "Save Analysis Files");
const FText FN2CToolbarCommand::CommandLabel_SaveParsedJson = NSLOCTEXT("NodeToCode", "SaveParsedJson", "Save Parsed Json");
const FText FN2CToolbarCommand::CommandLabel_SaveFlowJson = NSLOCTEXT("NodeToCode", "SaveFlowJson", "Save Flow Json");
const FText FN2CToolbarCommand::CommandLabel_SaveFlowText = NSLOCTEXT("NodeToCode", "SaveFlowText", "Save Flow Text");
const FText FN2CToolbarCommand::CommandLabel_OpenSaveFolder = NSLOCTEXT("NodeToCode", "OpenSaveFolder", "Open Save Folder");
const FText FN2CToolbarCommand::CommandLabel_Bp2CppMcp = NSLOCTEXT("NodeToCode", "Bp2CppMcp", "BP --> CPP (MCP)");
const FText FN2CToolbarCommand::CommandLabel_InspectBlueprintAuraMcp = NSLOCTEXT("NodeToCode", "InspectBlueprintMcp", "Inspect Blueprint (Aura MCP API)");
const FText FN2CToolbarCommand::CommandLabel_CopyFlowJson = NSLOCTEXT("NodeToCode", "CopyFlowJson", "Copy Flow Json");
const FText FN2CToolbarCommand::CommandLabel_CopyFlowText = NSLOCTEXT("NodeToCode", "CopyFlowText", "Copy Flow Text");
const FText FN2CToolbarCommand::CommandLabel_CopyParsedJson = NSLOCTEXT("NodeToCode", "CopyParsedJson", "Copy Parsed Json");

const FText FN2CToolbarCommand::CommandTooltip_SaveBlueprintJson = NSLOCTEXT("NodeToCode", "SaveBlueprintJsonTooltip", "Save the serialized Blueprint JSON to file");
const FText FN2CToolbarCommand::CommandTooltip_SaveParsedFlowFiles = NSLOCTEXT("NodeToCode", "SaveParsedFlowFilesTooltip", "Save parsed.json, flow.json, flow.txt, graph.txt, and structs.txt for the current Blueprint graph");
const FText FN2CToolbarCommand::CommandTooltip_SaveParsedJson = NSLOCTEXT("NodeToCode", "SaveParsedJsonTooltip", "Save parsed.json under the translation output directory");
const FText FN2CToolbarCommand::CommandTooltip_SaveFlowJson = NSLOCTEXT("NodeToCode", "SaveFlowJsonTooltip", "Save flow.json under the translation output directory");
const FText FN2CToolbarCommand::CommandTooltip_SaveFlowText = NSLOCTEXT("NodeToCode", "SaveFlowTextTooltip", "Save flow.txt under the translation output directory");
const FText FN2CToolbarCommand::CommandTooltip_OpenSaveFolder = NSLOCTEXT("NodeToCode", "OpenSaveFolderTooltip", "Open the translation output folder for the current Blueprint graph");
const FText FN2CToolbarCommand::CommandTooltip_Bp2CppMcp = NSLOCTEXT("NodeToCode", "Bp2CppMcpTooltip", "Run BP to C++ conversion using MCP workflow");
const FText FN2CToolbarCommand::CommandTooltip_InspectBlueprintAuraMcp = NSLOCTEXT("NodeToCode", "InspectBlueprintMcpTooltip", "Call the inspect-blueprint AuraMCP API for the current Blueprint graph and save the response to file");
const FText FN2CToolbarCommand::CommandTooltip_CopyFlowJson = NSLOCTEXT("NodeToCode", "CopyFlowJsonTooltip", "Copy flow.json text to clipboard");
const FText FN2CToolbarCommand::CommandTooltip_CopyFlowText = NSLOCTEXT("NodeToCode", "CopyFlowTextTooltip", "Copy flow text to clipboard");
const FText FN2CToolbarCommand::CommandTooltip_CopyParsedJson = NSLOCTEXT("NodeToCode", "CopyParsedJsonTooltip", "Copy parsed.json text to clipboard");
#pragma endregion

FN2CToolbarCommand::FN2CToolbarCommand()
    : TCommands<FN2CToolbarCommand>(
        TEXT("NodeToCode"), // Context name for fast lookup
        NSLOCTEXT("NodeToCode", "NodeToCode", "Node to Code"), // Localized context name
        NAME_None, // Parent
        FAppStyle::GetAppStyleSetName() // Icon style
    )
{
}

void FN2CToolbarCommand::RegisterCommands()
{
    FN2CLogger::Get().Log(TEXT("Registering N2C toolbar commands"), EN2CLogSeverity::Debug);
    
    UI_COMMAND(
        OpenWindowCommand,
        "Open Node to Code Editor",
        "Open the Node to Code Editor window",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    UI_COMMAND(
        CollectNodesCommand,
        "Translate Blueprint Graph to Code",
        "Translate current Blueprint graph to code.\nResults will be in the Node to Code Editor window.",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    #pragma region ODS
    UI_COMMAND(
        SaveBlueprintJsonCommand,
        "Save Blueprint JSON",
        "Save the serialized Blueprint JSON to file",
        EUserInterfaceActionType::Button,
        FInputChord()
    );
    #pragma endregion
    
    UI_COMMAND(
        CopyJsonCommand,
        "Copy Blueprint JSON",
        "Copy the serialized Blueprint JSON to clipboard for external use",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    #pragma region ODS
    UI_COMMAND(
        SaveParsedFlowFilesCommand,
        "Save Analysis Files",
        "Save parsed.json, flow.json, flow.txt, graph.txt, and structs.txt for the current Blueprint graph",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    // Parsed Json 저장
    UI_COMMAND(
        SaveParsedJsonCommand,
        "Save Parsed Json",
        "Save parsed.json for the current Blueprint graph",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    // Flow Json 저장
    UI_COMMAND(
        SaveFlowJsonCommand,
        "Save Flow Json",
        "Save flow.json for the current Blueprint graph",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    // Flow Text 저장
    UI_COMMAND(
        SaveFlowTextCommand,
        "Save Flow Text",
        "Save flow text for the current Blueprint graph",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    UI_COMMAND(
        OpenSaveFolderCommand,
        "Open Save Folder",
        "Open the translation output folder for the current Blueprint graph",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    UI_COMMAND(
        Bp2CppMcpCommand,
        "BP --> CPP (MCP)",
        "Run BP to C++ conversion using MCP workflow",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    UI_COMMAND(
        InspectBlueprintAuraMcpCommand,
        "Inspect Blueprint (Aura MCP API)",
        "Call the inspect-blueprint MCP API for the current Blueprint graph and save the response to file",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    // Flow Json 복사
    UI_COMMAND(
        CopyFlowJsonCommand,
        "Copy Flow Json",
        "Copy flow.json text for the current Blueprint graph to the clipboard",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    // Flow 텍스트 복사
    UI_COMMAND(
        CopyFlowTextCommand,
        "Copy Flow Text",
        "Copy flow text for the current Blueprint graph to the clipboard",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    // Parsed Json 복사
    UI_COMMAND(
        CopyParsedJsonCommand,
        "Copy Parsed Json",
        "Copy parsed.json text for the current Blueprint graph to the clipboard",
        EUserInterfaceActionType::Button,
        FInputChord()
    );
    #pragma endregion
    
    FN2CLogger::Get().Log(TEXT("N2C toolbar commands registered"), EN2CLogSeverity::Debug);
}

#undef LOCTEXT_NAMESPACE
