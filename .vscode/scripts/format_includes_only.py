import sys
import re
import subprocess
from pathlib import Path


INCLUDE_RE = re.compile(r'^\s*#include\s*[<"]([^">]+)[">]')
IMPORT_RE = re.compile(r"^\s*(?:export\s+)?import\s+.+;\s*(?://.*)?$")
SOURCE_STEM_RE = re.compile(
    r"^(?P<stem>.+?)(?:\.test)?\.(?:c|cc|cpp|cxx|m|mm|cppm|ixx|mpp)$"
)
MODULE_FILE_RE = re.compile(r"^.+\.(?:cppm|ixx|mpp)$")
MODULE_DECL_RE = re.compile(r"^\s*(?:export\s+)?module\s+.+;\s*$")
GLOBAL_MODULE_FRAGMENT_RE = re.compile(r"^\s*module\s*;\s*$")
PRIVATE_MODULE_FRAGMENT_RE = re.compile(r"^\s*module\s*:\s*private\s*;\s*$")


def source_stem(filepath: str) -> str:
    m = SOURCE_STEM_RE.match(Path(filepath).name)
    return m.group("stem") if m else ""


def is_module_file(filepath: str) -> bool:
    return bool(MODULE_FILE_RE.match(Path(filepath).name))


def find_module_declaration_index(lines: list[str]) -> int:
    for i, line in enumerate(lines):
        if GLOBAL_MODULE_FRAGMENT_RE.match(line):
            continue
        if MODULE_DECL_RE.match(line):
            return i
    return -1


def relevant_include_indices(lines: list[str], filepath: str) -> list[int]:
    include_indices = [i for i, line in enumerate(lines) if INCLUDE_RE.match(line)]
    if not include_indices:
        return []

    if not is_module_file(filepath):
        return include_indices

    module_decl_idx = find_module_declaration_index(lines)
    if module_decl_idx == -1:
        return include_indices

    # For module units, only process includes from the global module fragment.
    return [i for i in include_indices if i < module_decl_idx]


def import_sort_range(lines: list[str], filepath: str) -> tuple[int, int]:
    """Return [start, end) range where import sorting is allowed."""
    if not lines:
        return (0, 0)

    if not is_module_file(filepath):
        return (0, len(lines))

    module_decl_idx = find_module_declaration_index(lines)
    if module_decl_idx == -1:
        return (0, len(lines))

    private_idx = -1
    for i in range(module_decl_idx + 1, len(lines)):
        if PRIVATE_MODULE_FRAGMENT_RE.match(lines[i]):
            private_idx = i
            break

    if private_idx == -1:
        return (module_decl_idx + 1, len(lines))

    return (module_decl_idx + 1, private_idx)


def normalize_import_sort_key(line: str) -> str:
    # Keep case-insensitive lexical ordering and ignore leading indentation.
    return line.strip().lower()


def sort_import_blocks(lines: list[str], filepath: str) -> bool:
    """Sort contiguous import statement blocks in the allowed scope."""
    start, end = import_sort_range(lines, filepath)
    if end - start <= 1:
        return False

    changed = False
    i = start
    while i < end:
        if not IMPORT_RE.match(lines[i]):
            i += 1
            continue

        block_start = i
        i += 1
        while i < end and IMPORT_RE.match(lines[i]):
            i += 1
        block_end = i

        block = lines[block_start:block_end]
        sorted_block = sorted(block, key=normalize_import_sort_key)
        if sorted_block != block:
            lines[block_start:block_end] = sorted_block
            changed = True

    return changed


def include_path(line: str) -> str:
    m = INCLUDE_RE.match(line)
    return m.group(1) if m else ""


def move_main_include_first(lines: list[str], filepath: str) -> bool:
    stem = source_stem(filepath)
    if not stem:
        return False

    include_indices = relevant_include_indices(lines, filepath)
    if len(include_indices) < 2:
        return False

    expected = f"{stem}.h"
    main_idx = -1
    for i in include_indices:
        inc = include_path(lines[i])
        if inc and Path(inc).name == expected:
            main_idx = i
            break

    if main_idx == -1 or main_idx == include_indices[0]:
        return False

    main_line = lines.pop(main_idx)
    lines.insert(include_indices[0], main_line)

    # 在主头文件后插入空行（如果紧接着还有其他 include 且没有空行）
    main_pos = include_indices[0]
    if main_pos + 1 < len(lines):
        next_line = lines[main_pos + 1]
        # 只有在下一行是 include 且当前行不是空行时，才插入空行
        if INCLUDE_RE.match(next_line):
            lines.insert(main_pos + 1, "\n")

    return True


def cleanup_trailing_blank_lines_after_includes(
    lines: list[str], filepath: str
) -> None:
    """Remove excess blank lines after the last include statement."""
    include_indices = relevant_include_indices(lines, filepath)
    if not include_indices:
        return

    last_include_idx = include_indices[-1]
    # Remove consecutive blank lines immediately after the last include
    # Keep only one blank line
    idx = last_include_idx + 1
    blank_count = 0
    while idx < len(lines) and lines[idx].strip() == "":
        blank_count += 1
        idx += 1

    # If there are more than 1 blank line, remove the excess
    if blank_count > 1:
        for _ in range(blank_count - 1):
            lines.pop(last_include_idx + 2)


def normalize_blank_lines_inside_include_block(lines: list[str], filepath: str) -> None:
    """Collapse consecutive blank lines within include block to a single blank line."""
    include_indices = relevant_include_indices(lines, filepath)
    if len(include_indices) < 2:
        return

    first_include_idx = include_indices[0]
    last_include_idx = include_indices[-1]

    idx = first_include_idx + 1
    while idx <= last_include_idx and idx < len(lines):
        if lines[idx].strip() != "":
            idx += 1
            continue

        run_start = idx
        while idx <= last_include_idx and idx < len(lines) and lines[idx].strip() == "":
            idx += 1
        run_length = idx - run_start

        if run_length > 1:
            del lines[run_start + 1 : run_start + run_length]
            removed = run_length - 1
            last_include_idx -= removed
            idx = run_start + 1


def main(filepath):
    start_line = -1
    end_line = -1

    lines = []

    # 扫描文件，找出 #include 所在的行范围
    with open(filepath, "r", encoding="utf-8") as f:
        lines = f.readlines()
        include_indices = relevant_include_indices(lines, filepath)
        if include_indices:
            start_line = include_indices[0] + 1  # clang-format 行号从 1 开始
            end_line = include_indices[-1] + 1

    # 如果找到了头文件，就调用 clang-format 局部格式化
    if start_line != -1 and end_line != -1:
        cmd = ["clang-format", "-i", f"--lines={start_line}:{end_line}", filepath]
        try:
            subprocess.run(cmd, check=True)

            # clang-format 之后再强制把主头文件放到第一个 include。
            with open(filepath, "r", encoding="utf-8") as f:
                formatted_lines = f.readlines()
            # C++ module units have different structure; do not enforce TU-style
            # "main header first" ordering on .cppm/.ixx/.mpp files.
            if not is_module_file(filepath):
                move_main_include_first(formatted_lines, filepath)
            normalize_blank_lines_inside_include_block(formatted_lines, filepath)
            cleanup_trailing_blank_lines_after_includes(formatted_lines, filepath)
            sort_import_blocks(formatted_lines, filepath)
            with open(filepath, "w", encoding="utf-8") as f:
                f.writelines(formatted_lines)
        except subprocess.CalledProcessError as e:
            print(f"clang-format 执行失败: {e}")
        except FileNotFoundError:
            print("找不到 clang-format，请确保它已加入系统环境变量")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        main(sys.argv[1])
