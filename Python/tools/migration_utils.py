"""
Migration Utilities for Blueprint-to-C++ conversion.

This module provides helper functions for parsing Blueprint exports,
generating codemaps, and creating C++ code.

IMPORTANT: The goal is to replace Blueprint LOGIC with C++, NOT delete the Blueprint.
- Generated C++ becomes the new parent class
- Blueprint reparents to inherit from C++
- Asset references stay in Blueprint defaults (EditDefaultsOnly)
- Logic moves to C++

See BlueprintExports/BLUEPRINT_TO_CPP_WORKFLOW.md for detailed patterns.
"""

import json
import os
from datetime import datetime
from typing import Dict, List, Any, Optional
from pathlib import Path


# Type mapping from Blueprint to C++
BLUEPRINT_TO_CPP_TYPES = {
    # Primitives
    "bool": "bool",
    "byte": "uint8",
    "int": "int32",
    "int64": "int64",
    "float": "float",
    "double": "double",

    # Strings
    "string": "FString",
    "name": "FName",
    "text": "FText",

    # Math
    "vector": "FVector",
    "vector2d": "FVector2D",
    "vector4": "FVector4",
    "rotator": "FRotator",
    "transform": "FTransform",
    "quat": "FQuat",
    "color": "FColor",
    "linearcolor": "FLinearColor",

    # Objects
    "object": "UObject*",
    "class": "UClass*",
    "softobject": "TSoftObjectPtr<UObject>",
    "softclass": "TSoftClassPtr<UObject>",
}


def get_project_saved_dir() -> Path:
    """Get the project's Saved directory."""
    # This assumes we're running from UnrealMCP-Server
    return Path(__file__).parent.parent.parent / "Saved" / "UnrealMCP"


def ensure_directory(path: Path) -> Path:
    """Ensure a directory exists."""
    path.mkdir(parents=True, exist_ok=True)
    return path


def get_exports_dir() -> Path:
    """Get the exports directory."""
    return ensure_directory(get_project_saved_dir() / "Exports")


def get_analysis_dir() -> Path:
    """Get the analysis directory."""
    return ensure_directory(get_project_saved_dir() / "Analysis")


def get_codemaps_dir() -> Path:
    """Get the codemaps directory."""
    return ensure_directory(get_project_saved_dir() / "Codemaps")


def get_generated_dir() -> Path:
    """Get the generated code directory."""
    return ensure_directory(get_project_saved_dir() / "Generated")


def get_migrations_dir() -> Path:
    """Get the migrations status directory."""
    return ensure_directory(get_project_saved_dir() / "Migrations")


def find_latest_export(blueprint_name: str) -> Optional[Path]:
    """Find the most recent export file for a blueprint."""
    exports_dir = get_exports_dir()
    pattern = f"export_{blueprint_name}_*.json"

    matching_files = list(exports_dir.glob(pattern))
    if not matching_files:
        return None

    # Sort by modification time, newest first
    matching_files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return matching_files[0]


def load_blueprint_export(blueprint_name: str) -> Optional[Dict[str, Any]]:
    """Load the most recent blueprint export."""
    export_path = find_latest_export(blueprint_name)
    if not export_path:
        return None

    with open(export_path, 'r', encoding='utf-8') as f:
        return json.load(f)


def map_blueprint_type_to_cpp(bp_type: str, subtype: str = None) -> str:
    """Convert a Blueprint type to C++ type."""
    bp_type_lower = bp_type.lower()

    # Check direct mapping
    if bp_type_lower in BLUEPRINT_TO_CPP_TYPES:
        return BLUEPRINT_TO_CPP_TYPES[bp_type_lower]

    # Handle object references
    if bp_type_lower == "object" and subtype:
        # Clean up the subtype name
        class_name = subtype.split("'")[-1].rstrip("'") if "'" in subtype else subtype
        if class_name.startswith("U") or class_name.startswith("A"):
            return f"TObjectPtr<{class_name}>"
        return f"TObjectPtr<U{class_name}>"

    # Handle class references
    if bp_type_lower == "class" and subtype:
        class_name = subtype.split("'")[-1].rstrip("'") if "'" in subtype else subtype
        return f"TSubclassOf<{class_name}>"

    # Handle soft references
    if bp_type_lower == "softobject" and subtype:
        class_name = subtype.split("'")[-1].rstrip("'") if "'" in subtype else subtype
        return f"TSoftObjectPtr<{class_name}>"

    if bp_type_lower == "softclass" and subtype:
        class_name = subtype.split("'")[-1].rstrip("'") if "'" in subtype else subtype
        return f"TSoftClassPtr<{class_name}>"

    # Default - assume it's a struct or enum
    if bp_type.startswith("E"):
        return bp_type  # Enum
    if bp_type.startswith("F") or bp_type.startswith("S"):
        return bp_type  # Struct

    return f"/* UNKNOWN: {bp_type} */ void*"


