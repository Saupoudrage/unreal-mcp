#include "Commands/UnrealMCPBlueprintMigrationCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Composite.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EditorAssetLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPMigration, Log, All);

FUnrealMCPBlueprintMigrationCommands::FUnrealMCPBlueprintMigrationCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("export_blueprint_graph"))
    {
        return HandleExportBlueprintGraph(Params);
    }
    else if (CommandType == TEXT("get_blueprint_dependencies"))
    {
        return HandleGetBlueprintDependencies(Params);
    }
    else if (CommandType == TEXT("find_blueprint_references"))
    {
        return HandleFindBlueprintReferences(Params);
    }
    else if (CommandType == TEXT("redirect_function_call"))
    {
        return HandleRedirectFunctionCall(Params);
    }
    else if (CommandType == TEXT("delete_blueprint_function"))
    {
        return HandleDeleteBlueprintFunction(Params);
    }
    else if (CommandType == TEXT("set_blueprint_parent_class"))
    {
        return HandleSetBlueprintParentClass(Params);
    }
    else if (CommandType == TEXT("get_blueprint_functions"))
    {
        return HandleGetBlueprintFunctions(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown migration command: %s"), *CommandType));
}

FString FUnrealMCPBlueprintMigrationCommands::GetExportDirectory()
{
    FString ExportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("Exports"));

    // Ensure directory exists
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ExportDir))
    {
        PlatformFile.CreateDirectoryTree(*ExportDir);
    }

    return ExportDir;
}

FString FUnrealMCPBlueprintMigrationCommands::GenerateExportFileName(const FString& BlueprintName)
{
    FDateTime Now = FDateTime::Now();
    return FString::Printf(TEXT("export_%s_%04d%02d%02d_%02d%02d%02d.json"),
        *BlueprintName,
        Now.GetYear(), Now.GetMonth(), Now.GetDay(),
        Now.GetHour(), Now.GetMinute(), Now.GetSecond());
}

