#pragma once

#include "CoreMinimal.h"
#include "Core/N2CFlowBuilder.h"

class UK2Node;

#pragma region ODS
class FN2CFlowBuilder_01
{
public:
    static bool BuildFlowTextFromNodes_01(const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError);
    static bool BuildFlowTextFromGraph_01(UEdGraph* Graph, FString& OutText, FString& OutError);

private:
    /** Flow 데이터 -> 텍스트 라인 변환 */
    static TArray<FString> FlowDataToTextLines_01(const FN2CFlowData& Data);
};
#pragma endregion