def extract_function_signature(node: Dict[str, Any]) -> Dict[str, Any]:
    """Extract function signature from a CallFunction node."""
    signature = {
        "name": node.get("function_name", "Unknown"),
        "class": node.get("function_class", ""),
        "is_pure": node.get("is_pure", False),
        "parameters": [],
        "return_type": "void"
    }

    # Parse input pins for parameters
    for pin in node.get("input_pins", []):
        if pin["name"] not in ["execute", "self", "Target", "WorldContextObject"]:
            signature["parameters"].append({
                "name": pin["name"],
                "type": map_blueprint_type_to_cpp(pin.get("category", ""), pin.get("subcategory")),
                "default_value": pin.get("default_value")
            })

    # Parse output pins for return type
    for pin in node.get("output_pins", []):
        if pin["name"] not in ["then", "execute"]:
            if pin["name"] == "ReturnValue":
                signature["return_type"] = map_blueprint_type_to_cpp(
                    pin.get("category", ""), pin.get("subcategory")
                )
            # Could also track out parameters here

    return signature


def generate_uproperty_specifiers(var_info: Dict[str, Any]) -> str:
    """Generate UPROPERTY specifiers for a variable."""
    specifiers = []

    # Editability
    if var_info.get("is_exposed", False):
        specifiers.append("EditAnywhere")
    else:
        specifiers.append("VisibleAnywhere")

    # Blueprint access
    specifiers.append("BlueprintReadWrite")

    # Category
    category = var_info.get("category", "Default")
    specifiers.append(f'Category = "{category}"')

    return ", ".join(specifiers)


def generate_ufunction_specifiers(func_info: Dict[str, Any]) -> str:
    """Generate UFUNCTION specifiers for a function."""
    specifiers = []

    if func_info.get("is_event", False):
        specifiers.append("BlueprintNativeEvent")
    elif func_info.get("is_callable", True):
        specifiers.append("BlueprintCallable")

    if func_info.get("is_pure", False):
        specifiers.append("BlueprintPure")

    category = func_info.get("category", "Default")
    specifiers.append(f'Category = "{category}"')

    return ", ".join(specifiers)


def create_migration_status(blueprint_path: str) -> Dict[str, Any]:
    """Create initial migration status tracking object."""
    name = blueprint_path.split("/")[-1]
    return {
        "blueprint": blueprint_path,
        "blueprint_name": name,
        "started": datetime.now().isoformat(),
        "status": "in_progress",
        "steps": {
            "analysis": {"status": "pending"},
            "context": {"status": "pending"},
            "codemap": {"status": "pending"},
            "generation": {"status": "pending"},
            "validation": {"status": "pending"},
            "integration": {"status": "pending"}
        },
        "outputs": {},
        "issues": [],
        "manual_steps": []
    }


def save_migration_status(status: Dict[str, Any]) -> Path:
    """Save migration status to file."""
    name = status["blueprint_name"]
    path = get_migrations_dir() / f"{name}_status.json"

    with open(path, 'w', encoding='utf-8') as f:
        json.dump(status, f, indent=2)

    return path


def load_migration_status(blueprint_name: str) -> Optional[Dict[str, Any]]:
    """Load existing migration status."""
    path = get_migrations_dir() / f"{blueprint_name}_status.json"

    if not path.exists():
        return None

    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def update_migration_step(status: Dict[str, Any], step: str, step_status: str,
                          output_path: str = None, issues: List[str] = None):
    """Update a migration step status."""
    status["steps"][step] = {
        "status": step_status,
        "timestamp": datetime.now().isoformat()
    }

    if output_path:
        status["outputs"][step] = output_path

    if issues:
        status["issues"].extend(issues)

    # Update overall status
    all_completed = all(s["status"] == "completed" for s in status["steps"].values())
    any_failed = any(s["status"] == "failed" for s in status["steps"].values())

    if all_completed:
        status["status"] = "completed"
        status["completed"] = datetime.now().isoformat()
    elif any_failed:
        status["status"] = "failed"


