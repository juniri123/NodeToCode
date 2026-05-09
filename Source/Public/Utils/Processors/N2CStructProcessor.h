// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_StructOperation.h"
#include "K2Node_SetFieldsInStruct.h"

/**
 * @class FN2CStructProcessor
 * @brief Processor for struct-related nodes
 */
class NODETOCODE_API FN2CStructProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CStructProcessor() {}
    
    /** Destructor */
    virtual ~FN2CStructProcessor() {}

    /** Get the MCP graph text label for struct nodes. */
    virtual FString GetGraphTextLabel(UK2Node* Node) const override;
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
