"""
Utilities for working with Unreal Engine connections.

SIMPLIFIED VERSION: All requests are serialized with a global lock.
One request at a time - no concurrency, no race conditions.
"""

import logging
import socket
import json
import threading
import os
import datetime
from typing import Dict, Any, Optional

# Get logger
logger = logging.getLogger("UnrealMCP")

# Configuration
UNREAL_HOST = "127.0.0.1"
UNREAL_PORT = 55557

# Debug log file
_debug_log_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mcp_debug.log")

def _debug(msg: str):
    """Write debug message to file with timestamp."""
    try:
        with open(_debug_log_path, "a", encoding="utf-8") as f:
            f.write(f"[{datetime.datetime.now()}] {msg}\n")
            f.flush()
    except:
        pass

# Global lock - only ONE request can be in-flight at a time
_request_lock = threading.Lock()


def _send_tcp_command(command_name: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """
    Send a single TCP command to Unreal and wait for response.
    This is the core function - no retries, no complexity.
    """
    sock = None
    try:
        _debug(f"TCP [{command_name}] Creating socket...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(30)  # 30 second timeout for everything
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        _debug(f"TCP [{command_name}] Connecting to {UNREAL_HOST}:{UNREAL_PORT}...")
        sock.connect((UNREAL_HOST, UNREAL_PORT))
        _debug(f"TCP [{command_name}] Connected!")

        # Send command
        command_obj = {"type": command_name, "params": params or {}}
        command_json = json.dumps(command_obj)
        _debug(f"TCP [{command_name}] Sending {len(command_json)} bytes...")
        sock.sendall(command_json.encode('utf-8'))
        _debug(f"TCP [{command_name}] Sent! Waiting for response...")

        # Receive response - simple blocking recv until we get complete JSON
        chunks = []
        recv_count = 0
        while True:
            recv_count += 1
            _debug(f"TCP [{command_name}] recv() call #{recv_count}...")
            chunk = sock.recv(8192)
            _debug(f"TCP [{command_name}] recv() returned {len(chunk) if chunk else 0} bytes")

            if not chunk:
                _debug(f"TCP [{command_name}] Connection closed by server")
                break
            chunks.append(chunk)

            # Try to parse as JSON - if successful, we have complete response
            data = b''.join(chunks)
            try:
                response = json.loads(data.decode('utf-8'))
                _debug(f"TCP [{command_name}] SUCCESS! Got complete JSON ({len(data)} bytes)")
                return response
            except json.JSONDecodeError:
                _debug(f"TCP [{command_name}] Partial JSON ({len(data)} bytes), continuing...")
                continue

        # If we get here, connection closed before complete JSON
        _debug(f"TCP [{command_name}] Connection closed, checking if we have complete data...")
        if chunks:
            data = b''.join(chunks)
            try:
                response = json.loads(data.decode('utf-8'))
                _debug(f"TCP [{command_name}] SUCCESS after close ({len(data)} bytes)")
                return response
            except:
                _debug(f"TCP [{command_name}] FAILED - incomplete JSON after close")
                pass

        _debug(f"TCP [{command_name}] FAILED - no complete response")
        return {"status": "error", "error": "Connection closed before complete response"}

    except socket.timeout:
        _debug(f"TCP [{command_name}] TIMEOUT!")
        return {"status": "error", "error": "Connection timeout"}
    except ConnectionRefusedError:
        _debug(f"TCP [{command_name}] CONNECTION REFUSED!")
        return {"status": "error", "error": "Connection refused - is Unreal Engine running?"}
    except Exception as e:
        _debug(f"TCP [{command_name}] EXCEPTION: {e}")
        return {"status": "error", "error": str(e)}
    finally:
        _debug(f"TCP [{command_name}] Closing socket...")
        if sock:
            try:
                sock.close()
            except:
                pass
        _debug(f"TCP [{command_name}] Socket closed")


def send_unreal_command(command_name: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """
    Send a command to Unreal Engine.

    IMPORTANT: This function is serialized - only one request at a time.
    If multiple threads call this, they will queue up and execute one by one.
    """
    _debug(f"QUEUE [{command_name}] Entered send_unreal_command")
    _debug(f"QUEUE [{command_name}] Waiting for lock...")

    # Try to acquire lock with timeout to detect deadlocks
    acquired = _request_lock.acquire(timeout=60)
    if not acquired:
        _debug(f"QUEUE [{command_name}] DEADLOCK! Could not acquire lock after 60s")
        return {"status": "error", "error": "Request queue deadlock - lock not released by previous request"}

    _debug(f"QUEUE [{command_name}] Got lock!")

    try:
        _debug(f"QUEUE [{command_name}] Calling _send_tcp_command...")

        # Simple retry: try up to 2 times
        for attempt in range(2):
            response = _send_tcp_command(command_name, params)

            # Check if it's an error we should retry
            if response.get("status") == "error":
                error = response.get("error", "")
                if attempt == 0 and ("timeout" in error.lower() or "refused" in error.lower()):
                    logger.warning(f"Retrying after error: {error}")
                    import time
                    time.sleep(0.5)
                    continue

            # Success or non-retryable error
            break

        # Handle nested error format from C++ MCPErrorHandler
        if response.get("success") is False:
            error_field = response.get("error")
            if isinstance(error_field, dict):
                error_message = (error_field.get("errorMessage") or
                               error_field.get("errorDetails") or
                               error_field.get("message") or
                               "Unknown error")
                return {"status": "error", "error": error_message}
            elif isinstance(error_field, str):
                return {"status": "error", "error": error_field}
            else:
                return {"status": "error", "error": response.get("message", "Unknown error")}

        _debug(f"QUEUE [{command_name}] Done, returning response")
        return response

    finally:
        # ALWAYS release the lock, even if an exception occurred
        _request_lock.release()
        _debug(f"QUEUE [{command_name}] Released lock")


# Legacy compatibility
def get_unreal_engine_connection():
    """Legacy - not used in simplified version."""
    return None

def reset_connection():
    """Legacy - not used in simplified version."""
    pass


# Cache for project info
_project_info_cache: Dict[str, Any] = {}

def get_project_module_name() -> str:
    """Get the current Unreal project's module name."""
    global _project_info_cache

    if "module_name" in _project_info_cache:
        return _project_info_cache["module_name"]

    try:
        response = send_unreal_command("get_project_dir", {})
        if response and response.get("success") and response.get("project_name"):
            module_name = response["project_name"]
            _project_info_cache["module_name"] = module_name
            _project_info_cache["module_path"] = response.get("module_path", f"/Script/{module_name}")
            return module_name
    except Exception as e:
        logger.warning(f"Failed to get project module name: {e}")

    return "MyGame"


def get_project_module_path() -> str:
    """Get the full script module path for the current project."""
    global _project_info_cache

    if "module_path" in _project_info_cache:
        return _project_info_cache["module_path"]

    module_name = get_project_module_name()
    return _project_info_cache.get("module_path", f"/Script/{module_name}")
