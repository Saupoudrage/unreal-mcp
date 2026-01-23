#include "Commands/Migration/GetBlueprintFunctionsCommand.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Services/AssetDiscoveryService.h"

DEFINE_LOG_CATEGORY_STATIC(LogMigrationFunctions, Log, All);

FString FGetBlueprintFunctionsCommand::Execute(const FString& Parameters)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Parameters);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return CreateErrorResponse(TEXT("Invalid JSON parameters"));
    }

    FString BlueprintPath;
    if (!JsonObject->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    bool bIncludeInherited = false;
    JsonObject->TryGetBoolField(TEXT("include_inherited"), bIncludeInherited);

    // Load Blueprint
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!Blueprint)
    {
        TArray<FString> FoundBlueprints = FAssetDiscoveryService::Get().FindBlueprints(BlueprintPath);
        if (FoundBlueprints.Num() > 0)
        {
            Blueprint = LoadObject<UBlueprint>(nullptr, *FoundBlueprints[0]);
        }
    }

    if (!Blueprint)
    {
        return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    // Collect all function graphs
    TArray<TSharedPtr<FJsonValue>> FunctionsArray;

    // Get function graphs
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (!Graph) continue;

        TSharedPtr<FJsonObject> FuncJson = MakeShared<FJsonObject>();
        FuncJson->SetStringField(TEXT("name"), Graph->GetName());
        FuncJson->SetStringField(TEXT("graph_name"), Graph->GetName());
        FuncJson->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        FuncJson->SetBoolField(TEXT("is_event"), false);
        FuncJson->SetStringField(TEXT("type"), TEXT("Function"));

        // Check if it's an override
        bool bIsOverride = false;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
            {
                // Check if this function exists in parent class
                if (Blueprint->ParentClass)
                {
                    UFunction* ParentFunc = Blueprint->ParentClass->FindFunctionByName(FName(*Graph->GetName()));
                    bIsOverride = (ParentFunc != nullptr);
                }
                break;
            }
        }
        FuncJson->SetBoolField(TEXT("is_override"), bIsOverride);

        FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncJson));
    }

    // Get event graphs and identify events
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (!Graph) continue;

        // Check for event nodes in the graph
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
            {
                TSharedPtr<FJsonObject> EventJson = MakeShared<FJsonObject>();
                EventJson->SetStringField(TEXT("name"), EventNode->EventReference.GetMemberName().ToString());
                EventJson->SetStringField(TEXT("graph_name"), Graph->GetName());
                EventJson->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
                EventJson->SetBoolField(TEXT("is_event"), true);
                EventJson->SetStringField(TEXT("type"), TEXT("Event"));

                if (EventNode->EventReference.GetMemberParentClass())
                {
                    EventJson->SetStringField(TEXT("event_class"), EventNode->EventReference.GetMemberParentClass()->GetName());
                }

                FunctionsArray.Add(MakeShared<FJsonValueObject>(EventJson));
            }
        }

        // Also add the graph itself
        TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();
        GraphJson->SetStringField(TEXT("name"), Graph->GetName());
        GraphJson->SetStringField(TEXT("graph_name"), Graph->GetName());
        GraphJson->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphJson->SetBoolField(TEXT("is_event"), false);
        GraphJson->SetStringField(TEXT("type"), TEXT("EventGraph"));

        FunctionsArray.Add(MakeShared<FJsonValueObject>(GraphJson));
    }

    // Get macro graphs
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (!Graph) continue;

        TSharedPtr<FJsonObject> MacroJson = MakeShared<FJsonObject>();
        MacroJson->SetStringField(TEXT("name"), Graph->GetName());
        MacroJson->SetStringField(TEXT("graph_name"), Graph->GetName());
        MacroJson->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        MacroJson->SetBoolField(TEXT("is_event"), false);
        MacroJson->SetStringField(TEXT("type"), TEXT("Macro"));

        FunctionsArray.Add(MakeShared<FJsonValueObject>(MacroJson));
    }

    // Build result
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultJson->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultJson->SetNumberField(TEXT("function_count"), FunctionsArray.Num());
    ResultJson->SetArrayField(TEXT("functions"), FunctionsArray);

    if (Blueprint->ParentClass)
    {
        ResultJson->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
    }

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

    return OutputString;
}

bool FGetBlueprintFunctionsCommand::ValidateParams(const FString& Parameters) const
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Parameters);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return false;
    }

    FString BlueprintPath;
    return JsonObject->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) && !BlueprintPath.IsEmpty();
}

FString FGetBlueprintFunctionsCommand::CreateErrorResponse(const FString& ErrorMessage) const
{
    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetBoolField(TEXT("success"), false);
    ResponseObj->SetStringField(TEXT("error"), ErrorMessage);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

    return OutputString;
}