class CodemapBuilder:
    """Helper class to build a codemap from blueprint export."""

    def __init__(self, blueprint_name: str, parent_class: str, module: str):
        self.codemap = {
            "version": "1.0",
            "generated_at": datetime.now().isoformat(),
            "class": {
                "name": blueprint_name.replace("BP_", ""),
                "blueprint_name": blueprint_name,
                "parent": parent_class,
                "module": module,
                "api_macro": f"{module.upper()}_API"
            },
            "includes": [
                "CoreMinimal.h",
                f"{parent_class.lstrip('A').lstrip('U')}.h"
            ],
            "forward_declarations": [],
            "components": [],
            "properties": [],
            "functions": [],
            "delegates": [],
            "timelines": [],
            "implementation_order": ["Constructor", "BeginPlay"],
            "migration_notes": []
        }

    def add_include(self, include: str):
        """Add an include file."""
        if include not in self.codemap["includes"]:
            self.codemap["includes"].append(include)

    def add_forward_declaration(self, class_name: str):
        """Add a forward declaration."""
        if class_name not in self.codemap["forward_declarations"]:
            self.codemap["forward_declarations"].append(class_name)

    def add_component(self, name: str, component_type: str,
                      parent: str = None, properties: Dict = None,
                      needs_hierarchy: bool = False,
                      child_components: List[Dict] = None):
        """Add a component to the codemap.

        Args:
            needs_hierarchy: If True, component has children and should use USceneComponent base
            child_components: List of child component definitions
        """
        self.codemap["components"].append({
            "name": name,
            "type": component_type,
            "attachment": parent or "RootComponent",
            "needs_hierarchy": needs_hierarchy,
            "child_components": child_components or [],
            "default_properties": properties or {},
            "create_default_subobject": True
        })

    def add_property(self, name: str, cpp_type: str, category: str = "Default",
                     specifiers: List[str] = None, default_value: Any = None,
                     is_asset_reference: bool = False):
        """Add a property to the codemap.

        Args:
            is_asset_reference: If True, uses EditDefaultsOnly so value stays in Blueprint defaults
        """
        if is_asset_reference:
            # Asset references should be configurable in Blueprint, not hardcoded
            specs = specifiers or ["EditDefaultsOnly", "BlueprintReadOnly"]
        else:
            specs = specifiers or ["EditAnywhere", "BlueprintReadWrite"]

        self.codemap["properties"].append({
            "name": name,
            "type": cpp_type,
            "category": category,
            "specifiers": specs,
            "default_value": default_value,
            "is_asset_reference": is_asset_reference
        })

    def add_function(self, name: str, return_type: str = "void",
                     parameters: List[Dict] = None, specifiers: List[str] = None,
                     implementation_notes: str = "", node_guids: List[str] = None):
        """Add a function to the codemap."""
        self.codemap["functions"].append({
            "name": name,
            "return_type": return_type,
            "parameters": parameters or [],
            "specifiers": specifiers or ["BlueprintCallable"],
            "category": "Default",
            "implementation_notes": implementation_notes,
            "blueprint_nodes": node_guids or []
        })

        if name not in self.codemap["implementation_order"]:
            self.codemap["implementation_order"].append(name)

    def add_delegate(self, name: str, delegate_type: str = "multicast",
                     signature: str = ""):
        """Add a delegate to the codemap."""
        self.codemap["delegates"].append({
            "name": name,
            "type": delegate_type,
            "signature": signature
        })

    def add_timeline(self, name: str, curves: List[Dict],
                     update_func: str = None, finished_func: str = None):
        """Add a timeline to the codemap."""
        self.codemap["timelines"].append({
            "name": name,
            "curves": curves,
            "events": {
                "update": update_func,
                "finished": finished_func
            }
        })

    def add_note(self, note: str):
        """Add a migration note."""
        self.codemap["migration_notes"].append(note)

    def build(self) -> Dict[str, Any]:
        """Return the built codemap."""
        return self.codemap

    def save(self, blueprint_name: str) -> Path:
        """Save the codemap to file."""
        path = get_codemaps_dir() / f"{blueprint_name}_codemap.json"

        with open(path, 'w', encoding='utf-8') as f:
            json.dump(self.codemap, f, indent=2)

        return path