FString FUnrealMCPBlueprintMigrationCommands::WriteJsonToTempFile(const FString& FileName, const TSharedPtr<FJsonObject>& JsonContent)
{
    FString ExportDir = GetExportDirectory();
    FString FilePath = FPaths::Combine(ExportDir, FileName);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonContent.ToSharedRef(), Writer);

    if (FFileHelper::SaveStringToFile(JsonString, *FilePath))
    {
        UE_LOG(LogUnrealMCPMigration, Log, TEXT("Wrote export file: %s"), *FilePath);
        return FilePath;
    }

    UE_LOG(LogUnrealMCPMigration, Error, TEXT("Failed to write export file: %s"), *FilePath);
    return FString();
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::SerializePin(UEdGraphPin* Pin, bool bIncludeConnections)
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

    PinJson->SetBoolField(TEXT("is_array"), Pin->PinType.IsArray());
    PinJson->SetBoolField(TEXT("is_reference"), Pin->PinType.bIsReference);
    PinJson->SetBoolField(TEXT("is_const"), Pin->PinType.bIsConst);

    if (!Pin->DefaultValue.IsEmpty())
    {
        PinJson->SetStringField(TEXT("default_value"), Pin->DefaultValue);
    }

    if (Pin->DefaultObject)
    {
        PinJson->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
    }

    if (!Pin->DefaultTextValue.IsEmpty())
    {
        PinJson->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());
    }

    // Include connections if requested
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

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::SerializeNode(UEdGraphNode* Node)
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
    NodeJson->SetStringField(TEXT("comment"), Node->NodeComment);
    NodeJson->SetBoolField(TEXT("comment_bubble_visible"), Node->bCommentBubbleVisible);

    // Handle specific node types
    if (UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("CallFunction"));

        if (UFunction* Function = CallFuncNode->GetTargetFunction())
        {
            NodeJson->SetStringField(TEXT("function_name"), Function->GetName());
            if (Function->GetOwnerClass())
            {
                NodeJson->SetStringField(TEXT("function_class"), Function->GetOwnerClass()->GetName());
                NodeJson->SetStringField(TEXT("function_class_path"), Function->GetOwnerClass()->GetPathName());
            }
        }

        NodeJson->SetBoolField(TEXT("is_pure"), CallFuncNode->IsNodePure());
    }
    else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("Event"));
        NodeJson->SetStringField(TEXT("event_name"), EventNode->EventReference.GetMemberName().ToString());

        if (EventNode->EventReference.GetMemberParentClass())
        {
            NodeJson->SetStringField(TEXT("event_class"), EventNode->EventReference.GetMemberParentClass()->GetName());
        }
    }
    else if (UK2Node_VariableGet* VarGetNode = Cast<UK2Node_VariableGet>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("VariableGet"));
        NodeJson->SetStringField(TEXT("variable_name"), VarGetNode->VariableReference.GetMemberName().ToString());
    }
    else if (UK2Node_VariableSet* VarSetNode = Cast<UK2Node_VariableSet>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("VariableSet"));
        NodeJson->SetStringField(TEXT("variable_name"), VarSetNode->VariableReference.GetMemberName().ToString());
    }
    else if (UK2Node_InputAction* InputNode = Cast<UK2Node_InputAction>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("InputAction"));
        NodeJson->SetStringField(TEXT("action_name"), InputNode->InputActionName.ToString());
    }
    else if (UK2Node_Self* SelfNode = Cast<UK2Node_Self>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("Self"));
    }
    else if (UK2Node_FunctionEntry* FuncEntryNode = Cast<UK2Node_FunctionEntry>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("FunctionEntry"));
    }
    else if (UK2Node_FunctionResult* FuncResultNode = Cast<UK2Node_FunctionResult>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("FunctionResult"));
    }
    else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("MacroInstance"));
        if (MacroNode->GetMacroGraph())
        {
            NodeJson->SetStringField(TEXT("macro_name"), MacroNode->GetMacroGraph()->GetName());
        }
    }
    else
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("Other"));
    }

    // Serialize pins
    TArray<TSharedPtr<FJsonValue>> InputPinsArray;
    TArray<TSharedPtr<FJsonValue>> OutputPinsArray;

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin)
        {
            TSharedPtr<FJsonObject> PinJson = SerializePin(Pin, true);
            if (PinJson.IsValid())
            {
                if (Pin->Direction == EGPD_Input)
                {
                    InputPinsArray.Add(MakeShared<FJsonValueObject>(PinJson));
                }
                else
                {
                    OutputPinsArray.Add(MakeShared<FJsonValueObject>(PinJson));
                }
            }
        }
    }

    NodeJson->SetArrayField(TEXT("input_pins"), InputPinsArray);
    NodeJson->SetArrayField(TEXT("output_pins"), OutputPinsArray);

    return NodeJson;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::SerializeGraph(UEdGraph* Graph, bool bIncludeDefaults)
{
    if (!Graph)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();

    GraphJson->SetStringField(TEXT("name"), Graph->GetName());
    GraphJson->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
    GraphJson->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

    // Serialize all nodes
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

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::HandleExportBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Try to load Blueprint by path or name
    UBlueprint* Blueprint = nullptr;

    // First try as full path
    Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);

    // If that fails, try as simple name in /Game/Blueprints/
    if (!Blueprint)
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintPath);
    }

    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    // Get optional parameters
    FString GraphName;
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    bool bIncludeComponents = true;
    Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);

    bool bIncludeDefaults = false;
    Params->TryGetBoolField(TEXT("include_defaults"), bIncludeDefaults);

    // Build export JSON
    TSharedPtr<FJsonObject> ExportJson = MakeShared<FJsonObject>();
    ExportJson->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ExportJson->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

    if (Blueprint->ParentClass)
    {
        ExportJson->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
        ExportJson->SetStringField(TEXT("parent_class_path"), Blueprint->ParentClass->GetPathName());
    }

    // Get all graphs
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);

    TArray<TSharedPtr<FJsonValue>> GraphsArray;
    int32 TotalNodeCount = 0;
    int32 GraphCount = 0;

    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph)
        {
            // Filter by graph name if specified
            if (!GraphName.IsEmpty() && !Graph->GetName().Contains(GraphName))
            {
                continue;
            }

            TSharedPtr<FJsonObject> GraphJson = SerializeGraph(Graph, bIncludeDefaults);
            if (GraphJson.IsValid())
            {
                GraphsArray.Add(MakeShared<FJsonValueObject>(GraphJson));
                TotalNodeCount += Graph->Nodes.Num();
                GraphCount++;
            }
        }
    }
    ExportJson->SetArrayField(TEXT("graphs"), GraphsArray);

    // Include components if requested
    if (bIncludeComponents && Blueprint->SimpleConstructionScript)
    {
        TArray<TSharedPtr<FJsonValue>> ComponentsArray;

        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->ComponentTemplate)
            {
                TSharedPtr<FJsonObject> CompJson = MakeShared<FJsonObject>();
                CompJson->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
                CompJson->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());

                if (Node->ParentComponentOrVariableName != NAME_None)
                {
                    CompJson->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
                }

                ComponentsArray.Add(MakeShared<FJsonValueObject>(CompJson));
            }
        }

        ExportJson->SetArrayField(TEXT("components"), ComponentsArray);
    }

    // Include variables
    TArray<TSharedPtr<FJsonValue>> VariablesArray;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        TSharedPtr<FJsonObject> VarJson = MakeShared<FJsonObject>();
        VarJson->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarJson->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());

        if (Var.VarType.PinSubCategoryObject.IsValid())
        {
            VarJson->SetStringField(TEXT("subtype"), Var.VarType.PinSubCategoryObject->GetName());
        }

        VarJson->SetBoolField(TEXT("is_exposed"), (Var.PropertyFlags & CPF_Edit) != 0);
        VariablesArray.Add(MakeShared<FJsonValueObject>(VarJson));
    }
    ExportJson->SetArrayField(TEXT("variables"), VariablesArray);

    // Write to file
    FString FileName = GenerateExportFileName(Blueprint->GetName());
    FString FilePath = WriteJsonToTempFile(FileName, ExportJson);

    if (FilePath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to write export file"));
    }

    // Return response
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("file_path"), FilePath);
    ResultJson->SetNumberField(TEXT("graph_count"), GraphCount);
    ResultJson->SetNumberField(TEXT("node_count"), TotalNodeCount);

    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::HandleGetBlueprintDependencies(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Try to load Blueprint
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!Blueprint)
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintPath);
    }

    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    bool bIncludeEngineClasses = false;
    Params->TryGetBoolField(TEXT("include_engine_classes"), bIncludeEngineClasses);

    bool bRecursive = true;
    Params->TryGetBoolField(TEXT("recursive"), bRecursive);

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

    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::HandleFindBlueprintReferences(const TSharedPtr<FJsonObject>& Params)
{
    FString TargetPath;
    if (!Params->TryGetStringField(TEXT("target_path"), TargetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_path' parameter"));
    }

    FString TargetFunction;
    Params->TryGetStringField(TEXT("target_function"), TargetFunction);

    FString SearchScope = TEXT("project");
    Params->TryGetStringField(TEXT("search_scope"), SearchScope);

    bool bIncludeSoftReferences = true;
    Params->TryGetBoolField(TEXT("include_soft_references"), bIncludeSoftReferences);

    // Get asset registry
    IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    // Try to load the target Blueprint
    UBlueprint* TargetBlueprint = LoadObject<UBlueprint>(nullptr, *TargetPath);
    if (!TargetBlueprint)
    {
        TargetBlueprint = FUnrealMCPCommonUtils::FindBlueprint(TargetPath);
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
    ResultJson->SetStringField(TEXT("target_path"), TargetPath);
    if (!TargetFunction.IsEmpty())
    {
        ResultJson->SetStringField(TEXT("target_function"), TargetFunction);
    }
    ResultJson->SetNumberField(TEXT("referencer_count"), ReferencersArray.Num());
    ResultJson->SetArrayField(TEXT("referencers"), ReferencersArray);

    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::HandleRedirectFunctionCall(const TSharedPtr<FJsonObject>& Params)
{
    FString SourceBlueprintPath;
    if (!Params->TryGetStringField(TEXT("source_blueprint"), SourceBlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_blueprint' parameter"));
    }

    FString SourceFunction;
    if (!Params->TryGetStringField(TEXT("source_function"), SourceFunction))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_function' parameter"));
    }

    FString TargetClass;
    if (!Params->TryGetStringField(TEXT("target_class"), TargetClass))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_class' parameter"));
    }

    FString TargetFunction;
    if (!Params->TryGetStringField(TEXT("target_function"), TargetFunction))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_function' parameter"));
    }

    bool bDryRun = true;
    Params->TryGetBoolField(TEXT("dry_run"), bDryRun);

    bool bBackup = true;
    Params->TryGetBoolField(TEXT("backup"), bBackup);

    // Load source Blueprint
    UBlueprint* SourceBlueprint = LoadObject<UBlueprint>(nullptr, *SourceBlueprintPath);
    if (!SourceBlueprint)
    {
        SourceBlueprint = FUnrealMCPCommonUtils::FindBlueprint(SourceBlueprintPath);
    }

    if (!SourceBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source Blueprint not found: %s"), *SourceBlueprintPath));
    }

    // Find target class and function
    UClass* NewTargetClass = FindObject<UClass>(nullptr, *TargetClass);
    if (!NewTargetClass)
    {
        // Try with different paths
        NewTargetClass = LoadClass<UObject>(nullptr, *TargetClass);
    }

    if (!NewTargetClass)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Target class not found: %s"), *TargetClass));
    }

    UFunction* NewFunction = NewTargetClass->FindFunctionByName(FName(*TargetFunction));
    if (!NewFunction)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Target function not found: %s::%s"), *TargetClass, *TargetFunction));
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
    ResultJson->SetStringField(TEXT("source_blueprint"), SourceBlueprint->GetPathName());
    ResultJson->SetBoolField(TEXT("dry_run"), bDryRun);
    ResultJson->SetNumberField(TEXT("nodes_found"), NodesToRedirect.Num());
    ResultJson->SetArrayField(TEXT("changes"), ChangesArray);

    if (NodesToRedirect.Num() == 0)
    {
        ResultJson->SetStringField(TEXT("message"), TEXT("No matching function calls found to redirect"));
        return ResultJson;
    }

    // If dry run, just return the preview
    if (bDryRun)
    {
        ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Dry run: Found %d function calls to redirect"), NodesToRedirect.Num()));
        return ResultJson;
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

    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::HandleDeleteBlueprintFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    bool bBackup = true;
    Params->TryGetBoolField(TEXT("backup"), bBackup);

    // Load Blueprint
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!Blueprint)
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintPath);
    }

    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
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
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
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

    UE_LOG(LogUnrealMCPMigration, Log, TEXT("Deleted function '%s' from Blueprint '%s' (%d nodes)"), *FunctionName, *Blueprint->GetName(), NodeCount);

    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::HandleSetBlueprintParentClass(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString NewParentClassName;
    if (!Params->TryGetStringField(TEXT("new_parent_class"), NewParentClassName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_parent_class' parameter"));
    }

    bool bBackup = true;
    Params->TryGetBoolField(TEXT("backup"), bBackup);

    // Load Blueprint
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!Blueprint)
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintPath);
    }

    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    // Store old parent class name
    FString OldParentClassName = Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None");
    FString OldParentClassPath = Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT("");

    // Find new parent class
    UClass* NewParentClass = FindObject<UClass>(nullptr, *NewParentClassName);
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
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent class not found: %s"), *NewParentClassName));
    }

    // Verify the new parent is compatible
    if (Blueprint->ParentClass && !NewParentClass->IsChildOf(Blueprint->ParentClass->GetSuperClass()))
    {
        // Check if the change is safe (new class should be related to current hierarchy)
        UE_LOG(LogUnrealMCPMigration, Warning, TEXT("Reparenting to potentially incompatible class: %s -> %s"), *OldParentClassName, *NewParentClass->GetName());
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

    UE_LOG(LogUnrealMCPMigration, Log, TEXT("Reparented Blueprint '%s' from '%s' to '%s'"), *Blueprint->GetName(), *OldParentClassName, *NewParentClass->GetName());

    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintMigrationCommands::HandleGetBlueprintFunctions(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    bool bIncludeInherited = false;
    Params->TryGetBoolField(TEXT("include_inherited"), bIncludeInherited);

    // Load Blueprint
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!Blueprint)
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintPath);
    }

    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
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

    return ResultJson;
}
