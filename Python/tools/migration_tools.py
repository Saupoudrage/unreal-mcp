"""
Blueprint Migration Tools for Unreal MCP.

This module provides tools for Blueprint-to-C++ migration workflows,
including graph export, dependency analysis, reference finding, and
function call redirection.
"""

import logging
from typing import Dict, List, Any, Optional
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")


def register_migration_tools(mcp: FastMCP):
    """Register Blueprint migration tools with the MCP server."""

    @mcp.tool()
    def export_blueprint_graph(
        ctx: Context,
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
            Dict with:
            - success: Whether the export succeeded
            - file_path: Full path to the exported JSON file
            - graph_count: Number of graphs exported
            - node_count: Total number of nodes exported
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            params = {
                "blueprint_path": blueprint_path,
                "include_components": include_components,
                "include_defaults": include_defaults
            }

            if graph_name:
                params["graph_name"] = graph_name

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Exporting Blueprint graph: {blueprint_path}")
            response = unreal.send_command("export_blueprint_graph", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Blueprint graph export response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error exporting Blueprint graph: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_blueprint_dependencies(
        ctx: Context,
        blueprint_path: str,
        include_engine_classes: bool = False,
        recursive: bool = True
    ) -> Dict[str, Any]:
        """
        Get all dependencies of a Blueprint.

        Returns categorized lists of assets, blueprints, native classes,
        and function calls with their counts.

        Args:
            blueprint_path: Path to the Blueprint (e.g., "/Game/Blueprints/MyBP" or just "MyBP")
            include_engine_classes: Include engine/native class dependencies
            recursive: Recursively gather dependencies (not yet implemented)

        Returns:
            Dict with:
            - blueprint_path: The analyzed Blueprint's path
            - assets: List of asset dependencies
            - blueprints: List of Blueprint dependencies
            - native_classes: List of native C++ class dependencies
            - function_calls: List of function calls with counts
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            params = {
                "blueprint_path": blueprint_path,
                "include_engine_classes": include_engine_classes,
                "recursive": recursive
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Getting dependencies for Blueprint: {blueprint_path}")
            response = unreal.send_command("get_blueprint_dependencies", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Blueprint dependencies response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error getting Blueprint dependencies: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def find_blueprint_references(
        ctx: Context,
        target_path: str,
        target_function: str = "",
        search_scope: str = "project",
        include_soft_references: bool = True
    ) -> Dict[str, Any]:
        """
        Find all assets/Blueprints that reference a given Blueprint or function.

        Useful for understanding impact before migrating Blueprint functionality
        to C++.

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
        from unreal_mcp_server import get_unreal_connection

        try:
            params = {
                "target_path": target_path,
                "search_scope": search_scope,
                "include_soft_references": include_soft_references
            }

            if target_function:
                params["target_function"] = target_function

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Finding references to: {target_path}" +
                       (f"::{target_function}" if target_function else ""))
            response = unreal.send_command("find_blueprint_references", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Blueprint references response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error finding Blueprint references: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def redirect_function_call(
        ctx: Context,
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
        to preview changes.

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
        from unreal_mcp_server import get_unreal_connection

        try:
            params = {
                "source_blueprint": source_blueprint,
                "source_function": source_function,
                "target_class": target_class,
                "target_function": target_function,
                "dry_run": dry_run,
                "backup": backup
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            action = "Preview redirect" if dry_run else "Redirect"
            logger.info(f"{action}: {source_blueprint}::{source_function} -> {target_class}::{target_function}")
            response = unreal.send_command("redirect_function_call", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Redirect function call response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error redirecting function call: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def delete_blueprint_function(
        ctx: Context,
        blueprint_path: str,
        function_name: str,
        backup: bool = True
    ) -> Dict[str, Any]:
        """
        Delete a function graph from a Blueprint.

        Use this during Blueprint cleanup to remove function graphs that have
        been migrated to C++. Always creates a backup by default.

        Args:
            blueprint_path: Path to the Blueprint to modify
            function_name: Name of the function graph to delete
            backup: If True, create a backup JSON before making changes

        Returns:
            Dict with:
            - success: Whether the deletion succeeded
            - blueprint_path: The modified Blueprint's path
            - function_name: The deleted function name
            - backup_path: (if backup=True) Path to backup file
            - message: Summary message
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            params = {
                "blueprint_path": blueprint_path,
                "function_name": function_name,
                "backup": backup
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Deleting function '{function_name}' from Blueprint: {blueprint_path}")
            response = unreal.send_command("delete_blueprint_function", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Delete function response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error deleting Blueprint function: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_blueprint_parent_class(
        ctx: Context,
        blueprint_path: str,
        new_parent_class: str,
        backup: bool = True
    ) -> Dict[str, Any]:
        """
        Change the parent class of a Blueprint.

        Use this during migration to reparent a Blueprint to a new C++ class
        that contains the migrated functionality.

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
            - backup_path: (if backup=True) Path to backup file
            - requires_compile: Whether Blueprint needs recompile
            - message: Summary message
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            params = {
                "blueprint_path": blueprint_path,
                "new_parent_class": new_parent_class,
                "backup": backup
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Setting parent class of '{blueprint_path}' to '{new_parent_class}'")
            response = unreal.send_command("set_blueprint_parent_class", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Set parent class response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error setting Blueprint parent class: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_blueprint_functions(
        ctx: Context,
        blueprint_path: str,
        include_inherited: bool = False
    ) -> Dict[str, Any]:
        """
        Get a list of all functions defined in a Blueprint.

        Useful for verifying which functions exist before/after cleanup,
        and for planning which functions to migrate.

        Args:
            blueprint_path: Path to the Blueprint to analyze
            include_inherited: Include functions inherited from parent class

        Returns:
            Dict with:
            - success: Whether the query succeeded
            - blueprint_path: The Blueprint's path
            - functions: List of function details including:
              - name: Function name
              - graph_name: Name of the function's graph
              - node_count: Number of nodes in the function
              - is_event: Whether this is an event (BeginPlay, Tick, etc.)
              - is_overridable: Whether this can be overridden
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            params = {
                "blueprint_path": blueprint_path,
                "include_inherited": include_inherited
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Getting functions for Blueprint: {blueprint_path}")
            response = unreal.send_command("get_blueprint_functions", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get functions response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error getting Blueprint functions: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Blueprint migration tools registered successfully")