class CppGenerator:
    """Helper class to generate C++ code from a codemap."""

    def __init__(self, codemap: Dict[str, Any]):
        self.codemap = codemap
        self.class_info = codemap["class"]
        self.class_name = self.class_info["name"]
        self.parent_class = self.class_info["parent"]
        self.module = self.class_info["module"]
        self.api_macro = self.class_info["api_macro"]

    def generate_header(self) -> str:
        """Generate the .h file content."""
        lines = []

        # Header guard
        lines.append("// Copyright (c) 2024. All Rights Reserved.")
        lines.append("")
        lines.append("#pragma once")
        lines.append("")

        # Includes
        lines.append('#include "CoreMinimal.h"')
        for inc in self.codemap["includes"][1:]:  # Skip CoreMinimal
            lines.append(f'#include "{inc}"')
        lines.append(f'#include "{self.class_name}.generated.h"')
        lines.append("")

        # Forward declarations
        if self.codemap["forward_declarations"]:
            for fwd in self.codemap["forward_declarations"]:
                lines.append(f"class {fwd};")
            lines.append("")

        # Delegate declarations
        for delegate in self.codemap["delegates"]:
            sig = delegate.get("signature", "")
            lines.append(f'DECLARE_DYNAMIC_MULTICAST_DELEGATE{sig}({delegate["name"]});')
        if self.codemap["delegates"]:
            lines.append("")

        # Class declaration
        lines.append("/**")
        lines.append(f' * C++ base class for Blueprint: {self.class_info.get("blueprint_name", "")}')
        lines.append(" * ")
        lines.append(" * The Blueprint should reparent to this class after migration.")
        lines.append(" * Asset references are configured in Blueprint defaults (EditDefaultsOnly).")
        lines.append(" */")
        lines.append("UCLASS()")
        lines.append(f"class {self.api_macro} {self.class_name} : public {self.parent_class}")
        lines.append("{")
        lines.append("\tGENERATED_BODY()")
        lines.append("")
        lines.append("public:")
        lines.append(f"\t{self.class_name}();")
        lines.append("")

        # Components
        if self.codemap["components"]:
            lines.append("\t// Components")
            for comp in self.codemap["components"]:
                lines.append(f'\tUPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")')
                lines.append(f'\tTObjectPtr<{comp["type"]}> {comp["name"]};')
                lines.append("")

        # Properties - separate asset refs from runtime state
        asset_props = [p for p in self.codemap["properties"] if p.get("is_asset_reference")]
        state_props = [p for p in self.codemap["properties"] if not p.get("is_asset_reference")]

        if asset_props:
            lines.append("\t// Asset References (configure in Blueprint defaults)")
            for prop in asset_props:
                specs = ", ".join(prop["specifiers"])
                lines.append(f'\tUPROPERTY({specs}, Category = "{prop["category"]}")')
                lines.append(f'\t{prop["type"]} {prop["name"]};')
                lines.append("")

        if state_props:
            lines.append("\t// Runtime State")
            for prop in state_props:
                specs = ", ".join(prop["specifiers"])
                lines.append(f'\tUPROPERTY({specs}, Category = "{prop["category"]}")')
                lines.append(f'\t{prop["type"]} {prop["name"]};')
                lines.append("")

        # Delegates
        if self.codemap["delegates"]:
            lines.append("\t// Delegates")
            for delegate in self.codemap["delegates"]:
                lines.append(f'\tUPROPERTY(BlueprintAssignable, Category = "Events")')
                lines.append(f'\t{delegate["name"]} {delegate["name"].replace("F", "On", 1)};')
                lines.append("")

        # Functions
        if self.codemap["functions"]:
            lines.append("\t// Functions")
            for func in self.codemap["functions"]:
                specs = ", ".join(func["specifiers"])
                lines.append(f'\tUFUNCTION({specs}, Category = "{func["category"]}")')
                params = ", ".join([f'{p["type"]} {p["name"]}' for p in func["parameters"]])
                lines.append(f'\t{func["return_type"]} {func["name"]}({params});')
                lines.append("")

        # Protected section
        lines.append("protected:")
        lines.append("\tvirtual void BeginPlay() override;")

        # Check if Tick is needed
        has_tick = any(f["name"] == "Tick" for f in self.codemap["functions"])
        if has_tick or self.codemap["timelines"]:
            lines.append("\tvirtual void Tick(float DeltaTime) override;")

        lines.append("")

        # Private section
        lines.append("private:")

        # Timelines
        if self.codemap["timelines"]:
            lines.append("\t// Timelines")
            for tl in self.codemap["timelines"]:
                lines.append(f'\tFTimeline {tl["name"]};')
                for curve in tl["curves"]:
                    lines.append(f'\tUPROPERTY()')
                    lines.append(f'\tUCurveFloat* {curve["name"]}Curve;')
            lines.append("")

        lines.append("};")

        return "\n".join(lines)

    def generate_source(self) -> str:
        """Generate the .cpp file content."""
        lines = []

        # Includes
        lines.append("// Copyright (c) 2024. All Rights Reserved.")
        lines.append("")
        lines.append(f'#include "{self.class_name}.h"')
        lines.append("")

        # Constructor
        lines.append(f"{self.class_name}::{self.class_name}()")
        lines.append("{")

        # Tick setup
        has_tick = any(f["name"] == "Tick" for f in self.codemap["functions"]) or self.codemap["timelines"]
        lines.append(f"\tPrimaryActorTick.bCanEverTick = {'true' if has_tick else 'false'};")
        lines.append("")

        # Create components
        if self.codemap["components"]:
            lines.append("\t// Create components")
            for i, comp in enumerate(self.codemap["components"]):
                lines.append(f'\t{comp["name"]} = CreateDefaultSubobject<{comp["type"]}>(TEXT("{comp["name"]}"));')
                if i == 0:
                    lines.append(f"\tRootComponent = {comp['name']};")
                elif comp.get("attachment"):
                    lines.append(f'\t{comp["name"]}->SetupAttachment({comp["attachment"]});')
            lines.append("")

        # Set default values (NOT for asset references - those stay in BP defaults)
        state_props = [p for p in self.codemap["properties"]
                       if not p.get("is_asset_reference") and p.get("default_value") is not None]
        if state_props:
            lines.append("\t// Set defaults (asset refs configured in Blueprint)")
            for prop in state_props:
                lines.append(f'\t{prop["name"]} = {prop["default_value"]};')
            lines.append("")

        lines.append("}")
        lines.append("")

        # BeginPlay
        lines.append(f"void {self.class_name}::BeginPlay()")
        lines.append("{")
        lines.append("\tSuper::BeginPlay();")
        lines.append("")
        lines.append("\t// TODO: Implement BeginPlay logic from Blueprint")
        lines.append("}")
        lines.append("")

        # Tick
        if has_tick:
            lines.append(f"void {self.class_name}::Tick(float DeltaTime)")
            lines.append("{")
            lines.append("\tSuper::Tick(DeltaTime);")
            lines.append("")
            if self.codemap["timelines"]:
                lines.append("\t// Update timelines")
                for tl in self.codemap["timelines"]:
                    lines.append(f"\t{tl['name']}.TickTimeline(DeltaTime);")
                lines.append("")
            lines.append("\t// TODO: Implement Tick logic from Blueprint")
            lines.append("}")
            lines.append("")

        # Other functions
        for func in self.codemap["functions"]:
            if func["name"] in ["BeginPlay", "Tick"]:
                continue

            params = ", ".join([f'{p["type"]} {p["name"]}' for p in func["parameters"]])
            lines.append(f'{func["return_type"]} {self.class_name}::{func["name"]}({params})')
            lines.append("{")

            if func.get("implementation_notes"):
                lines.append(f"\t// {func['implementation_notes']}")

            if func.get("blueprint_nodes"):
                lines.append(f"\t// Source nodes: {', '.join(func['blueprint_nodes'][:3])}")

            lines.append("\t// TODO: Implement function logic from Blueprint")

            if func["return_type"] != "void":
                lines.append(f"\treturn {{}};  // Default return")

            lines.append("}")
            lines.append("")

        return "\n".join(lines)

    def save(self, blueprint_name: str, dry_run: bool = True) -> Dict[str, Path]:
        """Save generated files."""
        if dry_run:
            output_dir = get_generated_dir() / blueprint_name
        else:
            # Would write to actual Source directory
            output_dir = get_generated_dir() / blueprint_name

        output_dir.mkdir(parents=True, exist_ok=True)

        header_path = output_dir / f"{self.class_name}.h"
        source_path = output_dir / f"{self.class_name}.cpp"

        with open(header_path, 'w', encoding='utf-8') as f:
            f.write(self.generate_header())

        with open(source_path, 'w', encoding='utf-8') as f:
            f.write(self.generate_source())

        return {
            "header": header_path,
            "source": source_path
        }
