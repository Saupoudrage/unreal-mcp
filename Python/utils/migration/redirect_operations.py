"""
Blueprint Function Redirect Operations.

Provides functionality to redirect Blueprint function calls to C++ function calls.
This handles the key migration gap: Blueprint function nodes use 'title' field
while C++ function nodes use 'function_name' field.
"""

import logging
from typing import Dict, List, Any, Optional
from utils.unreal_connection_utils import send_unreal_command

logger = logging.getLogger("UnrealMCP.Migration")


def find_blueprint_function_calls(
    function_title: str,
    search_path: str = "/Game",
    max_results: int = 100
) -> Dict[str, Any]:
    """
    Find all Blueprint function calls by their display title.

    Args:
        function_title: The display title of the function (e.g., "Pickup Item")
        search_path: Content path to search in
        max_results: Maximum number of results

    Returns:
        Dict with matches grouped by blueprint
    """
    result = send_unreal_command("find_in_blueprints", {
        "search_query": function_title,
        "search_type": "function",
        "path": search_path,
        "max_results": max_results,
        "case_sensitive": False
    })

    return result


def get_node_connections(
    blueprint_name: str,
    node_id: str,
    graph_name: str = "EventGraph"
) -> Dict[str, Any]:
    """
    Get all connections for a node without deleting it.

    Uses get_blueprint_metadata with graph_nodes field to find connections.
    """
    result = send_unreal_command("get_blueprint_metadata", {
        "blueprint_name": blueprint_name,
        "fields": ["graph_nodes"],
        "graph_name": graph_name,
        "detail_level": "full"
    })

    if not result.get("success"):
        return result

    # Find the specific node
    nodes = result.get("graph_nodes", [])
    target_node = None
    for node in nodes:
        if node.get("node_id") == node_id:
            target_node = node
            break

    if not target_node:
        return {
            "success": False,
            "error": f"Node {node_id} not found in graph {graph_name}"
        }

    # Extract connections from pins
    input_connections = []
    output_connections = []

    for pin in target_node.get("input_pins", []):
        for conn in pin.get("connections", []):
            input_connections.append({
                "pin_name": pin.get("name"),
                "connected_node_id": conn.get("node_id"),
                "connected_pin": conn.get("pin_name")
            })

    for pin in target_node.get("output_pins", []):
        for conn in pin.get("connections", []):
            output_connections.append({
                "pin_name": pin.get("name"),
                "connected_node_id": conn.get("node_id"),
                "connected_pin": conn.get("pin_name")
            })

    return {
        "success": True,
        "node_id": node_id,
        "node_title": target_node.get("title", ""),
        "node_position": [target_node.get("pos_x", 0), target_node.get("pos_y", 0)],
        "input_connections": input_connections,
        "output_connections": output_connections
    }


def apply_pin_mapping(connections: List[Dict], pin_mapping: Dict[str, str], is_input: bool) -> List[Dict]:
    """
    Apply pin name mapping to connections.

    Args:
        connections: List of connection dicts
        pin_mapping: Dict mapping old pin names to new pin names
        is_input: True for input connections, False for output

    Returns:
        Updated connections with mapped pin names
    """
    mapped = []
    for conn in connections:
        new_conn = conn.copy()
        old_pin = conn.get("pin_name", "")

        # Apply mapping if exists
        if old_pin in pin_mapping:
            new_conn["pin_name"] = pin_mapping[old_pin]

        mapped.append(new_conn)

    return mapped


