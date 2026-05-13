// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COpenAIService.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CSystemPromptManager.h"

UN2CResponseParserBase* UN2COpenAIService::CreateResponseParser()
{
    UN2COpenAIResponseParser* Parser = NewObject<UN2COpenAIResponseParser>(this);
    return Parser;
}

void UN2COpenAIService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    
    // Find matching model enum for system prompt support check
    for (int32 i = 0; i < static_cast<int32>(EN2COpenAIModel::GPT4o_Mini_2024_07_18) + 1; i++)
    {
        EN2COpenAIModel Model = static_cast<EN2COpenAIModel>(i);
        if (FN2CLLMModelUtils::GetOpenAIModelValue(Model) == Config.Model)
        {
            OutSupportsSystemPrompts = FN2CLLMModelUtils::SupportsSystemPrompts(Model);
            return;
        }
    }
    
    // Default to supporting system prompts if model not found
    OutSupportsSystemPrompts = true;
}

void UN2COpenAIService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
    
    // Add organization header if available
    if (!OrganizationId.IsEmpty())
    {
        OutHeaders.Add(TEXT("OpenAI-Organization"), OrganizationId);
    }
}

FString UN2COpenAIService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Check if model supports system prompts
    bool bSupportsSystemPrompts = false;
    for (int32 i = 0; i < static_cast<int32>(EN2COpenAIModel::GPT4o_Mini_2024_07_18) + 1; i++)
    {
        EN2COpenAIModel Model = static_cast<EN2COpenAIModel>(i);
        if (FN2CLLMModelUtils::GetOpenAIModelValue(Model) == Config.Model)
        {
            bSupportsSystemPrompts = FN2CLLMModelUtils::SupportsSystemPrompts(Model);
            break;
        }
    }

    // Create and configure payload builder
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForOpenAI();
    
    // Set common parameters
    // Note: Temperature is not supported for o1/o3 models, but the payload builder will handle this
    PayloadBuilder->SetTemperature(0.0f);
    // open ai에서만 max token 설정을 넣어놔서 함수로 빼놓기가 애매함. 일단 하드 코딩함
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    int MaxTokens = FN2CLLMModelUtils::GetOpenAIModelInfo(Settings->OpenAI_Model).DefaultMaxOutputTokens;
    if (MaxTokens == 0 || Settings->OpenAIModelInfo.Contains(Settings->OpenAI_Model))
    {
        MaxTokens = Settings->OpenAIModelInfo.Find(Settings->OpenAI_Model)->DefaultMaxOutputTokens;        
    }
    PayloadBuilder->SetMaxTokens(MaxTokens);
    
    // Add JSON response format for models that support it
    // The payload builder will handle the differences between model types
    if (Config.Model != TEXT("o1-preview-2024-09-12") && Config.Model != TEXT("o1-mini-2024-09-12"))
    {
        PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());
    }
    
    // Determine final content based on system prompt support
    FString FinalContent = UserMessage;
    
    // Try prepending source files to the user message
    PromptManager->PrependSourceFilesToUserMessage(FinalContent);
    
    // Add messages
    if (bSupportsSystemPrompts)
    {
        PayloadBuilder->AddSystemMessage(SystemMessage);
        PayloadBuilder->AddUserMessage(FinalContent);
    }
    else
    {
        // Merge system and user prompts if model doesn't support system prompts
        FString MergedContent = PromptManager->MergePrompts(SystemMessage, FinalContent);
        PayloadBuilder->AddUserMessage(MergedContent);
    }
    
    // Build and return the payload
    return PayloadBuilder->Build();
}
