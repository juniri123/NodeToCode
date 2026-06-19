#pragma once

#include "CoreMinimal.h"
#include "Models/Python/N2CFlowModel.h"

class UEdGraph;
class UK2Node;
class FJsonObject;

#pragma region ODS
using K2NodeArray = TArray<UK2Node*>;

using NodeMap = TMap<FString, TSharedPtr<N2CFlow::Node>>;

using SharedStepPtr = TSharedPtr<N2CFlow::Step>;
using StepArray = TArray<SharedStepPtr>;
using StepMap = TMap<FString, SharedStepPtr>;

namespace N2CFlow
{
    struct Node;
    struct Step;
}

struct FN2CFlowData
{
    NodeMap NodesByName;
    StepMap StepsByKey;
    SharedStepPtr EntryStep;
    StepMap CommonSteps;
    N2CFlow::FGUIDAlias GuidAlias;
};

class FN2CFlowBuilder
{
    public:
        static bool BuildFlowDataFromGraph(UEdGraph* Graph, FN2CFlowData& OutData, FString& OutError);
        static bool BuildFlowDataFromNodes(const K2NodeArray& Nodes, FN2CFlowData& OutData, FString& OutError);
        static bool BuildFlowJsonFromGraph(UEdGraph* Graph, FString& OutJson, FString& OutError);
        static bool BuildFlowJsonFromNodes(const K2NodeArray& Nodes, FString& OutJson, FString& OutError);
        /** Flow 텍스트를 그래프에서 생성 */
        static bool BuildFlowTextFromGraph(UEdGraph* Graph, FString& OutText, FString& OutError);
        /** Flow 텍스트를 노드 배열에서 생성 */
        static bool BuildFlowTextFromNodes(const K2NodeArray& Nodes, FString& OutText, FString& OutError);

    private:
        static bool BuildNodesFromK2Nodes(const K2NodeArray& Nodes,
                                          NodeMap& OutNodesByName,
                                          N2CFlow::FGUIDAlias& OutGuidAlias);
        static void BuildStepsFromNodes(const NodeMap& NodesByName, StepMap& OutStepsByKey);
        static SharedStepPtr FindEntryStep(const StepMap& StepsByKey);
        static bool BuildExecFlow(const NodeMap& NodesByName,
                                StepMap& StepsByKey,
                                StepMap& CommonSteps,
                                const SharedStepPtr& EntryStep,
                                FString& OutError);
        static void ResolveMergingPoints(const SharedStepPtr& EntryStep,
                                        StepMap& StepsByKey,
                                        bool bDebug);

        static TSharedPtr<FJsonObject> FlowDataToJsonObject(const FN2CFlowData& Data);
        /** Flow 데이터 -> 텍스트 라인 변환 */
        static TArray<FString> FlowDataToTextLines(const FN2CFlowData& Data);
};
#pragma endregion
