#pragma once

#include "CoreMinimal.h"
#include "Commands/IUnrealMCPCommand.h"

/**
 * Command for exporting complete Blueprint graphs to JSON files.
 * Outputs to Saved/UnrealMCP/Exports/ to avoid socket buffer issues with complex Blueprints.
 *
 * Parameters:
 *   - blueprint_path (string, required): Path to the Blueprint
 *   - graph_name (string, optional): Filter to specific graph name
 *   - include_components (bool, optional): Include component hierarchy (default: true)
 *   - include_defaults (bool, optional): Include default values (default: false)
 *
 * Returns:
 *   - success (bool): Whether the export succeeded
 *   - file_path (string): Full path to the exported JSON file
 *   - graph_count (int): Number of graphs exported
 *   - node_count (int): Total number of nodes exported
 */
class UNREALMCP_API FExportBlueprintGraphCommand : public IUnrealMCPCommand
{
public:
    FExportBlueprintGraphCommand() = default;

    // IUnrealMCPCommand interface
    virtual FString Execute(const FString& Parameters) override;
    virtual FString GetCommandName() const override { return TEXT("export_blueprint_graph"); }
    virtual bool ValidateParams(const FString& Parameters) const override;

private:
    /**
     * Serialize a Blueprint graph to JSON.
     */
    TSharedPtr<FJsonObject> SerializeGraph(class UEdGraph* Graph, bool bIncludeDefaults);

    /**
     * Serialize a graph node to JSON.
     */
    TSharedPtr<FJsonObject> SerializeNode(class UEdGraphNode* Node);

    /**
     * Serialize a pin to JSON.
     */
    TSharedPtr<FJsonObject> SerializePin(class UEdGraphPin* Pin, bool bIncludeConnections);

    /**
     * Get the export directory path.
     */
    FString GetExportDirectory();

    /**
     * Generate a timestamped filename for exports.
     */
    FString GenerateExportFileName(const FString& BlueprintName);

    /**
     * Write JSON content to a temp file in Saved/UnrealMCP/Exports/
     */
    FString WriteJsonToTempFile(const FString& FileName, const TSharedPtr<FJsonObject>& JsonContent);

    /**
     * Create success response JSON
     */
    FString CreateSuccessResponse(const FString& FilePath, int32 GraphCount, int32 NodeCount) const;

    /**
     * Create error response JSON
     */
    FString CreateErrorResponse(const FString& ErrorMessage) const;
};
