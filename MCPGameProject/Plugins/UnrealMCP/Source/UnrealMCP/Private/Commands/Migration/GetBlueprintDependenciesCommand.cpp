#include "Commands/Migration/GetBlueprintDependenciesCommand.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogMigrationDependencies, Log, All);

FString FGetBlueprintDependenciesCommand::Execute(const FString& Parameters)
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

    // Try to load Blueprint
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

    bool bIncludeEngineClasses = false;
    JsonObject->TryGetBoolField(TEXT("include_engine_classes"), bIncludeEngineClasses);

    bool bRecursive = true;
    JsonObject->TryGetBoolField(TEXT("recursive"), bRecursive);

    // Get asset registry
    IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    // Collect dependencies
    TSet<FString> AssetDependencies;
    TSet<FString> BlueprintDependencies;
    TSet<FString> NativeClasses;
    TMap<FString, int32> FunctionCalls;

    // Get hard references
    TArray<FAssetIdentifier> Dependencies;
    AssetRegistry.GetDependencies(FAssetIdentifier(Blueprint->GetPackage()->GetFName()), Dependencies);

    for (const FAssetIdentifier& Dep : Dependencies)
    {
        FString DepPath = Dep.PackageName.ToString();

        // Skip engine content unless requested
        if (!bIncludeEngineClasses && (DepPath.StartsWith(TEXT("/Script/")) || DepPath.StartsWith(TEXT("/Engine/"))))
        {
            continue;
        }

        // Categorize the dependency
        if (DepPath.StartsWith(TEXT("/Script/")))
        {
            NativeClasses.Add(DepPath);
        }
        else
        {
            // Try to determine if it's a Blueprint or other asset
            FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(DepPath));
            if (AssetData.IsValid())
            {
                if (AssetData.AssetClassPath.GetAssetName() == TEXT("Blueprint"))
                {
                    BlueprintDependencies.Add(DepPath);
                }
                else
                {
                    AssetDependencies.Add(DepPath);
                }
            }
            else
            {
                AssetDependencies.Add(DepPath);
            }
        }
    }

    // Analyze function calls in graphs
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
            {
                if (UFunction* Function = CallNode->GetTargetFunction())
                {
                    FString FunctionKey = FString::Printf(TEXT("%s::%s"),
                        Function->GetOwnerClass() ? *Function->GetOwnerClass()->GetName() : TEXT("Unknown"),
                        *Function->GetName());

                    if (bIncludeEngineClasses || !FunctionKey.StartsWith(TEXT("U")) || !Function->GetOwnerClass() ||
                        !Function->GetOwnerClass()->GetPathName().StartsWith(TEXT("/Script/Engine")))
                    {
                        int32& Count = FunctionCalls.FindOrAdd(FunctionKey);
                        Count++;
                    }
                }
            }
        }
    }

    // Build response
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

    // Assets
    TArray<TSharedPtr<FJsonValue>> AssetsArray;
    for (const FString& Asset : AssetDependencies)
    {
        AssetsArray.Add(MakeShared<FJsonValueString>(Asset));
    }
    ResultJson->SetArrayField(TEXT("assets"), AssetsArray);

    // Blueprints
    TArray<TSharedPtr<FJsonValue>> BlueprintsArray;
    for (const FString& BP : BlueprintDependencies)
    {
        BlueprintsArray.Add(MakeShared<FJsonValueString>(BP));
    }
    ResultJson->SetArrayField(TEXT("blueprints"), BlueprintsArray);

    // Native classes
    TArray<TSharedPtr<FJsonValue>> NativeArray;
    for (const FString& Native : NativeClasses)
    {
        NativeArray.Add(MakeShared<FJsonValueString>(Native));
    }
    ResultJson->SetArrayField(TEXT("native_classes"), NativeArray);

    // Function calls with counts
    TArray<TSharedPtr<FJsonValue>> FunctionsArray;
    for (const TPair<FString, int32>& Pair : FunctionCalls)
    {
        TSharedPtr<FJsonObject> FuncJson = MakeShared<FJsonObject>();
        FuncJson->SetStringField(TEXT("function"), Pair.Key);
        FuncJson->SetNumberField(TEXT("call_count"), Pair.Value);
        FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncJson));
    }
    ResultJson->SetArrayField(TEXT("function_calls"), FunctionsArray);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

    return OutputString;
}

bool FGetBlueprintDependenciesCommand::ValidateParams(const FString& Parameters) const
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

FString FGetBlueprintDependenciesCommand::CreateErrorResponse(const FString& ErrorMessage) const
{
    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetBoolField(TEXT("success"), false);
    ResponseObj->SetStringField(TEXT("error"), ErrorMessage);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

    return OutputString;
}
