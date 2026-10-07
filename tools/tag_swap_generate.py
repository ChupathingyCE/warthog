#!/usr/bin/env python3
"""Generates the tables that put a map's tags in a big-endian console's byte
order (port/console/game/tag_byte_swap_tables.c; the walker is
port/console/game/tag_byte_swap.c).

The maps are the Xbox's, little-endian, and the game reads its tags as C
structures. The decompilation has no tag field definitions to swap them by
(the shipped game had none), but it has the structures, size-checked, and
the code that reads them. So:

- every structure's layout comes from the compiler (its record layout dumps:
  each field's offset and type), so a field of 2, 4 or 8 bytes is swapped
  where the game would read it;
- a tag group's root structures are those the game reads a tag of that
  group as (tag_get(GROUP, index) given to a `struct T *`), merged with its
  parent groups' (a weapon is read as an object too), the parents being the
  maps' own (each tag instance lists its group's parents);
- a tag block's element structure is the one the game reads it as
  (TAG_BLOCK_GET_ELEMENT(&x->field, i, struct T), and an element pointer
  made from the block's address), so the blocks the game reads are swapped,
  element by element; one it never reads keeps its count, address and
  definition swapped and its elements as they are.

Run on a checkout configured for the Xbox 360 (`ninja xbox360` builds): it
compiles each game unit once more with the same flags (clang's
-fdump-record-layouts-complete and -ast-dump=json; build/tag_swap holds what
each gave, until the unit changes), and with --maps, checks the tables
against the maps' tags (which blocks hold elements the tables do not
describe). Nothing it reads from the maps is written into the tables but
their group hierarchy, a fact of the game's tag groups.

usage: tools/tag_swap_generate.py [--maps DIR] [--output FILE] [--jobs N]
"""

import argparse
import hashlib
import json
import os
import pickle
import re
import shlex
import struct
import subprocess
import sys
import zlib
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "build" / "tag_swap"
OUTPUT = ROOT / "port" / "console" / "game" / "tag_byte_swap_tables.c"

# the scalar types the tags' structures use, by their spelling in the
# record layouts, and their sizes (the Xbox's: MSVC, 32-bit)
SCALARS = {
    "char": 1, "signed char": 1, "unsigned char": 1, "byte": 1, "boolean": 1, "bool": 1, "_Bool": 1,
    "short": 2, "unsigned short": 2, "word": 2, "wchar_t": 2, "short int": 2,
    "int": 4, "unsigned int": 4, "long": 4, "unsigned long": 4, "real": 4, "float": 4, "dword": 4,
    "long int": 4, "unsigned long int": 4, "tag": 4, "datum_index": 4, "string_id": 4, "angle": 4,
    "fraction": 4, "argb_color": 4, "real_fraction": 4, "DWORD": 4, "LONG": 4, "ULONG": 4, "UINT": 4,
    "WORD": 2, "BYTE": 1, "BOOL": 4, "FLOAT": 4, "size_t": 4, "time_t": 4, "__time32_t": 4,
    "long long": 8, "unsigned long long": 8, "__int64": 8, "unsigned __int64": 8, "double": 8,
    "__time64_t": 8, "long double": 8, "qword": 8,
}

LAYOUT_LINE = re.compile(r"^\s*(\d+)(?::(\d+)-(\d+))? \|(\s*)(.*?)\s*$")
SIZE_LINE = re.compile(r"\[sizeof=(\d+), (?:dsize=\d+, )?align=(\d+)")


def run(command: List[str]) -> str:
    return subprocess.run(command, capture_output=True, text=True, cwd=ROOT).stdout


# ---------- the units and their flags

def game_units() -> List[Tuple[str, List[str]]]:
    """(source, compile command) for each game unit of the Xbox 360 build"""
    units = []
    for line in run(["ninja", "-t", "commands", "xbox360-game-objects"]).splitlines():
        if " -c " not in line or "clang" not in line:
            continue
        command = line.split(" && ")[0]
        words = shlex.split(command)
        source = words[words.index("-c") + 1]
        if not (source.startswith("source/") or source.startswith("port/linux/game/")):
            continue
        flags = [w for i, w in enumerate(words) if w not in ("-c",) and not w.endswith(".o")
                 and words[i - 1] not in ("-o", "-MF", "-c") and w not in ("-MMD", "-MF", "-o")]
        flags = [w for w in flags if w != source]
        units.append((source, flags))
    return units


