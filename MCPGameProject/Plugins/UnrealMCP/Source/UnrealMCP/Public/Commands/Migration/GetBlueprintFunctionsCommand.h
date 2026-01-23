#pragma once

#include "CoreMinimal.h"
#include "Commands/IUnrealMCPCommand.h"

/**
 * Command for getting a list of all functions defined in a Blueprint.
 * Useful for verifying which functions exist before/after cleanup,
 * and for planning which functions to migrate.
 *
 * Parameters:
 *   - blueprint_path (string, required): Path to the Blueprint to analyze
 *   - include_inherited (bool, optional): Include inherited functions (default: false)
 *
 * Returns:
 *   - success (bool): Whether the query succeeded
 *   - blueprint_path (string): The Blueprint's path
 *   - blueprint_name (string): The Blueprint's name
 *   - parent_class (string): Parent class name
 *   - function_count (int): Total number of functions
 *   - functions (array): List of function details including:
 *       - name (string): Function name
 *       - graph_name (string): Name of the function's graph
 *       - node_count (int): Number of nodes in the function
 *       - is_event (bool): Whether this is an event
 *       - is_override (bool): Whether this is an override
 *       - type (string): "Function", "Event", "EventGraph", or "Macro"
 */
class UNREALMCP_API FGetBlueprintFunctionsCommand : public IUnrealMCPCommand
{
public:
    FGetBlueprintFunctionsCommand() = default;

    // IUnrealMCPCommand interface
    virtual FString Execute(const FString& Parameters) override;
    virtual FString GetCommandName() const override { return TEXT("get_blueprint_functions"); }
    virtual bool ValidateParams(const FString& Parameters) const override;

private:
    /**
     * Create error response JSON
     */
    FString CreateErrorResponse(const FString& ErrorMessage) const;
};
