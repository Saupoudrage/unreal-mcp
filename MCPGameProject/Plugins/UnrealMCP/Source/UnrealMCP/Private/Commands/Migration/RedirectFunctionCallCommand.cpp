#include "Commands/Migration/RedirectFunctionCallCommand.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Services/AssetDiscoveryService.h"

DEFINE_LOG_CATEGORY_STATIC(LogMigrationRedirect, Log, All);

FString FRedirectFunctionCallCommand::Execute(const FString& Parameters)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Parameters);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return CreateErrorResponse(TEXT("Invalid JSON parameters"));
    }

    FString SourceBlueprintPath;
    if (!JsonObject->TryGetStringField(TEXT("source_blueprint"), SourceBlueprintPath))
    {
        return CreateErrorResponse(TEXT("Missing 'source_blueprint' parameter"));
    }

    FString SourceFunction;
    if (!JsonObject->TryGetStringField(TEXT("source_function"), SourceFunction))
    {
        return CreateErrorResponse(TEXT("Missing 'source_function' parameter"));
    }

    FString TargetClass;
    if (!JsonObject->TryGetStringField(TEXT("target_class"), TargetClass))
    {
        return CreateErrorResponse(TEXT("Missing 'target_class' parameter"));
    }

    FString TargetFunction;
    if (!JsonObject->TryGetStringField(TEXT("target_function"), TargetFunction))
    {
        return CreateErrorResponse(TEXT("Missing 'target_function' parameter"));
    }

    bool bDryRun = true;
    JsonObject->TryGetBoolField(TEXT("dry_run"), bDryRun);

    bool bBackup = true;
    JsonObject->TryGetBoolField(TEXT("backup"), bBackup);

    // Load source Blueprint
    UBlueprint* SourceBlueprint = LoadObject<UBlueprint>(nullptr, *SourceBlueprintPath);
    if (!SourceBlueprint)
    {
        TArray<FString> FoundBlueprints = FAssetDiscoveryService::Get().FindBlueprints(SourceBlueprintPath);
        if (FoundBlueprints.Num() > 0)
        {
            SourceBlueprint = LoadObject<UBlueprint>(nullptr, *FoundBlueprints[0]);
        }
    }

    if (!SourceBlueprint)
    {
        return CreateErrorResponse(FString::Printf(TEXT("Source Blueprint not found: %s"), *SourceBlueprintPath));
    }

    // Find target class and function
    UClass* NewTargetClass = FindObject<UClass>(nullptr, *TargetClass);
    if (!NewTargetClass)
    {
        NewTargetClass = LoadClass<UObject>(nullptr, *TargetClass);
    }

    if (!NewTargetClass)
    {
        return CreateErrorResponse(FString::Printf(TEXT("Target class not found: %s"), *TargetClass));
    }

    UFunction* NewFunction = NewTargetClass->FindFunctionByName(FName(*TargetFunction));
    if (!NewFunction)
    {
        return CreateErrorResponse(FString::Printf(TEXT("Target function not found: %s::%s"), *TargetClass, *TargetFunction));
    }

    // Find all call nodes to redirect
    TArray<UK2Node_CallFunction*> NodesToRedirect;
    TArray<TSharedPtr<FJsonValue>> ChangesArray;

    TArray<UEdGraph*> AllGraphs;
    SourceBlueprint->GetAllGraphs(AllGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
            {
                if (UFunction* Function = CallNode->GetTargetFunction())
                {
                    if (Function->GetName() == SourceFunction)
                    {
                        NodesToRedirect.Add(CallNode);

                        TSharedPtr<FJsonObject> ChangeJson = MakeShared<FJsonObject>();
                        ChangeJson->SetStringField(TEXT("graph"), Graph->GetName());
                        ChangeJson->SetStringField(TEXT("node_guid"), CallNode->NodeGuid.ToString());
                        ChangeJson->SetStringField(TEXT("original_function"), FString::Printf(TEXT("%s::%s"),
                            Function->GetOwnerClass() ? *Function->GetOwnerClass()->GetName() : TEXT("Unknown"),
                            *Function->GetName()));
                        ChangeJson->SetStringField(TEXT("new_function"), FString::Printf(TEXT("%s::%s"),
                            *NewTargetClass->GetName(), *NewFunction->GetName()));
                        ChangeJson->SetNumberField(TEXT("pos_x"), CallNode->NodePosX);
                        ChangeJson->SetNumberField(TEXT("pos_y"), CallNode->NodePosY);
                        ChangesArray.Add(MakeShared<FJsonValueObject>(ChangeJson));
                    }
                }
            }
        }
    }

    // Build result
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("source_blueprint"), SourceBlueprint->GetPathName());
    ResultJson->SetBoolField(TEXT("dry_run"), bDryRun);
    ResultJson->SetNumberField(TEXT("nodes_found"), NodesToRedirect.Num());
    ResultJson->SetArrayField(TEXT("changes"), ChangesArray);

    if (NodesToRedirect.Num() == 0)
    {
        ResultJson->SetStringField(TEXT("message"), TEXT("No matching function calls found to redirect"));
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);
        return OutputString;
    }

    // If dry run, just return the preview
    if (bDryRun)
    {
        ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Dry run: Found %d function calls to redirect"), NodesToRedirect.Num()));
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);
        return OutputString;
    }

    // Create backup if requested
    FString BackupPath;
    if (bBackup)
    {
        FString FileName = FString::Printf(TEXT("backup_%s_%s.json"),
            *SourceBlueprint->GetName(),
            *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

        TSharedPtr<FJsonObject> BackupJson = MakeShared<FJsonObject>();
        BackupJson->SetStringField(TEXT("blueprint_path"), SourceBlueprint->GetPathName());
        BackupJson->SetStringField(TEXT("backup_time"), FDateTime::Now().ToString());
        BackupJson->SetArrayField(TEXT("original_state"), ChangesArray);

        BackupPath = WriteJsonToTempFile(FileName, BackupJson);
        ResultJson->SetStringField(TEXT("backup_path"), BackupPath);
    }

    // Apply the redirects
    int32 RedirectedCount = 0;
    for (UK2Node_CallFunction* CallNode : NodesToRedirect)
    {
        CallNode->Modify();
        CallNode->SetFromFunction(NewFunction);
        RedirectedCount++;
    }

    // Mark blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(SourceBlueprint);

    ResultJson->SetNumberField(TEXT("nodes_redirected"), RedirectedCount);
    ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully redirected %d function calls"), RedirectedCount));
    ResultJson->SetBoolField(TEXT("requires_compile"), true);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

    return OutputString;
}

