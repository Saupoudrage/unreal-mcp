#include "Commands/Migration/DeleteBlueprintFunctionCommand.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Services/AssetDiscoveryService.h"

DEFINE_LOG_CATEGORY_STATIC(LogMigrationDelete, Log, All);

FString FDeleteBlueprintFunctionCommand::Execute(const FString& Parameters)
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

    FString FunctionName;
    if (!JsonObject->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    bool bBackup = true;
    JsonObject->TryGetBoolField(TEXT("backup"), bBackup);

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

    // Find the function graph to delete
    UEdGraph* GraphToDelete = nullptr;
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph && Graph->GetName() == FunctionName)
        {
            GraphToDelete = Graph;
            break;
        }
    }

    // Also check FunctionGraphs array
    if (!GraphToDelete)
    {
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph && Graph->GetName() == FunctionName)
            {
                GraphToDelete = Graph;
                break;
            }
        }
    }

    if (!GraphToDelete)
    {
        return CreateErrorResponse(FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
    }

    // Create backup if requested
    FString BackupPath;
    if (bBackup)
    {
        TSharedPtr<FJsonObject> BackupJson = MakeShared<FJsonObject>();
        BackupJson->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
        BackupJson->SetStringField(TEXT("function_name"), FunctionName);
        BackupJson->SetStringField(TEXT("backup_time"), FDateTime::Now().ToString());
        BackupJson->SetObjectField(TEXT("graph_data"), SerializeGraph(GraphToDelete, true));

        FString FileName = FString::Printf(TEXT("backup_func_%s_%s_%s.json"),
            *Blueprint->GetName(),
            *FunctionName,
            *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

        BackupPath = WriteJsonToTempFile(FileName, BackupJson);
    }

    // Get node count before deletion for reporting
    int32 NodeCount = GraphToDelete->Nodes.Num();

    // Remove the function graph
    Blueprint->Modify();
    FBlueprintEditorUtils::RemoveGraph(Blueprint, GraphToDelete);

    // Mark blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    // Build result
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultJson->SetStringField(TEXT("function_name"), FunctionName);
    ResultJson->SetNumberField(TEXT("nodes_removed"), NodeCount);

    if (!BackupPath.IsEmpty())
    {
        ResultJson->SetStringField(TEXT("backup_path"), BackupPath);
    }

    ResultJson->SetBoolField(TEXT("requires_compile"), true);
    ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully deleted function '%s' (%d nodes)"), *FunctionName, NodeCount));

    UE_LOG(LogMigrationDelete, Log, TEXT("Deleted function '%s' from Blueprint '%s' (%d nodes)"), *FunctionName, *Blueprint->GetName(), NodeCount);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

    return OutputString;
}

bool FDeleteBlueprintFunctionCommand::ValidateParams(const FString& Parameters) const
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Parameters);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return false;
    }

    FString BlueprintPath, FunctionName;
    return JsonObject->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) && !BlueprintPath.IsEmpty()
        && JsonObject->TryGetStringField(TEXT("function_name"), FunctionName) && !FunctionName.IsEmpty();
}

FString FDeleteBlueprintFunctionCommand::GetExportDirectory()
{
    FString ExportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("Exports"));

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ExportDir))
    {
        PlatformFile.CreateDirectoryTree(*ExportDir);
    }

    return ExportDir;
}

FString FDeleteBlueprintFunctionCommand::WriteJsonToTempFile(const FString& FileName, const TSharedPtr<FJsonObject>& JsonContent)
{
    FString ExportDir = GetExportDirectory();
    FString FilePath = FPaths::Combine(ExportDir, FileName);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonContent.ToSharedRef(), Writer);

    if (FFileHelper::SaveStringToFile(JsonString, *FilePath))
    {
        UE_LOG(LogMigrationDelete, Log, TEXT("Wrote backup file: %s"), *FilePath);
        return FilePath;
    }

    UE_LOG(LogMigrationDelete, Error, TEXT("Failed to write backup file: %s"), *FilePath);
    return FString();
}

TSharedPtr<FJsonObject> FDeleteBlueprintFunctionCommand::SerializePin(UEdGraphPin* Pin, bool bIncludeConnections)
{
    if (!Pin)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();

    PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
    PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
    PinJson->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

    if (Pin->PinType.PinSubCategoryObject.IsValid())
    {
        PinJson->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategoryObject->GetName());
    }

    if (!Pin->DefaultValue.IsEmpty())
    {
        PinJson->SetStringField(TEXT("default_value"), Pin->DefaultValue);
    }

    if (bIncludeConnections && Pin->LinkedTo.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            if (LinkedPin && LinkedPin->GetOwningNode())
            {
                TSharedPtr<FJsonObject> ConnectionJson = MakeShared<FJsonObject>();
                ConnectionJson->SetStringField(TEXT("node_guid"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
                ConnectionJson->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
                ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnectionJson));
            }
        }
        PinJson->SetArrayField(TEXT("connections"), ConnectionsArray);
    }

    return PinJson;
}

TSharedPtr<FJsonObject> FDeleteBlueprintFunctionCommand::SerializeNode(UEdGraphNode* Node)
{
    if (!Node)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();

    NodeJson->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
    NodeJson->SetStringField(TEXT("class"), Node->GetClass()->GetName());
    NodeJson->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    NodeJson->SetNumberField(TEXT("pos_x"), Node->NodePosX);
    NodeJson->SetNumberField(TEXT("pos_y"), Node->NodePosY);

    // Serialize pins
    TArray<TSharedPtr<FJsonValue>> PinsArray;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin)
        {
            TSharedPtr<FJsonObject> PinJson = SerializePin(Pin, true);
            if (PinJson.IsValid())
            {
                PinsArray.Add(MakeShared<FJsonValueObject>(PinJson));
            }
        }
    }
    NodeJson->SetArrayField(TEXT("pins"), PinsArray);

    return NodeJson;
}

TSharedPtr<FJsonObject> FDeleteBlueprintFunctionCommand::SerializeGraph(UEdGraph* Graph, bool bIncludeDefaults)
{
    if (!Graph)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();

    GraphJson->SetStringField(TEXT("name"), Graph->GetName());
    GraphJson->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
    GraphJson->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

    TArray<TSharedPtr<FJsonValue>> NodesArray;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node)
        {
            TSharedPtr<FJsonObject> NodeJson = SerializeNode(Node);
            if (NodeJson.IsValid())
            {
                NodesArray.Add(MakeShared<FJsonValueObject>(NodeJson));
            }
        }
    }
    GraphJson->SetArrayField(TEXT("nodes"), NodesArray);

    return GraphJson;
}

FString FDeleteBlueprintFunctionCommand::CreateErrorResponse(const FString& ErrorMessage) const
{
    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetBoolField(TEXT("success"), false);
    ResponseObj->SetStringField(TEXT("error"), ErrorMessage);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

    return OutputString;
}
