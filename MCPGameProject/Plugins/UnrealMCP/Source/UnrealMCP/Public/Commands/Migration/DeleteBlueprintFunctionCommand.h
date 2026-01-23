#pragma once

#include "CoreMinimal.h"
#include "Commands/IUnrealMCPCommand.h"

/**
 * Command for deleting a function graph from a Blueprint.
 * Use this during Blueprint cleanup to remove function graphs that have been migrated to C++.
 * Always creates a backup by default.
 *
 * Parameters:
 *   - blueprint_path (string, required): Path to the Blueprint to modify
 *   - function_name (string, required): Name of the function graph to delete
 *   - backup (bool, optional): If true, create backup before changes (default: true)
 *
 * Returns:
 *   - success (bool): Whether the deletion succeeded
 *   - blueprint_path (string): The modified Blueprint's path
 *   - function_name (string): The deleted function name
 *   - nodes_removed (int): Number of nodes removed
 *   - backup_path (string): Path to backup file (if backup=true)
 *   - requires_compile (bool): Whether Blueprint needs recompile
 *   - message (string): Summary message
 */
class UNREALMCP_API FDeleteBlueprintFunctionCommand : public IUnrealMCPCommand
{
public:
    FDeleteBlueprintFunctionCommand() = default;

    // IUnrealMCPCommand interface
    virtual FString Execute(const FString& Parameters) override;
    virtual FString GetCommandName() const override { return TEXT("delete_blueprint_function"); }
    virtual bool ValidateParams(const FString& Parameters) const override;

private:
    /**
     * Get the export directory path for backups.
     */
    FString GetExportDirectory();

    /**
     * Write JSON content to a temp file.
     */
    FString WriteJsonToTempFile(const FString& FileName, const TSharedPtr<FJsonObject>& JsonContent);

    /**
     * Serialize a Blueprint graph to JSON for backup.
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
     * Create error response JSON
     */
    FString CreateErrorResponse(const FString& ErrorMessage) const;
};
