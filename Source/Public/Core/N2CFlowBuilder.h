#pragma once

#include "CoreMinimal.h"
#include "Models/Python/N2CFlowModel.h"

class UEdGraph;
class UK2Node;
class FJsonObject;

#pragma region ODS
namespace N2CFlow
{
    struct Node;
    struct Step;
}

struct FN2CFlowData
{
    TMap<FString, TSharedPtr<N2CFlow::Node>> NodesByName;
    TMap<FString, TSharedPtr<N2CFlow::Step>> StepsByKey;
    TSharedPtr<N2CFlow::Step> EntryStep;
    TMap<FString, TSharedPtr<N2CFlow::Step>> CommonSteps;
    N2CFlow::FGUIDAlias GuidAlias;
};

class FN2CFlowBuilder
{
    public:
        static bool BuildFlowDataFromGraph(UEdGraph* Graph, FN2CFlowData& OutData, FString& OutError);
        static bool BuildFlowDataFromNodes(const TArray<UK2Node*>& Nodes, FN2CFlowData& OutData, FString& OutError);
        static bool BuildFlowJsonFromGraph(UEdGraph* Graph, FString& OutJson, FString& OutError);
        static bool BuildFlowJsonFromNodes(const TArray<UK2Node*>& Nodes, FString& OutJson, FString& OutError);
        /** Flow 텍스트를 그래프에서 생성 */
        static bool BuildFlowTextFromGraph(UEdGraph* Graph, FString& OutText, FString& OutError);
        /** Flow 텍스트를 노드 배열에서 생성 */
        static bool BuildFlowTextFromNodes(const TArray<UK2Node*>& Nodes, FString& OutText, FString& OutError);

    private:
        static bool BuildNodesFromK2Nodes(const TArray<UK2Node*>& Nodes, TMap<FString, TSharedPtr<N2CFlow::Node>>& OutNodesByName);
        static void BuildStepsFromNodes(const TMap<FString, TSharedPtr<N2CFlow::Node>>& NodesByName, TMap<FString, TSharedPtr<N2CFlow::Step>>& OutStepsByKey);
        static TSharedPtr<N2CFlow::Step> FindEntryStep(const TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey);
        static bool BuildExecFlow(const TMap<FString, TSharedPtr<N2CFlow::Node>>& NodesByName,
                                TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey,
                                TMap<FString, TSharedPtr<N2CFlow::Step>>& CommonSteps,
                                const TSharedPtr<N2CFlow::Step>& EntryStep,
                                FString& OutError);
        static void ResolveMergingPoints(const TSharedPtr<N2CFlow::Step>& EntryStep,
                                        TMap<FString, TSharedPtr<N2CFlow::Step>>& StepsByKey,
                                        bool bDebug);

        static TSharedPtr<FJsonObject> FlowDataToJsonObject(const FN2CFlowData& Data);
        /** Flow 데이터 -> 텍스트 라인 변환 */
        static TArray<FString> FlowDataToTextLines(const FN2CFlowData& Data);
};
#pragma endregion