bool FRedirectFunctionCallCommand::ValidateParams(const FString& Parameters) const
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Parameters);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return false;
    }

    FString SourceBlueprint, SourceFunction, TargetClass, TargetFunction;
    return JsonObject->TryGetStringField(TEXT("source_blueprint"), SourceBlueprint) && !SourceBlueprint.IsEmpty()
        && JsonObject->TryGetStringField(TEXT("source_function"), SourceFunction) && !SourceFunction.IsEmpty()
        && JsonObject->TryGetStringField(TEXT("target_class"), TargetClass) && !TargetClass.IsEmpty()
        && JsonObject->TryGetStringField(TEXT("target_function"), TargetFunction) && !TargetFunction.IsEmpty();
}

FString FRedirectFunctionCallCommand::GetExportDirectory()
{
    FString ExportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("Exports"));

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ExportDir))
    {
        PlatformFile.CreateDirectoryTree(*ExportDir);
    }

    return ExportDir;
}

FString FRedirectFunctionCallCommand::WriteJsonToTempFile(const FString& FileName, const TSharedPtr<FJsonObject>& JsonContent)
{
    FString ExportDir = GetExportDirectory();
    FString FilePath = FPaths::Combine(ExportDir, FileName);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonContent.ToSharedRef(), Writer);

    if (FFileHelper::SaveStringToFile(JsonString, *FilePath))
    {
        UE_LOG(LogMigrationRedirect, Log, TEXT("Wrote backup file: %s"), *FilePath);
        return FilePath;
    }

    UE_LOG(LogMigrationRedirect, Error, TEXT("Failed to write backup file: %s"), *FilePath);
    return FString();
}

FString FRedirectFunctionCallCommand::CreateErrorResponse(const FString& ErrorMessage) const
{
    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetBoolField(TEXT("success"), false);
    ResponseObj->SetStringField(TEXT("error"), ErrorMessage);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

    return OutputString;
}
