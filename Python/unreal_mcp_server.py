#!/usr/bin/env python3
"""
Unified Unreal MCP Server - Main Entry Point

This server consolidates all Unreal Engine MCP tools into a single server.
It registers tools from all specialized modules:
- Blueprint tools (creation, variables, components)
- Node tools (graph manipulation, connections)
- Blueprint action tools (dynamic node discovery, creation)
- Migration tools (Blueprint-to-C++ migration)
- Editor tools (actor manipulation)
"""

# Force unbuffered stdout/stderr for proper MCP stdio transport
# This MUST be done before any other imports that might write to stdout
import sys
import os
os.environ['PYTHONUNBUFFERED'] = '1'
# Reopen stdout/stderr in unbuffered mode
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(line_buffering=False, write_through=True)
if hasattr(sys.stderr, 'reconfigure'):
    sys.stderr.reconfigure(line_buffering=False, write_through=True)

import logging
from typing import Dict, List, Any, Optional
from mcp.server.fastmcp import FastMCP, Context

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("UnrealMCP")

# Create the unified MCP server
mcp = FastMCP(
    "unrealMCP",
    description="Unified Unreal Engine MCP Server - Blueprint, Node, Migration, and Editor tools"
)

# Import utilities
from utils.unreal_connection_utils import send_unreal_command

# Import and register tool modules (sync pattern)
from node_tools.node_tools import register_blueprint_node_tools
from blueprint_action_tools.blueprint_action_tools import register_blueprint_action_tools

# Register core tool modules
register_blueprint_node_tools(mcp)
register_blueprint_action_tools(mcp)


# ============================================================================
# MIGRATION REDIRECT TOOLS - Blueprint-to-C++ function redirection
# ============================================================================

from utils.migration.redirect_operations import (
    redirect_blueprint_function_calls,
    verify_no_external_references,
    get_node_connections as get_node_connections_impl
)


@mcp.tool()
def redirect_blueprint_function_call(
    ctx: Context,
    source_function_title: str,
    target_class: str,
    target_function: str,
    pin_mapping: dict = None,
    blueprint_filter: str = "/Game",
    specific_blueprints: list = None,
    dry_run: bool = True
) -> dict:
    """
    Redirect Blueprint function calls to C++ function calls across multiple Blueprints.

    This is THE key tool for Blueprint-to-C++ migration. It finds all calls to a Blueprint
    function (by display title, e.g., "Pickup Item") and replaces them with calls to a
    C++ function, preserving pin connections.

    IMPORTANT: Always run with dry_run=True first to preview changes!

    Args:
        source_function_title: Display title of the BP function to replace (e.g., "Pickup Item")
                              This matches the text shown on the node in the Blueprint editor.
        target_class: C++ class containing the replacement function (e.g., "ConstructionDroneBase")
        target_function: Name of the C++ function (e.g., "PickupItem")
        pin_mapping: Optional dict mapping old pin names to new names.
                    Example: {"Item Pickup": "ItemPickup", "Style": "PickupStyle"}
                    If not provided, pin names are assumed to match.
        blueprint_filter: Content path to search (default: "/Game")
        specific_blueprints: Optional list of specific Blueprint names to process.
                           If provided, only these Blueprints will be modified.
        dry_run: If True (default), only preview changes without applying them.
                Set to False to actually perform the redirect.

    Returns:
        Dict with:
        - success: Overall success
        - dry_run: Whether this was a preview
        - source_function: The function being redirected
        - target_function: The C++ replacement (class::function)
        - blueprints_affected: Number of Blueprints with changes
        - nodes_redirected: Total nodes redirected (or would be)
        - results_by_blueprint: Detailed results per Blueprint
        - errors: Any errors encountered

    Examples:
        # Preview redirecting "Pickup Item" calls to C++ PickupItem
        redirect_blueprint_function_call(
            source_function_title="Pickup Item",
            target_class="ConstructionDroneBase",
            target_function="PickupItem",
            pin_mapping={"Item Pickup": "ItemPickup"},
            dry_run=True
        )

        # Actually perform the redirect (after previewing)
        redirect_blueprint_function_call(
            source_function_title="Pickup Item",
            target_class="ConstructionDroneBase",
            target_function="PickupItem",
            pin_mapping={"Item Pickup": "ItemPickup"},
            dry_run=False
        )
    """
    return redirect_blueprint_function_calls(
        source_function_title=source_function_title,
        target_class=target_class,
        target_function=target_function,
        pin_mapping=pin_mapping or {},
        blueprint_filter=blueprint_filter,
        specific_blueprints=specific_blueprints,
        dry_run=dry_run
    )


