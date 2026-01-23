#include "Commands/Migration/MigrationCommandRegistration.h"
#include "Commands/UnrealMCPCommandRegistry.h"
#include "Commands/Migration/ExportBlueprintGraphCommand.h"
#include "Commands/Migration/GetBlueprintDependenciesCommand.h"
#include "Commands/Migration/FindBlueprintReferencesCommand.h"
#include "Commands/Migration/RedirectFunctionCallCommand.h"
#include "Commands/Migration/DeleteBlueprintFunctionCommand.h"
#include "Commands/Migration/SetBlueprintParentClassCommand.h"
#include "Commands/Migration/GetBlueprintFunctionsCommand.h"

// Static member definition
TArray<FString> FMigrationCommandRegistration::RegisteredCommandNames;

void FMigrationCommandRegistration::RegisterAllMigrationCommands()
{
    UE_LOG(LogTemp, Log, TEXT("FMigrationCommandRegistration::RegisterAllMigrationCommands: Starting Migration command registration"));

    // Clear any existing registrations
    RegisteredCommandNames.Empty();

    // Register individual commands
    RegisterExportBlueprintGraphCommand();
    RegisterGetBlueprintDependenciesCommand();
    RegisterFindBlueprintReferencesCommand();
    RegisterRedirectFunctionCallCommand();
    RegisterDeleteBlueprintFunctionCommand();
    RegisterSetBlueprintParentClassCommand();
    RegisterGetBlueprintFunctionsCommand();

    UE_LOG(LogTemp, Log, TEXT("FMigrationCommandRegistration::RegisterAllMigrationCommands: Registered %d Migration commands"),
        RegisteredCommandNames.Num());
}

void FMigrationCommandRegistration::UnregisterAllMigrationCommands()
{
    UE_LOG(LogTemp, Log, TEXT("FMigrationCommandRegistration::UnregisterAllMigrationCommands: Starting Migration command unregistration"));

    FUnrealMCPCommandRegistry& Registry = FUnrealMCPCommandRegistry::Get();

    int32 UnregisteredCount = 0;
    for (const FString& CommandName : RegisteredCommandNames)
    {
        if (Registry.UnregisterCommand(CommandName))
        {
            UnregisteredCount++;
        }
    }

    RegisteredCommandNames.Empty();

    UE_LOG(LogTemp, Log, TEXT("FMigrationCommandRegistration::UnregisterAllMigrationCommands: Unregistered %d Migration commands"),
        UnregisteredCount);
}

void FMigrationCommandRegistration::RegisterExportBlueprintGraphCommand()
{
    TSharedPtr<FExportBlueprintGraphCommand> Command = MakeShared<FExportBlueprintGraphCommand>();
    RegisterAndTrackCommand(Command);
}

void FMigrationCommandRegistration::RegisterGetBlueprintDependenciesCommand()
{
    TSharedPtr<FGetBlueprintDependenciesCommand> Command = MakeShared<FGetBlueprintDependenciesCommand>();
    RegisterAndTrackCommand(Command);
}

void FMigrationCommandRegistration::RegisterFindBlueprintReferencesCommand()
{
    TSharedPtr<FFindBlueprintReferencesCommand> Command = MakeShared<FFindBlueprintReferencesCommand>();
    RegisterAndTrackCommand(Command);
}

void FMigrationCommandRegistration::RegisterRedirectFunctionCallCommand()
{
    TSharedPtr<FRedirectFunctionCallCommand> Command = MakeShared<FRedirectFunctionCallCommand>();
    RegisterAndTrackCommand(Command);
}

void FMigrationCommandRegistration::RegisterDeleteBlueprintFunctionCommand()
{
    TSharedPtr<FDeleteBlueprintFunctionCommand> Command = MakeShared<FDeleteBlueprintFunctionCommand>();
    RegisterAndTrackCommand(Command);
}

void FMigrationCommandRegistration::RegisterSetBlueprintParentClassCommand()
{
    TSharedPtr<FSetBlueprintParentClassCommand> Command = MakeShared<FSetBlueprintParentClassCommand>();
    RegisterAndTrackCommand(Command);
}

void FMigrationCommandRegistration::RegisterGetBlueprintFunctionsCommand()
{
    TSharedPtr<FGetBlueprintFunctionsCommand> Command = MakeShared<FGetBlueprintFunctionsCommand>();
    RegisterAndTrackCommand(Command);
}

void FMigrationCommandRegistration::RegisterAndTrackCommand(TSharedPtr<IUnrealMCPCommand> Command)
{
    if (!Command.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("FMigrationCommandRegistration::RegisterAndTrackCommand: Invalid command"));
        return;
    }

    FString CommandName = Command->GetCommandName();
    if (CommandName.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("FMigrationCommandRegistration::RegisterAndTrackCommand: Command has empty name"));
        return;
    }

    FUnrealMCPCommandRegistry& Registry = FUnrealMCPCommandRegistry::Get();
    if (Registry.RegisterCommand(Command))
    {
        RegisteredCommandNames.Add(CommandName);
        UE_LOG(LogTemp, Verbose, TEXT("FMigrationCommandRegistration::RegisterAndTrackCommand: Registered and tracked command '%s'"), *CommandName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FMigrationCommandRegistration::RegisterAndTrackCommand: Failed to register command '%s'"), *CommandName);
    }
}
