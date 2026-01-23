#pragma once

#include "CoreMinimal.h"
#include "Commands/IUnrealMCPCommand.h"

/**
 * Command for finding all assets/Blueprints that reference a given Blueprint or function.
 * Useful for understanding impact before migrating Blueprint functionality to C++.
 *
 * Parameters:
 *   - target_path (string, required): Path to the target Blueprint
 *   - target_function (string, optional): Function name to find specific references to
 *   - search_scope (string, optional): Search scope - "project" or "all" (default: "project")
 *   - include_soft_references (bool, optional): Include soft/lazy references (default: true)
 *
 * Returns:
 *   - target_path (string): The target Blueprint path
 *   - target_function (string): The function searched for (if specified)
 *   - referencer_count (int): Number of referencers found
 *   - referencers (array): List of referencer details
 */
class UNREALMCP_API FFindBlueprintReferencesCommand : public IUnrealMCPCommand
{
public:
    FFindBlueprintReferencesCommand() = default;

    // IUnrealMCPCommand interface
    virtual FString Execute(const FString& Parameters) override;
    virtual FString GetCommandName() const override { return TEXT("find_blueprint_references"); }
    virtual bool ValidateParams(const FString& Parameters) const override;

private:
    /**
     * Create error response JSON
     */
    FString CreateErrorResponse(const FString& ErrorMessage) const;
};
