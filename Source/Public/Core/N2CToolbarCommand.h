// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FN2CToolbarCommand : public TCommands<FN2CToolbarCommand>
{
public:
    FN2CToolbarCommand();

    // TCommands interface
    virtual void RegisterCommands() override;

    // Commands
    TSharedPtr<FUICommandInfo> OpenWindowCommand;
    TSharedPtr<FUICommandInfo> CollectNodesCommand;
    TSharedPtr<FUICommandInfo> CopyJsonCommand;
    TSharedPtr<FUICommandInfo> SaveParsedJsonCommand;
    TSharedPtr<FUICommandInfo> SaveFlowJsonCommand;
    TSharedPtr<FUICommandInfo> SaveFlowTextCommand;
    TSharedPtr<FUICommandInfo> OpenSaveFolderCommand;
    TSharedPtr<FUICommandInfo> CopyFlowJsonCommand;
    TSharedPtr<FUICommandInfo> CopyFlowTextCommand;
    TSharedPtr<FUICommandInfo> CopyParsedJsonCommand;

    // Command names and labels
    static const FName CommandName_Open;
    static const FName CommandName_Collect;
    static const FName CommandName_CopyJson;
    static const FName CommandName_SaveParsedJson;
    static const FName CommandName_SaveFlowJson;
    static const FName CommandName_SaveFlowText;
    static const FName CommandName_OpenSaveFolder;
    static const FName CommandName_CopyFlowJson;
    static const FName CommandName_CopyFlowText;
    static const FName CommandName_CopyParsedJson;
    static const FText CommandLabel_Open;
    static const FText CommandLabel_Collect;
    static const FText CommandLabel_CopyJson;
    static const FText CommandLabel_SaveParsedJson;
    static const FText CommandLabel_SaveFlowJson;
    static const FText CommandLabel_SaveFlowText;
    static const FText CommandLabel_OpenSaveFolder;
    static const FText CommandLabel_CopyFlowJson;
    static const FText CommandLabel_CopyFlowText;
    static const FText CommandLabel_CopyParsedJson;
    static const FText CommandTooltip_Open;
    static const FText CommandTooltip_Collect;
    static const FText CommandTooltip_CopyJson;
    static const FText CommandTooltip_SaveParsedJson;
    static const FText CommandTooltip_SaveFlowJson;
    static const FText CommandTooltip_SaveFlowText;
    static const FText CommandTooltip_OpenSaveFolder;
    static const FText CommandTooltip_CopyFlowJson;
    static const FText CommandTooltip_CopyFlowText;
    static const FText CommandTooltip_CopyParsedJson;
};