@mcp.tool()
def verify_function_references(
    ctx: Context,
    blueprint_path: str,
    function_name: str
) -> dict:
    """
    Verify that no external Blueprints still reference a Blueprint function.

    Use this BEFORE deleting a Blueprint function to ensure all external references
    have been redirected to C++. This prevents broken references after cleanup.

    Args:
        blueprint_path: Path to the Blueprint containing the function
        function_name: Name of the function to check

    Returns:
        Dict with:
        - success: Whether the check succeeded
        - function_name: The function checked
        - blueprint: The Blueprint containing the function
        - safe_to_delete: True if no external references remain
        - remaining_refs_count: Number of external references found
        - remaining_refs: List of Blueprints still referencing the function
    """
    return verify_no_external_references(blueprint_path, function_name)


@mcp.tool()
def inspect_node_connections(
    ctx: Context,
    blueprint_name: str,
    node_id: str,
    graph_name: str = "EventGraph"
) -> dict:
    """
    Get all connections for a node without deleting it.

    This is useful for inspecting a node's connections before deciding
    how to handle pin mapping during migration.

    Args:
        blueprint_name: Name of the Blueprint
        node_id: ID of the node to inspect
        graph_name: Name of the graph containing the node

    Returns:
        Dict with:
        - success: Whether the query succeeded
        - node_id: The node ID
        - node_title: Display title of the node
        - node_position: [x, y] position in the graph
        - input_connections: List of input connections
        - output_connections: List of output connections
    """
    return get_node_connections_impl(blueprint_name, node_id, graph_name)


# ============================================================================
# MIGRATION TOOLS - Export, analyze, and manage Blueprint migration
# ============================================================================

@mcp.tool()
def export_blueprint_graph(
    ctx: Context,
    blueprint_path: str,
    graph_name: str = "",
    include_components: bool = True,
    include_defaults: bool = False
) -> dict:
    """
    Export a complete Blueprint graph to a JSON file.

    The JSON is written to Saved/UnrealMCP/Exports/.
    """
    params = {
        "blueprint_path": blueprint_path,
        "include_components": include_components,
        "include_defaults": include_defaults
    }
    if graph_name:
        params["graph_name"] = graph_name
    return send_unreal_command("export_blueprint_graph", params)


@mcp.tool()
def get_blueprint_dependencies(
    ctx: Context,
    blueprint_path: str,
    include_engine_classes: bool = False,
    recursive: bool = True
) -> dict:
    """Get all dependencies of a Blueprint."""
    return send_unreal_command("get_blueprint_dependencies", {
        "blueprint_path": blueprint_path,
        "include_engine_classes": include_engine_classes,
        "recursive": recursive
    })


@mcp.tool()
def find_blueprint_references(
    ctx: Context,
    target_path: str,
    target_function: str = "",
    search_scope: str = "project",
    include_soft_references: bool = True
) -> dict:
    """Find all assets/Blueprints that reference a given Blueprint or function."""
    params = {
        "target_path": target_path,
        "search_scope": search_scope,
        "include_soft_references": include_soft_references
    }
    if target_function:
        params["target_function"] = target_function
    return send_unreal_command("find_blueprint_references", params)


@mcp.tool()
def delete_blueprint_function(
    ctx: Context,
    blueprint_path: str,
    function_name: str,
    backup: bool = True
) -> dict:
    """
    Delete a function graph from a Blueprint.

    Use during Blueprint cleanup to remove functions migrated to C++.
    Always verify no external references remain first using verify_function_references.
    """
    return send_unreal_command("delete_blueprint_function", {
        "blueprint_path": blueprint_path,
        "function_name": function_name,
        "backup": backup
    })


@mcp.tool()
def set_blueprint_parent_class(
    ctx: Context,
    blueprint_path: str,
    new_parent_class: str,
    backup: bool = True
) -> dict:
    """
    Change the parent class of a Blueprint.

    Use during migration to reparent a Blueprint to a new C++ class.
    """
    return send_unreal_command("set_blueprint_parent_class", {
        "blueprint_path": blueprint_path,
        "new_parent_class": new_parent_class,
        "backup": backup
    })


@mcp.tool()
def get_blueprint_functions(
    ctx: Context,
    blueprint_path: str,
    include_inherited: bool = False
) -> dict:
    """Get a list of all functions defined in a Blueprint."""
    return send_unreal_command("get_blueprint_functions", {
        "blueprint_path": blueprint_path,
        "include_inherited": include_inherited
    })


# ============================================================================
# BLUEPRINT TOOLS - Blueprint class and variable management
# ============================================================================

