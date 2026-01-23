#pragma once

#include "CoreMinimal.h"
#include "Commands/IUnrealMCPCommand.h"

/**
 * Command for analyzing all dependencies of a Blueprint.
 * Returns categorized lists of assets, blueprints, native classes, and function calls.
 *
 * Parameters:
 *   - blueprint_path (string, required): Path to the Blueprint
 *   - include_engine_classes (bool, optional): Include engine/native class dependencies (default: false)
 *   - recursive (bool, optional): Recursively gather dependencies (default: true)
 *
 * Returns:
 *   - blueprint_path (string): The analyzed Blueprint's path
 *   - assets (array): List of asset dependencies
 *   - blueprints (array): List of Blueprint dependencies
 *   - native_classes (array): List of native C++ class dependencies
 *   - function_calls (array): List of function calls with counts
 */
class UNREALMCP_API FGetBlueprintDependenciesCommand : public IUnrealMCPCommand
{
public:
    FGetBlueprintDependenciesCommand() = default;

    // IUnrealMCPCommand interface
    virtual FString Execute(const FString& Parameters) override;
    virtual FString GetCommandName() const override { return TEXT("get_blueprint_dependencies"); }
    virtual bool ValidateParams(const FString& Parameters) const override;

private:
    /**
     * Create error response JSON
     */
    FString CreateErrorResponse(const FString& ErrorMessage) const;
};