# ---------- record layouts

class Node:
    __slots__ = ("offset", "bits", "type", "name", "children")

    def __init__(self, offset: int, bits: Optional[Tuple[int, int]], type_: str, name: str):
        self.offset = offset
        self.bits = bits
        self.type = type_
        self.name = name
        self.children: List["Node"] = []


def split_type_name(text: str) -> Tuple[str, str]:
    """'struct tag_block attachments' -> ('struct tag_block', 'attachments');
    an unnamed member has no name"""
    if text.endswith(")") or " " not in text:
        return text, ""
    type_, name = text.rsplit(" ", 1)
    if name.startswith("[") or name in ("int", "long", "short", "char", "double") or type_ in ("struct", "union",
                                                                                                 "unsigned"):
        return text, ""
    return type_, name


def parse_layouts(text: str) -> Dict[str, Tuple[int, Node]]:
    """record type -> (size, tree) from clang's dumps"""
    records: Dict[str, Tuple[int, Node]] = {}
    lines = text.splitlines()
    index = 0
    while index < len(lines):
        if "*** Dumping AST Record Layout" not in lines[index]:
            index += 1
            continue
        index += 1
        stack: List[Tuple[int, Node]] = []
        top: Optional[Node] = None
        while index < len(lines):
            line = lines[index]
            size_match = SIZE_LINE.search(line)
            if size_match:
                if top is not None:
                    records.setdefault(top.type, (int(size_match.group(1)), top))
                index += 1
                break
            match = LAYOUT_LINE.match(line)
            index += 1
            if not match:
                continue
            offset = int(match.group(1))
            bits = (int(match.group(2)), int(match.group(3))) if match.group(2) else None
            depth = len(match.group(4))
            text_ = match.group(5)
            if top is None:
                top = Node(offset, None, text_, "")
                stack = [(depth, top)]
                continue
            type_, name = split_type_name(text_)
            node = Node(offset, bits, type_, name)
            while stack and stack[-1][0] >= depth:
                stack.pop()
            stack[-1][1].children.append(node)
            stack.append((depth, node))
    return records


# ---------- what the code reads

def strip(node: dict) -> dict:
    while node.get("kind") in ("ImplicitCastExpr", "ParenExpr", "CStyleCastExpr") and node.get("inner"):
        node = node["inner"][0]
    return node


def type_of(node: dict) -> str:
    t = node.get("type", {})
    return t.get("desugaredQualType") or t.get("qualType", "")


def record_name(type_: str) -> str:
    """'const struct x *' -> 'struct x'"""
    type_ = type_.replace("const ", "").replace("volatile ", "").strip()
    while type_.endswith("*"):
        type_ = type_[:-1].strip()
    return type_


def member_of(node: dict) -> Optional[Tuple[str, str]]:
    """(owner record, field) of a member expression"""
    node = strip(node)
    if node.get("kind") == "UnaryOperator" and node.get("opcode") == "&":
        node = strip(node["inner"][0])
    if node.get("kind") != "MemberExpr":
        return None
    base = node["inner"][0]
    return record_name(type_of(base)), node["name"]


ENUM_VALUES: Dict[str, int] = {}


def collect_enum_values(node) -> None:
    """every enumerator's value in a unit (the groups' tags are enumerators)"""
    stack = [node]
    while stack:
        item = stack.pop()
        if not isinstance(item, dict):
            continue
        if item.get("kind") == "EnumConstantDecl":
            for child in item.get("inner", []) or []:
                if "value" in child:
                    try:
                        ENUM_VALUES[item["id"]] = int(child["value"]) & 0xFFFFFFFF
                    except ValueError:
                        pass
                    break
        stack.extend(item.get("inner", []) or [])


