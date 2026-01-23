#pragma once

#include "CoreMinimal.h"
#include "Commands/IUnrealMCPCommand.h"

/**
 * Command for changing the parent class of a Blueprint.
 * Use this during migration to reparent a Blueprint to a new C++ class
 * that contains the migrated functionality.
 *
 * Parameters:
 *   - blueprint_path (string, required): Path to the Blueprint to modify
 *   - new_parent_class (string, required): Full path or name of the new parent class
 *   - backup (bool, optional): If true, create backup before changes (default: true)
 *
 * Returns:
 *   - success (bool): Whether the reparenting succeeded
 *   - blueprint_path (string): The modified Blueprint's path
 *   - old_parent_class (string): The previous parent class name
 *   - new_parent_class (string): The new parent class name
 *   - new_parent_class_path (string): Full path of new parent class
 *   - backup_path (string): Path to backup file (if backup=true)
 *   - requires_compile (bool): Whether Blueprint needs recompile
 *   - message (string): Summary message
 */
class UNREALMCP_API FSetBlueprintParentClassCommand : public IUnrealMCPCommand
{
public:
    FSetBlueprintParentClassCommand() = default;

    // IUnrealMCPCommand interface
    virtual FString Execute(const FString& Parameters) override;
    virtual FString GetCommandName() const override { return TEXT("set_blueprint_parent_class"); }
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
