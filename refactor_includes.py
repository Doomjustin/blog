#!/usr/bin/env python3
"""
Add module-prefix to all project-internal #include directives.

Rules:
  - <header.h>  and "header.h" in src/ → <module/header.h>
  - For headers that exist in multiple modules (read_awaiter.h, write_awaiter.h,
    operations.h), use the file's own module; fall back to the first owning module.
  - blog.h, and already-prefixed includes are left untouched.
  - demo/src/ is standalone and skipped entirely.
"""

import re
from pathlib import Path

ROOT = Path("/home/doom/blog")

MODULES = {
    "common": [
        "as_string.h",
        "chrono_duration.h",
        "coding.h",
        "common.h",
        "exceptions.h",
        "fixed_string.h",
        "format.h",
        "hash.h",
        "log.h",
        "lru_cache.h",
        "mpsc_queue.h",
        "named_type.h",
        "overloads.h",
        "random.h",
        "spinlock.h",
        "tracking_resource.h",
        "utility.h",
    ],
    "async": [
        "all.h",
        "any.h",
        "async.h",
        "awaitable.h",
        "buffer.h",
        "channel.h",
        "channel_pipe.h",
        "co_spawn.h",
        "detached_task.h",
        "final_awaiter.h",
        "io_context.h",
        "loop_operation.h",
        "operation.h",
        "poll_awaiter.h",
        "post.h",
        "read_awaiter.h",
        "retry.h",
        "run.h",
        "run_on_thread.h",
        "scope.h",
        "shift_to.h",
        "signals.h",
        "single_operation.h",
        "sleep_for.h",
        "stop_requested_awaiter.h",
        "stop_then.h",
        "task.h",
        "this_coroutine.h",
        "thread_safe_channel.h",
        "timeout.h",
        "when_all.h",
        "when_any.h",
        "write_awaiter.h",
        "write_sequence_awaiter.h",
    ],
    "net": [
        "accept_awaiter.h",
        "acceptor.h",
        "base_socket.h",
        "linger.h",
        "net.h",
        "operations.h",
        "option.h",
        "pooled_buffer.h",
        "query_endpoint.h",
        "receive_all_awaiter.h",
        "receive_awaiter.h",
        "receive_stream.h",
        "send_all_awaiter.h",
        "send_all_zc_awaiter.h",
        "send_awaiter.h",
        "send_zc_awaiter.h",
        "transfer.h",
        "zero_copy.h",
        "ip/address.h",
        "ip/datagram_socket.h",
        "ip/endpoint.h",
        "ip/multicast.h",
        "ip/stream_socket.h",
        "ip/tcp.h",
    ],
    "file_system": [
        "base_file.h",
        "file_system.h",
        "operations.h",
        "random_access_file.h",
        "read_all_awaiter.h",
        "read_at_awaiter.h",
        "read_awaiter.h",
        "stream_file.h",
        "write_all_awaiter.h",
        "write_at_awaiter.h",
        "write_awaiter.h",
    ],
}

# Headers that exist in more than one module — need context to resolve
from collections import defaultdict

header_to_modules: dict[str, list[str]] = defaultdict(list)
for mod, headers in MODULES.items():
    for h in headers:
        header_to_modules[h].append(mod)

AMBIGUOUS = {h for h, mods in header_to_modules.items() if len(mods) > 1}

# All known module prefixes (for detecting already-prefixed includes)
MODULE_PREFIXES = tuple(f"{m}/" for m in MODULES)

# Headers that live at the src/ root and must NOT be remapped
TOPLEVEL_KEEP = {"blog.h"}


def get_file_module(filepath: Path) -> str | None:
    """Return which module a source file belongs to (by directory name)."""
    for part in filepath.parts:
        if part in MODULES:
            return part
    return None


def resolve(header: str, file_module: str | None) -> str | None:
    """Return the prefixed include path, or None if header is not a project header."""
    if header in TOPLEVEL_KEEP:
        return None
    if header.startswith(MODULE_PREFIXES):
        return None  # already prefixed
    owners = header_to_modules.get(header)
    if not owners:
        return None  # not a project header
    if len(owners) == 1:
        return f"{owners[0]}/{header}"
    # Ambiguous: prefer the file's own module
    if file_module in owners:
        return f"{file_module}/{header}"
    # Fallback: first owning module (shouldn't hit for external files)
    return f"{owners[0]}/{header}"


INCLUDE_RE = re.compile(r'^(\s*#\s*include\s*)([<"])(.*?)[>"](\s*)$')


def process_file(filepath: Path, file_module: str | None) -> bool:
    text = filepath.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    new_lines = []
    changed = False
    for line in lines:
        m = INCLUDE_RE.match(line)
        if m:
            prefix, bracket, header, trail = m.groups()
            new_header = resolve(header, file_module)
            if new_header is not None:
                new_line = (
                    f"{prefix}<{new_header}>{trail}\n"
                    if not trail.strip()
                    else f"{prefix}<{new_header}>{trail}"
                )
                # Preserve original line ending
                orig_ending = "\n" if line.endswith("\n") else ""
                new_line = f"{prefix}<{new_header}>" + orig_ending
                if new_line != line:
                    changed = True
                    line = new_line
        new_lines.append(line)
    if changed:
        filepath.write_text("".join(new_lines), encoding="utf-8")
    return changed


SCAN_DIRS = ["src", "tutorial", "demo", "examples"]
EXTENSIONS = {".h", ".cpp"}
SKIP_DIRS = {
    ROOT / "demo" / "src",  # standalone demo, not part of the library
}

modified = []
for scan_dir_name in SCAN_DIRS:
    scan_dir = ROOT / scan_dir_name
    if not scan_dir.exists():
        continue
    for fp in sorted(scan_dir.rglob("*")):
        if not fp.is_file() or fp.suffix not in EXTENSIONS:
            continue
        # Skip standalone demo
        if any(fp.is_relative_to(skip) for skip in SKIP_DIRS):
            continue
        file_module = get_file_module(fp)
        if process_file(fp, file_module):
            modified.append(fp)

print(f"Modified {len(modified)} files:")
for f in sorted(modified):
    print(f"  {f.relative_to(ROOT)}")