def integer_of(node: dict) -> Optional[int]:
    node = strip(node)
    if node.get("kind") == "DeclRefExpr":
        return ENUM_VALUES.get(node.get("referencedDecl", {}).get("id", ""))
    while node.get("kind") == "ConstantExpr" and node.get("inner"):
        if "value" in node:
            break
        node = strip(node["inner"][0])
    value = node.get("value")
    if node.get("kind") in ("IntegerLiteral", "CharacterLiteral", "ConstantExpr") and value is not None:
        try:
            return int(value) & 0xFFFFFFFF
        except ValueError:
            return None
    return None


def callee(node: dict) -> str:
    if node.get("kind") != "CallExpr" or not node.get("inner"):
        return ""
    target = strip(node["inner"][0])
    return target.get("referencedDecl", {}).get("name", "") if target.get("kind") == "DeclRefExpr" else ""


ACCESSORS = ("tag_block_get_element", "tag_block_get_element_with_size")
GETTERS = ("tag_get", "tag_get_definition")


def pointee(type_: str) -> Optional[str]:
    """the type a pointer type points at, or None (void, or not a pointer)"""
    type_ = type_.strip()
    if not type_.endswith("*"):
        return None
    target = record_name(type_[:-1])
    if target in ("void", "") or "(" in target:
        return None
    return target


def collect_ast(tree: dict, elements: Dict[tuple, Set[str]], roots: Dict[int, Set[str]],
                calls: Dict[tuple, Set[tuple]]) -> None:
    """what the unit reads: a block's elements as (block, type); a group's
    tags as (group tag, type); blocks given to functions, (function,
    argument) -> blocks. A block is ('member', owner record, field) or
    ('param', function, index)."""

    def visit_function(function: dict) -> None:
        name = function.get("name", "")
        parameters: Dict[str, int] = {}
        locals_: Dict[str, tuple] = {}
        index = 0
        for child in function.get("inner", []) or []:
            if child.get("kind") == "ParmVarDecl":
                parameters[child.get("id")] = index
                index += 1

        def block_key(expression: dict) -> Optional[tuple]:
            expression = strip(expression)
            if expression.get("kind") == "UnaryOperator" and expression.get("opcode") == "&":
                expression = strip(expression["inner"][0])
                if expression.get("kind") == "MemberExpr" and record_name(type_of(expression)) == "struct tag_block":
                    owner = record_name(type_of(expression["inner"][0]))
                    return ("member", owner, expression["name"])
                return None
            if expression.get("kind") == "MemberExpr" and record_name(type_of(expression)) == "struct tag_block":
                owner = record_name(type_of(expression["inner"][0]))
                return ("member", owner, expression["name"])
            if expression.get("kind") == "DeclRefExpr" and type_of(expression).endswith("*") and \
                    record_name(type_of(expression)) == "struct tag_block":
                declaration = expression.get("referencedDecl", {})
                if declaration.get("id") in parameters:
                    return ("param", name, parameters[declaration["id"]])
                return locals_.get(declaration.get("id"))
            return None

        def address_block(expression: dict) -> Optional[tuple]:
            """the block whose address expression is (x.field.address)"""
            expression = strip(expression)
            if expression.get("kind") == "MemberExpr" and expression.get("name") == "address":
                return block_key(expression["inner"][0])
            return None

        def visit(node: dict) -> None:
            kind = node.get("kind")
            if kind == "VarDecl" and node.get("inner") and record_name(node.get("type", {}).get("qualType", "")) \
                    == "struct tag_block" and node.get("type", {}).get("qualType", "").endswith("*"):
                key = block_key(node["inner"][0])
                if key:
                    locals_[node.get("id")] = key
            if kind == "BinaryOperator" and node.get("opcode") == "=" and len(node.get("inner", [])) == 2:
                left = strip(node["inner"][0])
                if left.get("kind") == "DeclRefExpr" and type_of(left).endswith("*") and \
                        record_name(type_of(left)) == "struct tag_block":
                    key = block_key(node["inner"][1])
                    declaration = left.get("referencedDecl", {}).get("id")
                    if key and declaration not in parameters:
                        locals_[declaration] = key
            if kind in ("ImplicitCastExpr", "CStyleCastExpr") and node.get("castKind") == "BitCast":
                target = pointee(type_of(node))
                inner = strip(node["inner"][0]) if node.get("inner") else {}
                function_name = callee(inner)
                if target and function_name:
                    arguments = inner["inner"][1:]
                    if function_name == "xbox_pointer" and arguments:
                        key = address_block(arguments[0])
                        if key:
                            elements.setdefault(key, set()).add(target)
                    elif function_name in ACCESSORS and arguments:
                        key = block_key(arguments[0])
                        if key:
                            elements.setdefault(key, set()).add(target)
                    elif function_name in GETTERS and len(arguments) >= 2 and \
                            (target.startswith("struct ") or target.startswith("union ")):
                        group = integer_of(arguments[0])
                        if group is not None:
                            roots.setdefault(group, set()).add(target)
            function_name = callee(node)
            if function_name:
                arguments = node["inner"][1:]
                if function_name == "tag_block_get_element_with_size" and len(arguments) == 3:
                    key = block_key(arguments[0])
                    size = strip(arguments[2])
                    if key and size.get("kind") == "UnaryExprOrTypeTraitExpr" and size.get("name") == "sizeof":
                        argument_type = size.get("argType", {})
                        element = argument_type.get("desugaredQualType") or argument_type.get("qualType", "")
                        elements.setdefault(key, set()).add(record_name(element))
                elif function_name not in ACCESSORS + GETTERS + ("xbox_pointer",):
                    for position, argument in enumerate(arguments):
                        key = block_key(argument)
                        if key:
                            calls.setdefault((function_name, position), set()).add(key)
            for child in node.get("inner", []) or []:
                if isinstance(child, dict):
                    visit(child)

        for child in function.get("inner", []) or []:
            if child.get("kind") != "ParmVarDecl":
                visit(child)

    for declaration in tree.get("inner", []):
        if declaration.get("kind") == "FunctionDecl" and declaration.get("inner"):
            visit_function(declaration)


