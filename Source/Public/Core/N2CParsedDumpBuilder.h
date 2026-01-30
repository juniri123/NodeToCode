// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;
class UK2Node;

/**
 * UE 블루프린트 그래프 → 덤프 기반 JSON(파이썬 _parsed.json 유사 포맷) 빌더
 * - 덤프 텍스트의 핵심 필드를 UE 데이터 구조에서 재구성
 * - LLM 참고용으로 저장
 */
class FN2CParsedDumpBuilder
{
public:
    /** Graph에서 덤프 유사 JSON 생성 */
    static bool BuildParsedJsonFromGraph(UEdGraph* Graph, FString& OutJson, FString& OutError);

    /** Nodes에서 덤프 유사 JSON 생성 */
    static bool BuildParsedJsonFromNodes(const TArray<UK2Node*>& Nodes, FString& OutJson, FString& OutError);
};
