#pragma once

#include "CoreMinimal.h"

/**
 * Static class responsible for registering all Blueprint Migration commands
 * with the command registry system.
 *
 * Migration commands provide tools for Blueprint-to-C++ migration workflows,
 * including graph export, dependency analysis, reference finding, and
 * function call redirection.
 */
class UNREALMCP_API FMigrationCommandRegistration
{
public:
    /**
     * Register all Migration commands with the command registry
     * This should be called during module startup
     */
    static void RegisterAllMigrationCommands();

    /**
     * Unregister all Migration commands from the command registry
     * This should be called during module shutdown
     */
    static void UnregisterAllMigrationCommands();

private:
    /** Array of registered command names for cleanup */
    static TArray<FString> RegisteredCommandNames;

    /**
     * Register individual Migration commands
     */
    static void RegisterExportBlueprintGraphCommand();
    static void RegisterGetBlueprintDependenciesCommand();
    static void RegisterFindBlueprintReferencesCommand();
    static void RegisterRedirectFunctionCallCommand();
    static void RegisterDeleteBlueprintFunctionCommand();
    static void RegisterSetBlueprintParentClassCommand();
    static void RegisterGetBlueprintFunctionsCommand();

    /**
     * Helper to register a command and track it for cleanup
     * @param Command - Command to register
     */
    static void RegisterAndTrackCommand(TSharedPtr<class IUnrealMCPCommand> Command);
};