def analyze(unit: Tuple[str, List[str]]) -> Tuple[str, dict]:
    source, flags = unit
    key = hashlib.sha1((" ".join(flags) + source).encode() + (ROOT / source).read_bytes()).hexdigest()
    cached = CACHE / (source.replace("/", "_") + ".pickle")
    if cached.is_file():
        data = pickle.loads(cached.read_bytes())
        if data.get("key") == key:
            return source, data
    clang = flags[0]
    rest = flags[1:]
    layouts_text = subprocess.run([clang, *rest, "-fsyntax-only", "-Xclang", "-fdump-record-layouts-complete",
                                   source], capture_output=True, text=True, cwd=ROOT).stdout
    ast_text = subprocess.run([clang, *rest, "-fsyntax-only", "-Xclang", "-ast-dump=json", source],
                              capture_output=True, text=True, cwd=ROOT).stdout
    records = parse_layouts(layouts_text)
    elements: Dict[tuple, Set[str]] = {}
    roots: Dict[int, Set[str]] = {}
    calls: Dict[tuple, Set[tuple]] = {}
    try:
        tree = json.loads(ast_text)
        ENUM_VALUES.clear()
        collect_enum_values(tree)
        collect_ast(tree, elements, roots, calls)
    except json.JSONDecodeError:
        pass
    data = {"key": key, "records": records, "elements": elements, "roots": roots, "calls": calls}
    CACHE.mkdir(parents=True, exist_ok=True)
    cached.write_bytes(pickle.dumps(data))
    return source, data


# ---------- layouts to swap programs

