#!/usr/bin/env python3
"""Migration MCP Server for Blueprint-to-C++ migration workflows.

This server provides tools for analyzing Blueprints, exporting graph data,
finding dependencies and references, and managing the migration process.
"""

import asyncio
import json
from typing import Any, Dict, List, Optional

from fastmcp import FastMCP

app = FastMCP("Migration MCP Server")

TCP_HOST = "127.0.0.1"
TCP_PORT = 55557


async def send_tcp_command(command_type: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Send a command to the Unreal Engine TCP server."""
    try:
        command_data = {"type": command_type, "params": params}
        json_data = json.dumps(command_data)

        reader, writer = await asyncio.open_connection(TCP_HOST, TCP_PORT)
        writer.write(json_data.encode('utf-8'))
        writer.write(b'\n')
        await writer.drain()

        response_data = await reader.read(49152)
        response_str = response_data.decode('utf-8').strip()

        writer.close()
        await writer.wait_closed()

        if response_str:
            try:
                return json.loads(response_str)
            except json.JSONDecodeError as json_err:
                return {"success": False, "error": f"JSON decode error: {str(json_err)}"}
        else:
            return {"success": False, "error": "Empty response from server"}
    except Exception as e:
        return {"success": False, "error": f"TCP communication error: {str(e)}"}


@app.tool()
async def export_blueprint_graph(
    blueprint_path: str,
    graph_name: str = "",
    include_components: bool = True,
    include_defaults: bool = False
) -> Dict[str, Any]:
    """
    Export a complete Blueprint graph to a JSON file.

    The JSON is written to Saved/UnrealMCP/Exports/ to avoid socket buffer
    issues with complex Blueprints. Use this to analyze Blueprint structure
    before migrating to C++.

    Args:
        blueprint_path: Path to the Blueprint (e.g., "/Game/Blueprints/MyBP" or just "MyBP")
        graph_name: Optional graph name filter (exports all if omitted)
        include_components: Include component hierarchy in export
        include_defaults: Include default values for all properties

    Returns:
        Dict with:
        - success: Whether the export succeeded
        - file_path: Full path to the exported JSON file
        - graph_count: Number of graphs exported
        - node_count: Total number of nodes exported
    """
    params = {
        "blueprint_path": blueprint_path,
        "include_components": include_components,
        "include_defaults": include_defaults
    }

    if graph_name:
        params["graph_name"] = graph_name

    return await send_tcp_command("export_blueprint_graph", params)


@app.tool()
async def get_blueprint_dependencies(
    blueprint_path: str,
    include_engine_classes: bool = False,
    recursive: bool = True
) -> Dict[str, Any]:
    """
    Get all dependencies of a Blueprint.

    Returns categorized lists of assets, blueprints, native classes,
    and function calls with their counts. Essential for understanding
    what a Blueprint relies on before migration.

    Args:
        blueprint_path: Path to the Blueprint (e.g., "/Game/Blueprints/MyBP" or just "MyBP")
        include_engine_classes: Include engine/native class dependencies
        recursive: Recursively gather dependencies

    Returns:
        Dict with:
        - blueprint_path: The analyzed Blueprint's path
        - assets: List of asset dependencies
        - blueprints: List of Blueprint dependencies
        - native_classes: List of native C++ class dependencies
        - function_calls: List of function calls with counts
    """
    params = {
        "blueprint_path": blueprint_path,
        "include_engine_classes": include_engine_classes,
        "recursive": recursive
    }

    return await send_tcp_command("get_blueprint_dependencies", params)


@app.tool()
async def find_blueprint_references(
    target_path: str,
    target_function: str = "",
    search_scope: str = "project",
    include_soft_references: bool = True
) -> Dict[str, Any]:
    """
    Find all assets/Blueprints that reference a given Blueprint or function.

    Useful for understanding impact before migrating Blueprint functionality
    to C++. Helps identify which Blueprints will need updates after migration.

    Args:
        target_path: Path to the target Blueprint
        target_function: Optional function name to find specific references to
        search_scope: Search scope ("project" or "all")
        include_soft_references: Include soft/lazy references

    Returns:
        Dict with:
        - target_path: The target Blueprint path
        - target_function: The function searched for (if specified)
        - referencer_count: Number of referencers found
        - referencers: List of referencer details including:
          - referencer_path: Path to the referencing asset
          - referencer_name: Name of the referencing Blueprint
          - type: "Blueprint" or "Asset"
          - reference_locations: (if searching for function) List of exact locations
    """
    params = {
        "target_path": target_path,
        "search_scope": search_scope,
        "include_soft_references": include_soft_references
    }

    if target_function:
        params["target_function"] = target_function

    return await send_tcp_command("find_blueprint_references", params)


@app.tool()
async def redirect_function_call(
    source_blueprint: str,
    source_function: str,
    target_class: str,
    target_function: str,
    dry_run: bool = True,
    backup: bool = True
) -> Dict[str, Any]:
    """
    Redirect function calls in a Blueprint from one function to another.

    Use this to update Blueprints to call C++ functions instead of
    Blueprint functions during migration. Always use dry_run=True first
    to preview changes before applying them.

    Args:
        source_blueprint: Path to the Blueprint to modify
        source_function: Name of the function currently being called
        target_class: C++ class containing the new function (e.g., "UMyGameLibrary")
        target_function: Name of the C++ function to redirect to
        dry_run: If True, only preview changes without applying them
        backup: If True, create a backup JSON before making changes

    Returns:
        Dict with:
        - source_blueprint: The Blueprint being modified
        - dry_run: Whether this was a preview only
        - nodes_found: Number of matching function calls found
        - changes: List of changes (or changes that would be made)
        - message: Summary message
        - backup_path: (if backup=True and dry_run=False) Path to backup file
        - requires_compile: (if dry_run=False) Whether Blueprint needs recompile
    """
    params = {
        "source_blueprint": source_blueprint,
        "source_function": source_function,
        "target_class": target_class,
        "target_function": target_function,
        "dry_run": dry_run,
        "backup": backup
    }

    return await send_tcp_command("redirect_function_call", params)


@app.tool()
async def delete_blueprint_function(
    blueprint_path: str,
    function_name: str,
    backup: bool = True
) -> Dict[str, Any]:
    """
    Delete a function graph from a Blueprint.

    Use this during Blueprint cleanup to remove function graphs that have
    been migrated to C++. Always creates a backup by default for safety.

    Args:
        blueprint_path: Path to the Blueprint to modify
        function_name: Name of the function graph to delete
        backup: If True, create a backup JSON before making changes

    Returns:
        Dict with:
        - success: Whether the deletion succeeded
        - blueprint_path: The modified Blueprint's path
        - function_name: The deleted function name
        - nodes_removed: Number of nodes removed
        - backup_path: (if backup=True) Path to backup file
        - requires_compile: Whether Blueprint needs recompile
        - message: Summary message
    """
    params = {
        "blueprint_path": blueprint_path,
        "function_name": function_name,
        "backup": backup
    }

    return await send_tcp_command("delete_blueprint_function", params)


@app.tool()
async def set_blueprint_parent_class(
    blueprint_path: str,
    new_parent_class: str,
    backup: bool = True
) -> Dict[str, Any]:
    """
    Change the parent class of a Blueprint.

    Use this during migration to reparent a Blueprint to a new C++ class
    that contains the migrated functionality. This is the final step
    after migrating Blueprint logic to C++.

    Args:
        blueprint_path: Path to the Blueprint to modify
        new_parent_class: Full path or name of the new parent class
        backup: If True, create a backup JSON before making changes

    Returns:
        Dict with:
        - success: Whether the reparenting succeeded
        - blueprint_path: The modified Blueprint's path
        - old_parent_class: The previous parent class name
        - new_parent_class: The new parent class name
        - new_parent_class_path: Full path of new parent class
        - backup_path: (if backup=True) Path to backup file
        - requires_compile: Whether Blueprint needs recompile
        - message: Summary message
    """
    params = {
        "blueprint_path": blueprint_path,
        "new_parent_class": new_parent_class,
        "backup": backup
    }

    return await send_tcp_command("set_blueprint_parent_class", params)


@app.tool()
async def get_blueprint_functions(
    blueprint_path: str,
    include_inherited: bool = False
) -> Dict[str, Any]:
    """
    Get a list of all functions defined in a Blueprint.

    Useful for verifying which functions exist before/after cleanup,
    and for planning which functions to migrate. Lists functions,
    events, event graphs, and macros.

    Args:
        blueprint_path: Path to the Blueprint to analyze
        include_inherited: Include functions inherited from parent class

    Returns:
        Dict with:
        - success: Whether the query succeeded
        - blueprint_path: The Blueprint's path
        - blueprint_name: The Blueprint's name
        - parent_class: Parent class name
        - function_count: Total number of functions
        - functions: List of function details including:
          - name: Function name
          - graph_name: Name of the function's graph
          - node_count: Number of nodes in the function
          - is_event: Whether this is an event (BeginPlay, Tick, etc.)
          - is_override: Whether this is an override
          - type: "Function", "Event", "EventGraph", or "Macro"
    """
    params = {
        "blueprint_path": blueprint_path,
        "include_inherited": include_inherited
    }

    return await send_tcp_command("get_blueprint_functions", params)


if __name__ == "__main__":
    app.run()
