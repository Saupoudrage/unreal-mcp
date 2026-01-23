#pragma once

#include "CoreMinimal.h"
#include "Commands/IUnrealMCPCommand.h"

/**
 * Command for redirecting function calls in a Blueprint from one function to another.
 * Use this to update Blueprints to call C++ functions instead of Blueprint functions during migration.
 * Supports dry_run mode and automatic backup creation.
 *
 * Parameters:
 *   - source_blueprint (string, required): Path to the Blueprint to modify
 *   - source_function (string, required): Name of the function currently being called
 *   - target_class (string, required): C++ class containing the new function
 *   - target_function (string, required): Name of the C++ function to redirect to
 *   - dry_run (bool, optional): If true, only preview changes (default: true)
 *   - backup (bool, optional): If true, create backup before changes (default: true)
 *
 * Returns:
 *   - source_blueprint (string): The Blueprint being modified
 *   - dry_run (bool): Whether this was a preview only
 *   - nodes_found (int): Number of matching function calls found
 *   - changes (array): List of changes (or changes that would be made)
 *   - message (string): Summary message
 *   - backup_path (string): Path to backup file (if backup=true and dry_run=false)
 *   - requires_compile (bool): Whether Blueprint needs recompile
 */
class UNREALMCP_API FRedirectFunctionCallCommand : public IUnrealMCPCommand
{
public:
    FRedirectFunctionCallCommand() = default;

    // IUnrealMCPCommand interface
    virtual FString Execute(const FString& Parameters) override;
    virtual FString GetCommandName() const override { return TEXT("redirect_function_call"); }
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
     * Create error response JSON
     */
    FString CreateErrorResponse(const FString& ErrorMessage) const;
};
