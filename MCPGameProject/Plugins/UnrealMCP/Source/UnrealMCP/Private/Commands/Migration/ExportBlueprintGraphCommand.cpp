#include "Commands/Migration/ExportBlueprintGraphCommand.h"
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
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Services/AssetDiscoveryService.h"

DEFINE_LOG_CATEGORY_STATIC(LogMigrationExport, Log, All);

FString FExportBlueprintGraphCommand::Execute(const FString& Parameters)
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

    // Try to load Blueprint by path or name
    UBlueprint* Blueprint = nullptr;

    // First try as full path
    Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);

    // If that fails, try to find by name using discovery service
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

    // Get optional parameters
    FString GraphName;
    JsonObject->TryGetStringField(TEXT("graph_name"), GraphName);

    bool bIncludeComponents = true;
    JsonObject->TryGetBoolField(TEXT("include_components"), bIncludeComponents);

    bool bIncludeDefaults = false;
    JsonObject->TryGetBoolField(TEXT("include_defaults"), bIncludeDefaults);

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
        return CreateErrorResponse(TEXT("Failed to write export file"));
    }

    return CreateSuccessResponse(FilePath, GraphCount, TotalNodeCount);
}

bool FExportBlueprintGraphCommand::ValidateParams(const FString& Parameters) const
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

FString FExportBlueprintGraphCommand::GetExportDirectory()
{
    FString ExportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("Exports"));

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ExportDir))
    {
        PlatformFile.CreateDirectoryTree(*ExportDir);
    }

    return ExportDir;
}

FString FExportBlueprintGraphCommand::GenerateExportFileName(const FString& BlueprintName)
{
    FDateTime Now = FDateTime::Now();
    return FString::Printf(TEXT("export_%s_%04d%02d%02d_%02d%02d%02d.json"),
        *BlueprintName,
        Now.GetYear(), Now.GetMonth(), Now.GetDay(),
        Now.GetHour(), Now.GetMinute(), Now.GetSecond());
}

FString FExportBlueprintGraphCommand::WriteJsonToTempFile(const FString& FileName, const TSharedPtr<FJsonObject>& JsonContent)
{
    FString ExportDir = GetExportDirectory();
    FString FilePath = FPaths::Combine(ExportDir, FileName);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonContent.ToSharedRef(), Writer);

    if (FFileHelper::SaveStringToFile(JsonString, *FilePath))
    {
        UE_LOG(LogMigrationExport, Log, TEXT("Wrote export file: %s"), *FilePath);
        return FilePath;
    }

    UE_LOG(LogMigrationExport, Error, TEXT("Failed to write export file: %s"), *FilePath);
    return FString();
}

TSharedPtr<FJsonObject> FExportBlueprintGraphCommand::SerializePin(UEdGraphPin* Pin, bool bIncludeConnections)
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

TSharedPtr<FJsonObject> FExportBlueprintGraphCommand::SerializeNode(UEdGraphNode* Node)
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
    else if (Cast<UK2Node_Self>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("Self"));
    }
    else if (Cast<UK2Node_FunctionEntry>(Node))
    {
        NodeJson->SetStringField(TEXT("node_type"), TEXT("FunctionEntry"));
    }
    else if (Cast<UK2Node_FunctionResult>(Node))
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

TSharedPtr<FJsonObject> FExportBlueprintGraphCommand::SerializeGraph(UEdGraph* Graph, bool bIncludeDefaults)
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

FString FExportBlueprintGraphCommand::CreateSuccessResponse(const FString& FilePath, int32 GraphCount, int32 NodeCount) const
{
    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetBoolField(TEXT("success"), true);
    ResponseObj->SetStringField(TEXT("file_path"), FilePath);
    ResponseObj->SetNumberField(TEXT("graph_count"), GraphCount);
    ResponseObj->SetNumberField(TEXT("node_count"), NodeCount);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

    return OutputString;
}

FString FExportBlueprintGraphCommand::CreateErrorResponse(const FString& ErrorMessage) const
{
    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetBoolField(TEXT("success"), false);
    ResponseObj->SetStringField(TEXT("error"), ErrorMessage);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

    return OutputString;
}
