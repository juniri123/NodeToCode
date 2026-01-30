#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace N2CFlow
{
struct Pin
{
    // 핀 이름
    FString Name;
    // 핀 GUID
    FString Guid;
    // 이 핀을 가진 노드 이름
    FString NodeName;
    // 이 핀을 가진 노드 GUID
    FString NodeGuid;

    Pin() = default;
    Pin(FString InPinName,
        FString InPinGuid,
        FString InNodeName,
        FString InNodeGuid)
        : Name(MoveTemp(InPinName))
        , Guid(MoveTemp(InPinGuid))
        , NodeName(MoveTemp(InNodeName))
        , NodeGuid(MoveTemp(InNodeGuid))
    {
    }

    TSharedPtr<FJsonObject> ToJsonObject() const;
    static bool FromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, Pin& OutPin);
};

struct Link
{
    // 출발 핀
    Pin FromPin;
    // 도착 핀
    Pin ToPin;

    Link() = default;
    Link(Pin InFrom, Pin InTo)
        : FromPin(MoveTemp(InFrom))
        , ToPin(MoveTemp(InTo))
    {
    }

    TSharedPtr<FJsonObject> ToJsonObject() const;
    static bool FromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, Link& OutLink);
};

struct Node
{
    // 노드 이름
    FString Name;
    // 노드 GUID
    FString Guid;

    // 실행 입력/출력 핀
    TArray<Pin> ExecInPins;
    TArray<Pin> ExecOutPins;
    // 실행 입력/출력 핀 링크
    TArray<Link> ExecInLinks;
    TArray<Link> ExecOutLinks;

    // 데이터 입력/출력 핀
    TArray<Pin> DataInPins;
    TArray<Pin> DataOutPins;
    // 데이터 입력/출력 핀 링크
    TArray<Link> DataInLinks;
    TArray<Link> DataOutLinks;

    Node() = default;
    Node(FString InName, FString InGuid)
        : Name(MoveTemp(InName))
        , Guid(MoveTemp(InGuid))
    {
    }

    TSharedPtr<FJsonObject> ToJsonObject() const;
    static bool FromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, Node& OutNode);
};

struct Step : public TSharedFromThis<Step>
{
    // 스텝 키 (보통 노드 이름, 중복 시 머징 접미사가 붙을 수 있음)
    FString Key;

    // 이 스텝에 연결된 노드 (직렬화 시 node_name/node_guid 사용)
    TSharedPtr<Node> Node;
    // node.exec_in_links의 from 핀들
    TArray<Pin> FromPins;

    // 분기 자식 노드들 (직렬화 시 branch_keys로 저장)
    TArray<TSharedPtr<Step>> Branches;
    // 직렬 흐름 next 노드 (직렬화 시 next_key로 저장)
    TSharedPtr<Step> Next;
    // 이 스텝으로 머지되는 placeholder들 (common/merging point에서 사용)
    TArray<TSharedPtr<Step>> CommonPlaceholders;
    // 들여쓰기 depth (흐름 출력용)
    int32 LogicDepth = 0;

    // 분기 스텝 여부 (부모의 exec-out 핀이 여러 개인 경우)
    bool bIsBranched = false;
    // Common step 위치를 표시하는 placeholder 여부
    bool bIsCommonPlaceholder = false;
    // 머지 포인트 여부
    bool bIsMergingPoint = false;
    // 주석 처리 여부 (머지되어 사라진 placeholder 등)
    bool bIsCommentOut = false;
    // fallthrough 머지 여부 (switch-case 특수 처리)
    bool bIsFallthrough = false;

    // 런타임 전용 outlink 인덱스 (직렬화하지 않음)
    TOptional<int32> OutLinkIdx;

    // JSON 복원 시 사용하는 필드들
    FString NodeName;
    FString NodeGuid;
    TArray<FString> BranchKeys;
    FString NextKey;
    TArray<FString> CommonPlaceholderKeys;

    void MakeAsEntry(int32 InLogicDepth);
    int32 GetOutlinkIdx();
    int32 NextOutlinkIdx();
    const Link* GetOutlink() const;
    bool HasAllOutlinkProcessed() const;
    bool HasMultipleExecInlink() const;
    bool HasBranches() const;
    bool IsCommonStep() const;
    int32 PopLogicDepth(int32 GlobalLogicDepth) const;
    int32 PushLogicDepth(int32 GlobalLogicDepth) const;

    void AppendFromPin(const Link& InLink);
    void AppendBranch(const TSharedPtr<Step>& InBranch);
    void SetNext(const TSharedPtr<Step>& InNext);
    void RecordCommonPlaceholder(const TSharedPtr<Step>& InPlaceholder);

    TSharedPtr<FJsonObject> ToJsonObject() const;
    static bool FromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, Step& OutStep);
};

struct MergingGroup
{
    // placeholder들이 향하는 공통 목적지 스텝
    TSharedPtr<Step> CommonStep;
    // 생성해야 하는 머지 포인트 스텝
    TSharedPtr<Step> MergingPointStep;
    // 이 그룹에 속한 placeholder 목록
    TArray<TSharedPtr<Step>> Placeholders;
    // fallthrough 머지 여부
    bool bIsFallthrough = false;

    MergingGroup(TSharedPtr<Step> InCommonStep,
                 TSharedPtr<Step> InMergingPointStep,
                 TArray<TSharedPtr<Step>> InPlaceholders,
                 bool bInIsFallthrough)
        : CommonStep(MoveTemp(InCommonStep))
        , MergingPointStep(MoveTemp(InMergingPointStep))
        , Placeholders(MoveTemp(InPlaceholders))
        , bIsFallthrough(bInIsFallthrough)
    {
    }
};
} // namespace N2CFlow