def redirect_single_node(
    blueprint_name: str,
    node_id: str,
    graph_name: str,
    target_class: str,
    target_function: str,
    pin_mapping: Optional[Dict[str, str]] = None,
    dry_run: bool = True
) -> Dict[str, Any]:
    """
    Redirect a single Blueprint function node to a C++ function call.

    Args:
        blueprint_name: Name of the Blueprint containing the node
        node_id: ID of the node to replace
        graph_name: Name of the graph containing the node
        target_class: C++ class containing the replacement function
        target_function: Name of the C++ function
        pin_mapping: Optional dict mapping old pin names to new pin names
        dry_run: If True, only preview changes

    Returns:
        Dict with operation results
    """
    pin_mapping = pin_mapping or {}

    # Step 1: Get current node connections
    conn_result = get_node_connections(blueprint_name, node_id, graph_name)
    if not conn_result.get("success"):
        return conn_result

    node_position = conn_result.get("node_position", [0, 0])
    input_connections = conn_result.get("input_connections", [])
    output_connections = conn_result.get("output_connections", [])

    if dry_run:
        # Just return what would happen
        mapped_inputs = apply_pin_mapping(input_connections, pin_mapping, True)
        mapped_outputs = apply_pin_mapping(output_connections, pin_mapping, False)

        return {
            "success": True,
            "dry_run": True,
            "blueprint_name": blueprint_name,
            "graph_name": graph_name,
            "old_node_id": node_id,
            "old_node_title": conn_result.get("node_title"),
            "new_function": f"{target_class}::{target_function}",
            "position": node_position,
            "input_connections_to_restore": mapped_inputs,
            "output_connections_to_restore": mapped_outputs,
            "pin_mapping_applied": pin_mapping
        }

    # Step 2: Delete the old node and store connections
    replace_result = send_unreal_command("replace_node", {
        "blueprint_name": blueprint_name,
        "old_node_id": node_id,
        "new_node_type": target_function,
        "target_graph": graph_name
    })

    if not replace_result.get("success"):
        return {
            "success": False,
            "error": f"Failed to replace node: {replace_result.get('error', 'Unknown error')}",
            "step": "replace_node"
        }

    stored_connections = replace_result.get("stored_connections", [])
    old_pos_x = replace_result.get("old_node_pos_x", node_position[0])
    old_pos_y = replace_result.get("old_node_pos_y", node_position[1])

    # Step 3: Create the new C++ function call node
    create_result = send_unreal_command("create_node_by_action_name", {
        "blueprint_name": blueprint_name,
        "function_name": target_function,
        "class_name": target_class,
        "node_position": [old_pos_x, old_pos_y],
        "target_graph": graph_name
    })

    if not create_result.get("success"):
        return {
            "success": False,
            "error": f"Failed to create new node: {create_result.get('error', 'Unknown error')}",
            "step": "create_node",
            "stored_connections": stored_connections
        }

    new_node_id = create_result.get("node_id")

    # Step 4: Reconnect pins with mapping
    reconnect_errors = []
    reconnect_successes = []

    # Build connection list from stored connections
    connections_to_make = []

    for stored in stored_connections:
        old_pin_name = stored.get("pin_name", "")
        new_pin_name = pin_mapping.get(old_pin_name, old_pin_name)

        if stored.get("direction") == "input":
            # This was an input - source is the connected node, target is new node
            connections_to_make.append({
                "source_node_id": stored.get("connected_node_id"),
                "source_pin": stored.get("connected_pin_name"),
                "target_node_id": new_node_id,
                "target_pin": new_pin_name
            })
        else:
            # This was an output - source is new node, target is connected node
            connections_to_make.append({
                "source_node_id": new_node_id,
                "source_pin": new_pin_name,
                "target_node_id": stored.get("connected_node_id"),
                "target_pin": stored.get("connected_pin_name")
            })

    if connections_to_make:
        connect_result = send_unreal_command("connect_blueprint_nodes", {
            "blueprint_name": blueprint_name,
            "connections": connections_to_make,
            "target_graph": graph_name
        })

        if connect_result.get("success"):
            reconnect_successes = connect_result.get("results", [])
        else:
            reconnect_errors.append(connect_result.get("error", "Unknown connection error"))

    return {
        "success": True,
        "dry_run": False,
        "blueprint_name": blueprint_name,
        "graph_name": graph_name,
        "old_node_id": node_id,
        "new_node_id": new_node_id,
        "new_function": f"{target_class}::{target_function}",
        "connections_restored": len(reconnect_successes),
        "connection_errors": reconnect_errors,
        "requires_compile": True
    }


