// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/N2CMcpModule.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"

UN2CMcpModule* UN2CMcpModule::Get()
{
    static UN2CMcpModule* Instance = nullptr;
    if (!Instance)
    {
        Instance = NewObject<UN2CMcpModule>();
        Instance->AddToRoot();
    }
    return Instance;
}

void UN2CMcpModule::CreateSessionAsync(const FN2CMcpSessionRequest& Request, FN2CMcpSessionComplete OnComplete)
{
    if (Request.PayloadMode == EN2CMcpPayloadMode::RawContent)
    {
        if (Request.FlowJson.IsEmpty() || Request.ParsedJson.IsEmpty())
        {
            const FString Error = TEXT("MCP session creation requires flow.json and parsed.json content.");
            FN2CLogger::Get().LogWarning(Error);
            OnComplete.ExecuteIfBound(false, FString(), Error);
            return;
        }
    }
    else
    {
        if (Request.FlowFilename.IsEmpty() || Request.ParsedFilename.IsEmpty())
        {
            const FString Error = TEXT("MCP session creation requires flow_filename and parsed_filename.");
            FN2CLogger::Get().LogWarning(Error);
            OnComplete.ExecuteIfBound(false, FString(), Error);
            return;
        }
    }

    FString Endpoint = Request.ServerBaseUrl;
    Endpoint.TrimEndInline();

    if (!Endpoint.EndsWith(TEXT("/")))
    {
        Endpoint += TEXT("/");
    }

    FString EndpointPath = Request.SessionCreateEndpoint;
    EndpointPath.TrimStartAndEndInline();
    if (EndpointPath.StartsWith(TEXT("/")))
    {
        EndpointPath.RightChopInline(1);
    }

    const FString Url = Endpoint + EndpointPath;

    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    if (Request.PayloadMode == EN2CMcpPayloadMode::RawContent)
    {
        RootObject->SetStringField(TEXT("flow"), Request.FlowJson);
        RootObject->SetStringField(TEXT("parsed"), Request.ParsedJson);
    }
    else
    {
        RootObject->SetStringField(TEXT("flow_filename"), Request.FlowFilename);
        RootObject->SetStringField(TEXT("parsed_filename"), Request.ParsedFilename);
    }

    if (!Request.FlowText.IsEmpty())
    {
        RootObject->SetStringField(TEXT("flow_text"), Request.FlowText);
    }

    if (!Request.BlueprintJson.IsEmpty())
    {
        RootObject->SetStringField(TEXT("blueprint_json"), Request.BlueprintJson);
    }

    if (!Request.PromptText.IsEmpty())
    {
        RootObject->SetStringField(TEXT("prompt"), Request.PromptText);
    }

    if (!Request.GraphName.IsEmpty())
    {
        RootObject->SetStringField(TEXT("graph_name"), Request.GraphName);
    }

    if (!Request.BlueprintName.IsEmpty())
    {
        RootObject->SetStringField(TEXT("blueprint_name"), Request.BlueprintName);
    }

    RootObject->SetStringField(TEXT("payload_mode"),
        Request.PayloadMode == EN2CMcpPayloadMode::RawContent ? TEXT("raw") : TEXT("file"));

    FString Payload;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
    {
        const FString Error = TEXT("Failed to serialize MCP session request payload.");
        FN2CLogger::Get().LogError(Error);
        OnComplete.ExecuteIfBound(false, FString(), Error);
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(Url);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetContentAsString(Payload);

    HttpRequest->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr InRequest, FHttpResponsePtr InResponse, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !InResponse.IsValid())
            {
                const FString Error = TEXT("MCP session request failed or returned no response.");
                FN2CLogger::Get().LogError(Error);
                OnComplete.ExecuteIfBound(false, FString(), Error);
                return;
            }

            const int32 StatusCode = InResponse->GetResponseCode();
            const FString ResponseBody = InResponse->GetContentAsString();
            if (StatusCode < 200 || StatusCode >= 300)
            {
                const FString Error = FString::Printf(TEXT("MCP session request failed (HTTP %d): %s"), StatusCode, *ResponseBody);
                FN2CLogger::Get().LogError(Error);
                OnComplete.ExecuteIfBound(false, FString(), Error);
                return;
            }

            TSharedPtr<FJsonObject> JsonObject;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
            if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
            {
                const FString Error = FString::Printf(TEXT("Failed to parse MCP session response: %s"), *ResponseBody);
                FN2CLogger::Get().LogError(Error);
                OnComplete.ExecuteIfBound(false, FString(), Error);
                return;
            }

            bool bOk = false;
            JsonObject->TryGetBoolField(TEXT("ok"), bOk);
            FString SessionId;
            JsonObject->TryGetStringField(TEXT("session_id"), SessionId);

            if (!bOk || SessionId.IsEmpty())
            {
                const FString Error = FString::Printf(TEXT("MCP session response missing session_id: %s"), *ResponseBody);
                FN2CLogger::Get().LogError(Error);
                OnComplete.ExecuteIfBound(false, FString(), Error);
                return;
            }

            OnComplete.ExecuteIfBound(true, SessionId, FString());
        }
    );

    if (!HttpRequest->ProcessRequest())
    {
        const FString Error = TEXT("Failed to start MCP session request.");
        FN2CLogger::Get().LogError(Error);
        OnComplete.ExecuteIfBound(false, FString(), Error);
    }
}