class Layouts:
    def __init__(self, records: Dict[str, Tuple[int, Node]], elements: Dict[Tuple[str, str], Set[str]]):
        self.records = records
        self.elements = elements
        self.memo: Dict[str, Tuple[int, Dict[int, int], Dict[int, Tuple[str, str]]]] = {}
        self.unknown_types: Set[str] = set()
        self.bitfields: Set[str] = set()

    def size_of(self, type_: str) -> Optional[int]:
        type_ = type_.strip()
        array = re.match(r"^(.*)\[(\d+)\]$", type_)
        if array:
            size = self.size_of(array.group(1))
            return None if size is None else size * int(array.group(2))
        if type_ in SCALARS:
            return SCALARS[type_]
        if type_.endswith("*") or type_.startswith("enum "):
            return 4
        if type_ in self.records:
            return self.records[type_][0]
        return None

    def layout(self, type_: str):
        """(size, {offset: scalar size}, {offset: (owner, field)}) of a record"""
        if type_ in self.memo:
            return self.memo[type_]
        if type_ in SCALARS:
            width = SCALARS[type_]
            self.memo[type_] = (width, {0: width} if width > 1 else {}, {})
            return self.memo[type_]
        size, tree = self.records[type_]
        scalars: Dict[int, int] = {}
        blocks: Dict[int, Tuple[str, str]] = {}
        self.memo[type_] = (size, scalars, blocks)
        self._walk(tree, type_, 0, scalars, blocks, top=True)
        return self.memo[type_]

    def _add_type(self, type_: str, offset: int, owner: str, field: str, scalars, blocks) -> None:
        type_ = type_.strip()
        array = re.match(r"^(.*)\[(\d+)\]$", type_)
        if array:
            element = array.group(1).strip()
            element_size = self.size_of(element)
            if element_size is None:
                self.unknown_types.add(element)
                return
            for index in range(int(array.group(2))):
                self._add_type(element, offset + index * element_size, owner, field, scalars, blocks)
            return
        if type_ == "struct tag_block":
            blocks[offset] = (owner, field)
            for word in range(3):
                scalars[offset + word * 4] = 4
            return
        if type_ in SCALARS:
            if SCALARS[type_] > 1:
                scalars.setdefault(offset, SCALARS[type_])
            return
        if type_.endswith("*") or type_.startswith("enum "):
            scalars.setdefault(offset, 4)
            return
        if type_ in self.records:
            _, element_scalars, element_blocks = self.layout(type_)
            for at, size in element_scalars.items():
                scalars.setdefault(offset + at, size)
            for at, key in element_blocks.items():
                blocks.setdefault(offset + at, key)
            return
        self.unknown_types.add(type_)

    def _walk(self, node: Node, owner: str, base: int, scalars, blocks, top: bool = False) -> None:
        for child in node.children:
            if child.bits is not None:
                self.bitfields.add(f"{owner}.{child.name}")
                continue
            if child.type == "struct tag_block":
                self._add_type(child.type, child.offset, owner, child.name, scalars, blocks)
            elif child.children and not re.search(r"\[\d+\]$", child.type):
                self._walk(child, child.type if not child.type.startswith(("struct (", "union (")) else owner,
                           0, scalars, blocks)
            else:
                self._add_type(child.type, child.offset, owner, child.name, scalars, blocks)

    def element_of(self, key: Tuple[str, str]) -> Optional[str]:
        candidates = sorted(t for t in self.elements.get(key, ()) if t in self.records or t in SCALARS)
        if not candidates:
            return None
        # the largest view: the most fields read
        return max(candidates, key=lambda t: (self.layout(t)[0], len(self.layout(t)[1])))


# ---------- the maps: group hierarchy and a check

def map_tags(path: Path):
    """(group tag, parents, tag data offset into the tag data, address of the
    tag data's start) of each tag of an Xbox map, and the tag data"""
    raw = path.read_bytes()
    header = raw[:0x800]
    version, length = struct.unpack_from("<ii", header, 4)
    tag_data_offset, tag_data_size = struct.unpack_from("<ii", header, 0x10)
    body = raw[0x800:]
    try:
        body = zlib.decompress(body)
    except zlib.error:
        pass
    data = (header + body)[tag_data_offset:tag_data_offset + tag_data_size]
    return data


TAG_CACHE_ADDRESS = 0x803A6000


