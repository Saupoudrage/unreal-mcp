#include "Commands/Migration/SetBlueprintParentClassCommand.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Services/AssetDiscoveryService.h"

DEFINE_LOG_CATEGORY_STATIC(LogMigrationReparent, Log, All);

FString FSetBlueprintParentClassCommand::Execute(const FString& Parameters)
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

    FString NewParentClassName;
    if (!JsonObject->TryGetStringField(TEXT("new_parent_class"), NewParentClassName))
    {
        return CreateErrorResponse(TEXT("Missing 'new_parent_class' parameter"));
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

    // Store old parent class name
    FString OldParentClassName = Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None");
    FString OldParentClassPath = Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT("");

    // Find new parent class using asset discovery service
    FString ErrorMessage;
    UClass* NewParentClass = FAssetDiscoveryService::Get().ResolveParentClassForBlueprint(NewParentClassName, ErrorMessage);

    // Try additional resolution methods if not found
    if (!NewParentClass)
    {
        NewParentClass = FindObject<UClass>(nullptr, *NewParentClassName);
    }
    if (!NewParentClass)
    {
        NewParentClass = LoadClass<UObject>(nullptr, *NewParentClassName);
    }
    // Try with common prefixes
    if (!NewParentClass)
    {
        NewParentClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("A%s"), *NewParentClassName));
    }
    if (!NewParentClass)
    {
        NewParentClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("U%s"), *NewParentClassName));
    }

    if (!NewParentClass)
    {
        return CreateErrorResponse(FString::Printf(TEXT("Parent class not found: %s"), *NewParentClassName));
    }

    // Verify the new parent is compatible
    if (Blueprint->ParentClass && !NewParentClass->IsChildOf(Blueprint->ParentClass->GetSuperClass()))
    {
        UE_LOG(LogMigrationReparent, Warning, TEXT("Reparenting to potentially incompatible class: %s -> %s"), *OldParentClassName, *NewParentClass->GetName());
    }

    // Create backup if requested
    FString BackupPath;
    if (bBackup)
    {
        TSharedPtr<FJsonObject> BackupJson = MakeShared<FJsonObject>();
        BackupJson->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
        BackupJson->SetStringField(TEXT("old_parent_class"), OldParentClassName);
        BackupJson->SetStringField(TEXT("old_parent_class_path"), OldParentClassPath);
        BackupJson->SetStringField(TEXT("new_parent_class"), NewParentClass->GetName());
        BackupJson->SetStringField(TEXT("backup_time"), FDateTime::Now().ToString());

        FString FileName = FString::Printf(TEXT("backup_reparent_%s_%s.json"),
            *Blueprint->GetName(),
            *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

        BackupPath = WriteJsonToTempFile(FileName, BackupJson);
    }

    // Reparent the Blueprint
    Blueprint->Modify();
    Blueprint->ParentClass = NewParentClass;

    // Refresh the Blueprint
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    // Build result
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultJson->SetStringField(TEXT("old_parent_class"), OldParentClassName);
    ResultJson->SetStringField(TEXT("new_parent_class"), NewParentClass->GetName());
    ResultJson->SetStringField(TEXT("new_parent_class_path"), NewParentClass->GetPathName());

    if (!BackupPath.IsEmpty())
    {
        ResultJson->SetStringField(TEXT("backup_path"), BackupPath);
    }

    ResultJson->SetBoolField(TEXT("requires_compile"), true);
    ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully reparented Blueprint from '%s' to '%s'"), *OldParentClassName, *NewParentClass->GetName()));

    UE_LOG(LogMigrationReparent, Log, TEXT("Reparented Blueprint '%s' from '%s' to '%s'"), *Blueprint->GetName(), *OldParentClassName, *NewParentClass->GetName());

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

    return OutputString;
}

bool FSetBlueprintParentClassCommand::ValidateParams(const FString& Parameters) const
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Parameters);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return false;
    }

    FString BlueprintPath, NewParentClass;
    return JsonObject->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) && !BlueprintPath.IsEmpty()
        && JsonObject->TryGetStringField(TEXT("new_parent_class"), NewParentClass) && !NewParentClass.IsEmpty();
}

FString FSetBlueprintParentClassCommand::GetExportDirectory()
{
    FString ExportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("Exports"));

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ExportDir))
    {
        PlatformFile.CreateDirectoryTree(*ExportDir);
    }

    return ExportDir;
}

FString FSetBlueprintParentClassCommand::WriteJsonToTempFile(const FString& FileName, const TSharedPtr<FJsonObject>& JsonContent)
{
    FString ExportDir = GetExportDirectory();
    FString FilePath = FPaths::Combine(ExportDir, FileName);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonContent.ToSharedRef(), Writer);

    if (FFileHelper::SaveStringToFile(JsonString, *FilePath))
    {
        UE_LOG(LogMigrationReparent, Log, TEXT("Wrote backup file: %s"), *FilePath);
        return FilePath;
    }

    UE_LOG(LogMigrationReparent, Error, TEXT("Failed to write backup file: %s"), *FilePath);
    return FString();
}

FString FSetBlueprintParentClassCommand::CreateErrorResponse(const FString& ErrorMessage) const
{
    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetBoolField(TEXT("success"), false);
    ResponseObj->SetStringField(TEXT("error"), ErrorMessage);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

    return OutputString;
}