@mcp.tool()
def create_blueprint(
    ctx: Context,
    name: str,
    parent_class: str,
    folder_path: str = ""
) -> dict:
    """Create a new Blueprint class."""
    params = {"name": name, "parent_class": parent_class}
    if folder_path:
        params["folder_path"] = folder_path
    return send_unreal_command("create_blueprint", params)


@mcp.tool()
def compile_blueprint(ctx: Context, blueprint_name: str) -> dict:
    """
    Compile a Blueprint with enhanced error reporting.

    Returns detailed compilation error information including node-level,
    graph-level, and Blueprint-level errors.
    """
    return send_unreal_command("compile_blueprint", {"blueprint_name": blueprint_name})


@mcp.tool()
def get_blueprint_metadata(
    ctx: Context,
    blueprint_name: str,
    fields: list = None,
    graph_name: str = None,
    node_type: str = None,
    event_type: str = None,
    detail_level: str = None,
    component_name: str = None
) -> dict:
    """
    Get comprehensive metadata about a Blueprint.

    Args:
        blueprint_name: Name or path of the Blueprint
        fields: List of metadata fields to retrieve. Options:
                "parent_class", "interfaces", "variables", "functions",
                "components", "component_properties", "graphs", "status",
                "metadata", "timelines", "asset_info", "orphaned_nodes", "graph_nodes"
        graph_name: Optional graph name filter for "graph_nodes" field
        node_type: Optional node type filter ("Event", "Function", "Variable", etc.)
        event_type: Optional event type filter ("BeginPlay", "Tick", etc.)
        detail_level: Detail level for "graph_nodes": "summary", "flow", or "full"
        component_name: Required when using "component_properties" field
    """
    params = {"blueprint_name": blueprint_name}
    if fields:
        params["fields"] = fields
    if graph_name:
        params["graph_name"] = graph_name
    if node_type:
        params["node_type"] = node_type
    if event_type:
        params["event_type"] = event_type
    if detail_level:
        params["detail_level"] = detail_level
    if component_name:
        params["component_name"] = component_name
    return send_unreal_command("get_blueprint_metadata", params)


@mcp.tool()
def add_blueprint_variable(
    ctx: Context,
    blueprint_name: str,
    variable_name: str,
    variable_type: str,
    is_exposed: bool = False
) -> dict:
    """Add a variable to a Blueprint."""
    return send_unreal_command("add_blueprint_variable", {
        "blueprint_name": blueprint_name,
        "variable_name": variable_name,
        "variable_type": variable_type,
        "is_exposed": is_exposed
    })


@mcp.tool()
def delete_blueprint_variable(
    ctx: Context,
    blueprint_name: str,
    variable_name: str
) -> dict:
    """Delete a variable from a Blueprint."""
    return send_unreal_command("delete_blueprint_variable", {
        "blueprint_name": blueprint_name,
        "variable_name": variable_name
    })


# ============================================================================
# EDITOR TOOLS - Actor and level management
# ============================================================================

@mcp.tool()
def get_actors_in_level(ctx: Context) -> dict:
    """Get a list of all actors in the current level."""
    return send_unreal_command("get_actors_in_level", {})


@mcp.tool()
def find_actors_by_name(ctx: Context, pattern: str) -> dict:
    """Find actors by name pattern."""
    return send_unreal_command("find_actors_by_name", {"pattern": pattern})


@mcp.tool()
def spawn_actor(
    ctx: Context,
    name: str,
    actor_type: str,
    location: list = None,
    rotation: list = None
) -> dict:
    """Spawn a new actor in the level."""
    params = {"name": name, "type": actor_type}
    if location:
        params["location"] = location
    if rotation:
        params["rotation"] = rotation
    return send_unreal_command("spawn_actor", params)


@mcp.tool()
def spawn_blueprint_actor(
    ctx: Context,
    blueprint_name: str,
    actor_name: str,
    location: list = None,
    rotation: list = None
) -> dict:
    """Spawn an actor from a Blueprint."""
    params = {"blueprint_name": blueprint_name, "actor_name": actor_name}
    if location:
        params["location"] = location
    if rotation:
        params["rotation"] = rotation
    return send_unreal_command("spawn_blueprint_actor", params)


@mcp.tool()
def delete_actor(ctx: Context, name: str) -> dict:
    """Delete an actor by name."""
    return send_unreal_command("delete_actor", {"name": name})


@mcp.tool()
def set_actor_transform(
    ctx: Context,
    name: str,
    location: list = None,
    rotation: list = None,
    scale: list = None
) -> dict:
    """Set the transform of an actor."""
    params = {"name": name}
    if location:
        params["location"] = location
    if rotation:
        params["rotation"] = rotation
    if scale:
        params["scale"] = scale
    return send_unreal_command("set_actor_transform", params)


if __name__ == "__main__":
    mcp.run(transport='stdio')