def map_tag_instances(data: bytes):
    instances_address, scenario, checksum, count = struct.unpack_from("<IiIi", data, 0)
    base = TAG_CACHE_ADDRESS
    tags = []
    for index in range(count):
        offset = instances_address - base + index * 32
        group, parent1, parent2, tag_index, name, address = struct.unpack_from("<IIIiII", data, offset)
        tags.append((group, parent1, parent2, address))
    return tags


def fourcc(value: int) -> str:
    return struct.pack(">I", value).decode("latin-1")


# ---------- output

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--maps", type=Path, help="a folder of the Xbox's maps, to check the tables against")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()

    units = game_units()
    if not units:
        sys.exit("no game units: configure the Xbox 360 build first (python3 configure.py --xdk360 ...)")
    records: Dict[str, Tuple[int, Node]] = {}
    found: Dict[tuple, Set[str]] = {}
    roots: Dict[int, Set[str]] = {}
    calls: Dict[tuple, Set[tuple]] = {}
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for source, data in pool.map(analyze, units):
            for name, value in data["records"].items():
                if name not in records or records[name][0] < value[0] or (
                        records[name][0] == value[0] and count_nodes(value[1]) > count_nodes(records[name][1])):
                    records[name] = value
            for key, value in data["elements"].items():
                found.setdefault(key, set()).update(value)
            for key, value in data["roots"].items():
                roots.setdefault(key, set()).update(value)
            for key, value in data["calls"].items():
                calls.setdefault(key, set()).update(value)
    elements = resolve_elements(found, calls)
    # (a comment is the last word: only where the code says nothing)
    for key, value in commented_elements().items():
        if not any(t in records or t in SCALARS for t in elements.get(key, ())):
            elements[key] = value
    print(f"{len(units)} units, {len(records)} records, {len(elements)} blocks with element types, "
          f"{len(roots)} groups with root records", file=sys.stderr)

    layouts = Layouts(records, elements)
    hierarchy: Dict[int, Tuple[int, int]] = {}
    if args.maps:
        for map_path in sorted(args.maps.glob("*.map")):
            try:
                data = map_tags(map_path)
                for group, parent1, parent2, _ in map_tag_instances(data):
                    hierarchy[group] = (parent1, parent2)
            except (struct.error, ValueError):
                continue
    write_tables(args.output, layouts, roots, hierarchy)
    if args.maps:
        check(args.maps, layouts, roots, hierarchy)
    for name in sorted(layouts.bitfields):
        print("note: bitfield (its bits are not the Xbox's on a big-endian target):", name, file=sys.stderr)
    return 0


COMMENTED_BLOCK = re.compile(r"^\s*struct tag_block\s+(\w+)\s*;\s*//\s*(\w+)\s*$")
RECORD_START = re.compile(r"^(struct|union)\s+(\w+)\s*$")


def commented_elements() -> Dict[Tuple[str, str], Set[str]]:
    """blocks whose declaration names its element in a comment
    (`struct tag_block seats; // unit_seat`), in the record that declares it"""
    hints: Dict[Tuple[str, str], Set[str]] = {}
    for path in sorted((ROOT / "source").rglob("*")):
        if path.suffix not in (".c", ".h"):
            continue
        record = None
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            start = RECORD_START.match(line)
            if start:
                record = f"{start.group(1)} {start.group(2)}"
                continue
            if line.startswith("};"):
                record = None
                continue
            match = COMMENTED_BLOCK.match(line)
            if match and record:
                element = match.group(2)
                hints.setdefault((record, match.group(1)), set()).update(
                    {f"struct {element}", f"union {element}", element})
    return hints


def resolve_elements(found: Dict[tuple, Set[str]], calls: Dict[tuple, Set[tuple]]) -> Dict[Tuple[str, str], Set[str]]:
    """each member block's element types: its own, and those of the
    parameters it is given to (through any number of calls)"""
    types: Dict[tuple, Set[str]] = {key: set(value) for key, value in found.items()}
    changed = True
    while changed:
        changed = False
        for (function, position), blocks in calls.items():
            parameter_types = types.get(("param", function, position))
            if not parameter_types:
                continue
            for block in blocks:
                known = types.setdefault(block, set())
                if not parameter_types <= known:
                    known |= parameter_types
                    changed = True
    return {(key[1], key[2]): value for key, value in types.items() if key[0] == "member"}