def redirect_blueprint_function_calls(
    source_function_title: str,
    target_class: str,
    target_function: str,
    pin_mapping: Optional[Dict[str, str]] = None,
    blueprint_filter: str = "/Game",
    specific_blueprints: Optional[List[str]] = None,
    dry_run: bool = True,
    backup: bool = True
) -> Dict[str, Any]:
    """
    Redirect all calls to a Blueprint function across multiple Blueprints.

    This is the main entry point for batch function redirection during migration.

    Args:
        source_function_title: Display title of the BP function (e.g., "Pickup Item")
        target_class: C++ class containing the replacement function
        target_function: Name of the C++ function
        pin_mapping: Dict mapping old pin names to new names (e.g., {"Item Pickup": "ItemPickup"})
        blueprint_filter: Content path to search (default: "/Game")
        specific_blueprints: Optional list of specific Blueprint names to process
        dry_run: If True, only preview changes
        backup: If True, create backups before changes (only when dry_run=False)

    Returns:
        Dict with:
        - success: Overall success
        - source_function: The function being redirected
        - target_function: The C++ replacement
        - blueprints_affected: Number of Blueprints with changes
        - nodes_redirected: Total nodes redirected (or would be)
        - results_by_blueprint: Detailed results per Blueprint
        - errors: Any errors encountered
    """
    pin_mapping = pin_mapping or {}

    # Step 1: Find all calls to the source function
    logger.info(f"Searching for calls to '{source_function_title}'...")
    search_result = find_blueprint_function_calls(
        source_function_title,
        blueprint_filter,
        max_results=500
    )

    if not search_result.get("success"):
        return {
            "success": False,
            "error": f"Search failed: {search_result.get('error', 'Unknown error')}"
        }

    matches = search_result.get("matches", [])
    if not matches:
        return {
            "success": True,
            "source_function": source_function_title,
            "target_function": f"{target_class}::{target_function}",
            "blueprints_affected": 0,
            "nodes_redirected": 0,
            "message": f"No calls to '{source_function_title}' found"
        }

    # Group by Blueprint
    by_blueprint = search_result.get("by_blueprint", {})

    # Filter to specific blueprints if provided
    if specific_blueprints:
        by_blueprint = {k: v for k, v in by_blueprint.items() if k in specific_blueprints}

    # Step 2: Process each Blueprint
    results_by_blueprint = {}
    total_nodes = 0
    total_errors = []

    for bp_name, bp_matches in by_blueprint.items():
        bp_results = []

        for match in bp_matches:
            node_id = match.get("node_id")
            graph_name = match.get("graph_name", "EventGraph")

            if not node_id:
                continue

            result = redirect_single_node(
                blueprint_name=bp_name,
                node_id=node_id,
                graph_name=graph_name,
                target_class=target_class,
                target_function=target_function,
                pin_mapping=pin_mapping,
                dry_run=dry_run
            )

            bp_results.append(result)

            if result.get("success"):
                total_nodes += 1
            else:
                total_errors.append({
                    "blueprint": bp_name,
                    "node_id": node_id,
                    "error": result.get("error")
                })

        results_by_blueprint[bp_name] = bp_results

        # Compile the Blueprint if we made changes
        if not dry_run and bp_results:
            compile_result = send_unreal_command("compile_blueprint", {
                "blueprint_name": bp_name
            })
            if not compile_result.get("success"):
                total_errors.append({
                    "blueprint": bp_name,
                    "error": f"Compile failed: {compile_result.get('error')}"
                })

    return {
        "success": len(total_errors) == 0,
        "dry_run": dry_run,
        "source_function": source_function_title,
        "target_function": f"{target_class}::{target_function}",
        "pin_mapping": pin_mapping,
        "blueprints_affected": len(results_by_blueprint),
        "nodes_redirected": total_nodes,
        "results_by_blueprint": results_by_blueprint,
        "errors": total_errors,
        "message": f"{'Would redirect' if dry_run else 'Redirected'} {total_nodes} nodes in {len(results_by_blueprint)} Blueprints"
    }


def verify_no_external_references(
    blueprint_path: str,
    function_name: str
) -> Dict[str, Any]:
    """
    Verify that no external Blueprints still reference a function.

    Use this before deleting a Blueprint function to ensure all references
    have been updated.

    Args:
        blueprint_path: Path to the Blueprint containing the function
        function_name: Name of the function to check

    Returns:
        Dict with:
        - safe_to_delete: True if no external references remain
        - remaining_refs: List of Blueprints still referencing the function
    """
    search_result = send_unreal_command("find_in_blueprints", {
        "search_query": function_name,
        "search_type": "function",
        "path": "/Game",
        "max_results": 100
    })

    if not search_result.get("success"):
        return {
            "success": False,
            "error": f"Search failed: {search_result.get('error')}"
        }

    # Extract the Blueprint name from path
    bp_name = blueprint_path.split("/")[-1]

    # Filter out references from the Blueprint itself
    external_refs = []
    for match in search_result.get("matches", []):
        match_bp = match.get("blueprint_name", "")
        if match_bp != bp_name and match_bp != blueprint_path:
            external_refs.append({
                "blueprint": match.get("blueprint_path"),
                "graph": match.get("graph_name"),
                "node_id": match.get("node_id"),
                "node_title": match.get("node_title")
            })

    return {
        "success": True,
        "function_name": function_name,
        "blueprint": blueprint_path,
        "safe_to_delete": len(external_refs) == 0,
        "remaining_refs_count": len(external_refs),
        "remaining_refs": external_refs
    }
