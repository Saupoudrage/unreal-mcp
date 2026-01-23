#include "Commands/Migration/FindBlueprintReferencesCommand.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Services/AssetDiscoveryService.h"

DEFINE_LOG_CATEGORY_STATIC(LogMigrationReferences, Log, All);

FString FFindBlueprintReferencesCommand::Execute(const FString& Parameters)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Parameters);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return CreateErrorResponse(TEXT("Invalid JSON parameters"));
    }

    FString TargetPath;
    if (!JsonObject->TryGetStringField(TEXT("target_path"), TargetPath))
    {
        return CreateErrorResponse(TEXT("Missing 'target_path' parameter"));
    }

    FString TargetFunction;
    JsonObject->TryGetStringField(TEXT("target_function"), TargetFunction);

    FString SearchScope = TEXT("project");
    JsonObject->TryGetStringField(TEXT("search_scope"), SearchScope);

    bool bIncludeSoftReferences = true;
    JsonObject->TryGetBoolField(TEXT("include_soft_references"), bIncludeSoftReferences);

    // Get asset registry
    IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    // Try to load the target Blueprint
    UBlueprint* TargetBlueprint = LoadObject<UBlueprint>(nullptr, *TargetPath);
    if (!TargetBlueprint)
    {
        TArray<FString> FoundBlueprints = FAssetDiscoveryService::Get().FindBlueprints(TargetPath);
        if (FoundBlueprints.Num() > 0)
        {
            TargetBlueprint = LoadObject<UBlueprint>(nullptr, *FoundBlueprints[0]);
        }
    }

    // Get referencers
    TArray<FAssetIdentifier> Referencers;
    FAssetIdentifier TargetId;

    if (TargetBlueprint)
    {
        TargetId = FAssetIdentifier(TargetBlueprint->GetPackage()->GetFName());
    }
    else
    {
        // Try as package name
        TargetId = FAssetIdentifier(FName(*TargetPath));
    }

    AssetRegistry.GetReferencers(TargetId, Referencers);

    // Build detailed reference list
    TArray<TSharedPtr<FJsonValue>> ReferencersArray;

    for (const FAssetIdentifier& RefId : Referencers)
    {
        FString RefPath = RefId.PackageName.ToString();

        // Skip engine/script packages
        if (RefPath.StartsWith(TEXT("/Script/")) || RefPath.StartsWith(TEXT("/Engine/")))
        {
            continue;
        }

        TSharedPtr<FJsonObject> RefJson = MakeShared<FJsonObject>();
        RefJson->SetStringField(TEXT("referencer_path"), RefPath);

        // Try to get more details about how it's referenced
        UBlueprint* ReferencerBP = LoadObject<UBlueprint>(nullptr, *RefPath);
        if (ReferencerBP)
        {
            RefJson->SetStringField(TEXT("referencer_name"), ReferencerBP->GetName());
            RefJson->SetStringField(TEXT("type"), TEXT("Blueprint"));

            // If looking for function references, scan the graphs
            if (!TargetFunction.IsEmpty())
            {
                TArray<TSharedPtr<FJsonValue>> LocationsArray;

                TArray<UEdGraph*> AllGraphs;
                ReferencerBP->GetAllGraphs(AllGraphs);

                for (UEdGraph* Graph : AllGraphs)
                {
                    if (!Graph) continue;

                    for (UEdGraphNode* Node : Graph->Nodes)
                    {
                        if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
                        {
                            if (UFunction* Function = CallNode->GetTargetFunction())
                            {
                                if (Function->GetName() == TargetFunction)
                                {
                                    TSharedPtr<FJsonObject> LocJson = MakeShared<FJsonObject>();
                                    LocJson->SetStringField(TEXT("graph"), Graph->GetName());
                                    LocJson->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
                                    LocJson->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                                    LocJson->SetNumberField(TEXT("pos_x"), Node->NodePosX);
                                    LocJson->SetNumberField(TEXT("pos_y"), Node->NodePosY);
                                    LocationsArray.Add(MakeShared<FJsonValueObject>(LocJson));
                                }
                            }
                        }
                    }
                }

                RefJson->SetArrayField(TEXT("reference_locations"), LocationsArray);
            }
        }
        else
        {
            RefJson->SetStringField(TEXT("type"), TEXT("Asset"));
        }

        ReferencersArray.Add(MakeShared<FJsonValueObject>(RefJson));
    }

    // Build response
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("target_path"), TargetPath);
    if (!TargetFunction.IsEmpty())
    {
        ResultJson->SetStringField(TEXT("target_function"), TargetFunction);
    }
    ResultJson->SetNumberField(TEXT("referencer_count"), ReferencersArray.Num());
    ResultJson->SetArrayField(TEXT("referencers"), ReferencersArray);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

    return OutputString;
}

bool FFindBlueprintReferencesCommand::ValidateParams(const FString& Parameters) const
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Parameters);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return false;
    }

    FString TargetPath;
    return JsonObject->TryGetStringField(TEXT("target_path"), TargetPath) && !TargetPath.IsEmpty();
}

FString FFindBlueprintReferencesCommand::CreateErrorResponse(const FString& ErrorMessage) const
{
    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetBoolField(TEXT("success"), false);
    ResponseObj->SetStringField(TEXT("error"), ErrorMessage);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

    return OutputString;
}