def count_nodes(node: Node) -> int:
    return 1 + sum(count_nodes(child) for child in node.children)


def group_views(group: int, roots: Dict[int, Set[str]], hierarchy: Dict[int, Tuple[int, int]]) -> List[str]:
    views = set(roots.get(group, ()))
    for parent in hierarchy.get(group, (0xFFFFFFFF, 0xFFFFFFFF)):
        if parent not in (0, 0xFFFFFFFF):
            views |= roots.get(parent, set())
    return sorted(views)


def merged(layouts: Layouts, views: List[str]):
    size = 0
    scalars: Dict[int, int] = {}
    blocks: Dict[int, Tuple[str, str]] = {}
    conflicts = []
    for view in views:
        if view not in layouts.records:
            continue
        view_size, view_scalars, view_blocks = layouts.layout(view)
        size = max(size, view_size)
        for at, scalar in view_scalars.items():
            if scalars.get(at, scalar) != scalar:
                conflicts.append(at)
            scalars.setdefault(at, scalar)
        for at, key in view_blocks.items():
            if at not in blocks or (layouts.element_of(blocks[at]) is None and layouts.element_of(key)):
                blocks[at] = key
    # a scalar overlapping a wider one at a lower offset is the wider one's
    covered: Dict[int, int] = {}
    for at in sorted(scalars):
        if any(start < at < start + width for start, width in covered.items()):
            continue
        covered[at] = scalars[at]
    return size, covered, blocks, conflicts


def program(scalars: Dict[int, int], blocks: Dict[int, int]) -> List[Tuple[int, int, int]]:
    """(offset, op, argument) with runs merged: op 2, 4, 8 swap argument
    scalars of that size from offset; op 0 a block whose element layout is
    argument"""
    ops: List[Tuple[int, int, int]] = []
    for at in sorted(scalars):
        size = scalars[at]
        if ops and ops[-1][1] == size and ops[-1][0] + ops[-1][2] * size == at:
            ops[-1] = (ops[-1][0], size, ops[-1][2] + 1)
        else:
            ops.append((at, size, 1))
    for at in sorted(blocks):
        ops.append((at, 0, blocks[at]))
    return ops


def write_tables(output: Path, layouts: Layouts, roots: Dict[int, Set[str]],
                 hierarchy: Dict[int, Tuple[int, int]]) -> None:
    layout_index: Dict[str, int] = {}
    entries: List[Tuple[str, int, List[Tuple[int, int, int]]]] = []
    missing_elements: Set[Tuple[str, str]] = set()

    def add(name: str, size: int, scalars: Dict[int, int], blocks: Dict[int, Tuple[str, str]]) -> int:
        index = len(entries)
        layout_index[name] = index
        entries.append((name, size, []))
        resolved: Dict[int, int] = {}
        for at, key in blocks.items():
            element = layouts.element_of(key)
            if element is None:
                missing_elements.add(key)
                continue
            if element not in layout_index:
                element_size, element_scalars, element_blocks = layouts.layout(element)
                add(element, element_size, element_scalars, element_blocks)
            resolved[at] = layout_index[element]
        entries[index] = (name, size, program(scalars, resolved))
        return index

    groups: List[Tuple[int, int]] = []
    all_groups = sorted(set(roots) | set(hierarchy))
    for group in all_groups:
        views = group_views(group, roots, hierarchy)
        if not views:
            continue
        size, scalars, blocks, _ = merged(layouts, views)
        groups.append((group, add(f"group {fourcc(group)}: " + ", ".join(views), size, scalars, blocks)))

    lines = [
        "/*",
        "TAG_BYTE_SWAP_TABLES.C",
        "",
        "Generated by tools/tag_swap_generate.py: do not edit. Each tag group's",
        "layout, and each tag block element's, as the swaps a big-endian console",
        "makes to read a map's tags (port/console/game/tag_byte_swap.c).",
        "*/",
        "",
        '#include "tag_byte_swap.h"',
        "",
    ]
    ops_lines = ["const struct tag_swap_op tag_swap_ops[] =", "{"]
    layout_lines = ["const struct tag_swap_layout tag_swap_layouts[] =", "{"]
    first = 0
    for name, size, ops in entries:
        layout_lines.append(f"\t{{ {size}, {first}, {len(ops)} }}, /* {name} */")
        for offset, op, argument in ops:
            ops_lines.append(f"\t{{ {offset}, {op}, {argument} }},")
        first += len(ops)
    if first == 0:
        ops_lines.append("\t{ 0, 0, 0 },")
    ops_lines.append("};")
    layout_lines.append("};")
    group_lines = ["const struct tag_swap_group tag_swap_groups[] =", "{"]
    for group, index in groups:
        group_lines.append(f"\t{{ 0x{group:08X}, {index} }}, /* '{fourcc(group)}' */")
    group_lines.append("};")
    lines += ops_lines + [""] + layout_lines + [""] + group_lines + [
        "",
        f"const int tag_swap_group_count = {len(groups)};",
        f"const int tag_swap_layout_count = {len(entries)};",
        "",
    ]
    output.write_text("\n".join(lines), encoding="utf-8")
    print(f"{output}: {len(groups)} groups, {len(entries)} layouts, {first} operations; "
          f"{len(missing_elements)} blocks of unknown elements", file=sys.stderr)


