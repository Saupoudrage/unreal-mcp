#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Blueprint migration commands.
 * Provides tools for analyzing Blueprints and migrating functionality to C++.
 */
class UNREALMCP_API FUnrealMCPBlueprintMigrationCommands
{
public:
    FUnrealMCPBlueprintMigrationCommands();

    // Handle migration commands
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    // Command handlers

    /**
     * Export complete Blueprint graph to JSON file.
     * Outputs to Saved/UnrealMCP/Exports/ to avoid socket buffer issues.
     */
    TSharedPtr<FJsonObject> HandleExportBlueprintGraph(const TSharedPtr<FJsonObject>& Params);

    /**
     * Get all dependencies of a Blueprint (assets, classes, functions).
     */
    TSharedPtr<FJsonObject> HandleGetBlueprintDependencies(const TSharedPtr<FJsonObject>& Params);

    /**
     * Find all assets/Blueprints that reference a given Blueprint or function.
     */
    TSharedPtr<FJsonObject> HandleFindBlueprintReferences(const TSharedPtr<FJsonObject>& Params);

    /**
     * Redirect function calls from Blueprint function to C++ function.
     * Supports dry_run mode and backup creation.
     */
    TSharedPtr<FJsonObject> HandleRedirectFunctionCall(const TSharedPtr<FJsonObject>& Params);

    /**
     * Delete a function graph from a Blueprint.
     * Used during cleanup to remove migrated functions.
     */
    TSharedPtr<FJsonObject> HandleDeleteBlueprintFunction(const TSharedPtr<FJsonObject>& Params);

    /**
     * Change the parent class of a Blueprint.
     * Used to reparent Blueprint to new C++ class after migration.
     */
    TSharedPtr<FJsonObject> HandleSetBlueprintParentClass(const TSharedPtr<FJsonObject>& Params);

    /**
     * Get list of all functions defined in a Blueprint.
     * Useful for verification before/after cleanup.
     */
    TSharedPtr<FJsonObject> HandleGetBlueprintFunctions(const TSharedPtr<FJsonObject>& Params);

    // Helper functions

    /**
     * Write JSON content to a temp file in Saved/UnrealMCP/Exports/
     * @param FileName The filename to use (without path)
     * @param JsonContent The JSON content to write
     * @return Full path to the written file, or empty string on failure
     */
    FString WriteJsonToTempFile(const FString& FileName, const TSharedPtr<FJsonObject>& JsonContent);

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
};
