"""
Blueprint Migration Tools for Unreal MCP.

This module provides tools for Blueprint-to-C++ migration workflows,
including graph export, dependency analysis, reference finding, and
function call redirection.

These tools can be registered with any FastMCP server that has
access to the Unreal Engine TCP connection.
"""

import asyncio
import json
import logging
from typing import Any, Dict, List, Optional

from fastmcp import FastMCP

# Get logger
logger = logging.getLogger("UnrealMCP.Migration")


def register_migration_tools(mcp: FastMCP, send_command_func):
    """
    Register Blueprint migration tools with an MCP server.

    Args:
        mcp: FastMCP server instance to register tools with
        send_command_func: Async function to send commands to Unreal Engine
                          Should have signature: async def send(cmd_type: str, params: dict) -> dict
    """

    @mcp.tool()
    async def export_blueprint_graph(
        blueprint_path: str,
        graph_name: str = "",
        include_components: bool = True,
        include_defaults: bool = False
    ) -> Dict[str, Any]:
        """
        Export a complete Blueprint graph to a JSON file.

        The JSON is written to Saved/UnrealMCP/Exports/ to avoid socket buffer
        issues with complex Blueprints.

        Args:
            blueprint_path: Path to the Blueprint (e.g., "/Game/Blueprints/MyBP" or just "MyBP")
            graph_name: Optional graph name filter (exports all if omitted)
            include_components: Include component hierarchy in export
            include_defaults: Include default values for all properties

        Returns:
            Dict with file_path, graph_count, node_count
        """
        params = {
            "blueprint_path": blueprint_path,
            "include_components": include_components,
            "include_defaults": include_defaults
        }

        if graph_name:
            params["graph_name"] = graph_name

        logger.info(f"Exporting Blueprint graph: {blueprint_path}")
        return await send_command_func("export_blueprint_graph", params)

    @mcp.tool()
    async def get_blueprint_dependencies(
        blueprint_path: str,
        include_engine_classes: bool = False,
        recursive: bool = True
    ) -> Dict[str, Any]:
        """
        Get all dependencies of a Blueprint.

        Args:
            blueprint_path: Path to the Blueprint
            include_engine_classes: Include engine/native class dependencies
            recursive: Recursively gather dependencies

        Returns:
            Dict with assets, blueprints, native_classes, function_calls
        """
        params = {
            "blueprint_path": blueprint_path,
            "include_engine_classes": include_engine_classes,
            "recursive": recursive
        }

        logger.info(f"Getting dependencies for Blueprint: {blueprint_path}")
        return await send_command_func("get_blueprint_dependencies", params)

    @mcp.tool()
    async def find_blueprint_references(
        target_path: str,
        target_function: str = "",
        search_scope: str = "project",
        include_soft_references: bool = True
    ) -> Dict[str, Any]:
        """
        Find all assets/Blueprints that reference a given Blueprint or function.

        Args:
            target_path: Path to the target Blueprint
            target_function: Optional function name to find specific references to
            search_scope: Search scope ("project" or "all")
            include_soft_references: Include soft/lazy references

        Returns:
            Dict with referencer_count and referencers list
        """
        params = {
            "target_path": target_path,
            "search_scope": search_scope,
            "include_soft_references": include_soft_references
        }

        if target_function:
            params["target_function"] = target_function

        logger.info(f"Finding references to: {target_path}" +
                   (f"::{target_function}" if target_function else ""))
        return await send_command_func("find_blueprint_references", params)

    @mcp.tool()
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

        Use dry_run=True first to preview changes.

        Args:
            source_blueprint: Path to the Blueprint to modify
            source_function: Name of the function currently being called
            target_class: C++ class containing the new function
            target_function: Name of the C++ function to redirect to
            dry_run: If True, only preview changes
            backup: If True, create backup before changes

        Returns:
            Dict with nodes_found, changes list, and status
        """
        params = {
            "source_blueprint": source_blueprint,
            "source_function": source_function,
            "target_class": target_class,
            "target_function": target_function,
            "dry_run": dry_run,
            "backup": backup
        }

        action = "Preview redirect" if dry_run else "Redirect"
        logger.info(f"{action}: {source_blueprint}::{source_function} -> {target_class}::{target_function}")
        return await send_command_func("redirect_function_call", params)

    @mcp.tool()
    async def delete_blueprint_function(
        blueprint_path: str,
        function_name: str,
        backup: bool = True
    ) -> Dict[str, Any]:
        """
        Delete a function graph from a Blueprint.

        Use during cleanup to remove migrated functions.

        Args:
            blueprint_path: Path to the Blueprint to modify
            function_name: Name of the function graph to delete
            backup: If True, create backup before changes

        Returns:
            Dict with success status and backup_path
        """
        params = {
            "blueprint_path": blueprint_path,
            "function_name": function_name,
            "backup": backup
        }

        logger.info(f"Deleting function '{function_name}' from Blueprint: {blueprint_path}")
        return await send_command_func("delete_blueprint_function", params)

    @mcp.tool()
    async def set_blueprint_parent_class(
        blueprint_path: str,
        new_parent_class: str,
        backup: bool = True
    ) -> Dict[str, Any]:
        """
        Change the parent class of a Blueprint.

        Use to reparent Blueprint to new C++ class after migration.

        Args:
            blueprint_path: Path to the Blueprint to modify
            new_parent_class: Full path or name of the new parent class
            backup: If True, create backup before changes

        Returns:
            Dict with old and new parent class info
        """
        params = {
            "blueprint_path": blueprint_path,
            "new_parent_class": new_parent_class,
            "backup": backup
        }

        logger.info(f"Setting parent class of '{blueprint_path}' to '{new_parent_class}'")
        return await send_command_func("set_blueprint_parent_class", params)

    @mcp.tool()
    async def get_blueprint_functions(
        blueprint_path: str,
        include_inherited: bool = False
    ) -> Dict[str, Any]:
        """
        Get a list of all functions defined in a Blueprint.

        Args:
            blueprint_path: Path to the Blueprint to analyze
            include_inherited: Include inherited functions

        Returns:
            Dict with functions list containing name, type, node_count
        """
        params = {
            "blueprint_path": blueprint_path,
            "include_inherited": include_inherited
        }

        logger.info(f"Getting functions for Blueprint: {blueprint_path}")
        return await send_command_func("get_blueprint_functions", params)

    logger.info("Blueprint migration tools registered successfully")