def check(maps: Path, layouts: Layouts, roots: Dict[int, Set[str]], hierarchy: Dict[int, Tuple[int, int]]) -> None:
    """which blocks in the maps hold elements the tables do not describe,
    or have counts and addresses that are not a block's (a layout that is
    wrong)"""
    unknown: Dict[Tuple[str, str], int] = {}
    bad: Dict[str, int] = {}
    no_views: Dict[int, int] = {}

    for map_path in sorted(maps.glob("*.map")):
        try:
            data = map_tags(map_path)
            tags = map_tag_instances(data)
        except (struct.error, ValueError):
            continue

        def walk(name: str, scalars, blocks, address: int, depth: int = 0) -> None:
            offset = address - TAG_CACHE_ADDRESS
            for at, key in blocks.items():
                if offset + at + 8 > len(data) or offset < 0:
                    bad[name] = bad.get(name, 0) + 1
                    return
                count, block_address = struct.unpack_from("<iI", data, offset + at)
                if count == 0:
                    continue
                if count < 0 or count > 0x100000 or not (TAG_CACHE_ADDRESS <= block_address < TAG_CACHE_ADDRESS
                                                         + len(data)):
                    bad[f"{key[0]}.{key[1]}"] = bad.get(f"{key[0]}.{key[1]}", 0) + 1
                    continue
                element = layouts.element_of(key)
                if element is None:
                    unknown[key] = unknown.get(key, 0) + count
                    continue
                size, element_scalars, element_blocks = layouts.layout(element)
                if depth < 12:
                    for index in range(count):
                        walk(element, element_scalars, element_blocks, block_address + index * size, depth + 1)

        for group, parent1, parent2, address in tags:
            if address < TAG_CACHE_ADDRESS or address >= TAG_CACHE_ADDRESS + len(data):
                continue
            views = group_views(group, roots, hierarchy)
            if not views:
                no_views[group] = no_views.get(group, 0) + 1
                continue
            _, scalars, blocks, _ = merged(layouts, views)
            walk(fourcc(group), scalars, blocks, address)

    for group, count in sorted(no_views.items(), key=lambda item: -item[1]):
        print(f"check: no layout for group '{fourcc(group)}' ({count} tags)", file=sys.stderr)
    for key, count in sorted(unknown.items(), key=lambda item: -item[1]):
        print(f"check: unknown elements: {key[0]}.{key[1]} ({count} elements)", file=sys.stderr)
    for name, count in sorted(bad.items(), key=lambda item: -item[1]):
        print(f"check: not a block: {name} ({count})", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
