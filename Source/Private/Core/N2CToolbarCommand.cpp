// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CToolbarCommand.h"

#include "Styling/AppStyle.h"
#include "Framework/Commands/UICommandList.h"
#include "Utils/N2CLogger.h"

#define LOCTEXT_NAMESPACE "NodeToCode"

const FName FN2CToolbarCommand::CommandName_Open = TEXT("NodeToCode_OpenWindow");
const FName FN2CToolbarCommand::CommandName_Collect = TEXT("NodeToCode_CollectNodes");
const FName FN2CToolbarCommand::CommandName_CopyJson = TEXT("NodeToCode_CopyJson");
// N2C 확장: Flow 관련 툴바 커맨드 추가
const FName FN2CToolbarCommand::CommandName_SaveFlow = TEXT("NodeToCode_SaveFlow");
const FName FN2CToolbarCommand::CommandName_CopyFlowJson = TEXT("NodeToCode_CopyFlowJson");
const FName FN2CToolbarCommand::CommandName_CopyFlowText = TEXT("NodeToCode_CopyFlowText");
const FName FN2CToolbarCommand::CommandName_CopyParsedJson = TEXT("NodeToCode_CopyParsedJson");
const FText FN2CToolbarCommand::CommandLabel_Open = NSLOCTEXT("NodeToCode", "OpenWindow", "Open Node to Code");
const FText FN2CToolbarCommand::CommandLabel_Collect = NSLOCTEXT("NodeToCode", "CollectNodes", "Collect and Translate Nodes");
const FText FN2CToolbarCommand::CommandLabel_CopyJson = NSLOCTEXT("NodeToCode", "CopyJson", "Copy Blueprint JSON");
// N2C 확장: Flow 관련 라벨 추가
const FText FN2CToolbarCommand::CommandLabel_SaveFlow = NSLOCTEXT("NodeToCode", "SaveFlow", "Save Flow Json/Text");
const FText FN2CToolbarCommand::CommandLabel_CopyFlowJson = NSLOCTEXT("NodeToCode", "CopyFlowJson", "Copy Flow Json");
const FText FN2CToolbarCommand::CommandLabel_CopyFlowText = NSLOCTEXT("NodeToCode", "CopyFlowText", "Copy Flow Text");
const FText FN2CToolbarCommand::CommandLabel_CopyParsedJson = NSLOCTEXT("NodeToCode", "CopyParsedJson", "Copy Parsed Json");
const FText FN2CToolbarCommand::CommandTooltip_Open = NSLOCTEXT("NodeToCode", "OpenWindowTooltip", "Open the Node to Code window");
const FText FN2CToolbarCommand::CommandTooltip_Collect = NSLOCTEXT("NodeToCode", "CollectNodesTooltip", "Collect nodes from current Blueprint graph and translate to code");
const FText FN2CToolbarCommand::CommandTooltip_CopyJson = NSLOCTEXT("NodeToCode", "CopyJsonTooltip", "Copy the serialized Blueprint JSON to clipboard");
// N2C 확장: Flow 관련 툴팁 추가
const FText FN2CToolbarCommand::CommandTooltip_SaveFlow = NSLOCTEXT("NodeToCode", "SaveFlowTooltip", "Save flow.json and flow.txt under the translation output directory and open the folder");
const FText FN2CToolbarCommand::CommandTooltip_CopyFlowJson = NSLOCTEXT("NodeToCode", "CopyFlowJsonTooltip", "Copy flow.json text to clipboard");
const FText FN2CToolbarCommand::CommandTooltip_CopyFlowText = NSLOCTEXT("NodeToCode", "CopyFlowTextTooltip", "Copy flow text to clipboard");
const FText FN2CToolbarCommand::CommandTooltip_CopyParsedJson = NSLOCTEXT("NodeToCode", "CopyParsedJsonTooltip", "Copy parsed.json text to clipboard");

FN2CToolbarCommand::FN2CToolbarCommand()
    : TCommands<FN2CToolbarCommand>(
        TEXT("NodeToCode"), // Context name for fast lookup
        NSLOCTEXT("NodeToCode", "NodeToCode", "Node to Code"), // Localized context name
        NAME_None, // Parent
        FAppStyle::GetAppStyleSetName() // Icon style
    )
{
}

// N2C 확장: Flow 관련 커맨드 등록 포함
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
    
    UI_COMMAND(
        CopyJsonCommand,
        "Copy Blueprint JSON",
        "Copy the serialized Blueprint JSON to clipboard for external use",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    // Flow Json/Text 저장
    UI_COMMAND(
        SaveFlowCommand,
        "Save Parsed Json, Flow Json/Text",
        "Save Parsed Json and Flow Json/Text for the current Blueprint graph and open the folder",
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
    
    FN2CLogger::Get().Log(TEXT("N2C toolbar commands registered"), EN2CLogSeverity::Debug);
}

#undef LOCTEXT_NAMESPACE
