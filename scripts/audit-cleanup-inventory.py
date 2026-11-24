#!/usr/bin/env python3
"""Read-only, exact-head whole-tree duplicate inventory; never auto-delete."""
import ast
from collections import Counter, defaultdict
import hashlib
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
tracked = subprocess.check_output(["git", "-C", ROOT, "ls-files", "-z"]).split(b"\0")
paths = [Path(p.decode()) for p in tracked if p]
counts = Counter()
bytes_read = 0
text_files = 0
exact = defaultdict(list)
funcs = defaultdict(list)
blocks = defaultdict(list)
sources = {".c", ".h", ".S", ".s", ".sh", ".py", ".yml", ".yaml", ".ld"}
N = 12
for path in paths:
    file = ROOT / path
    if file.is_dir() or file.is_symlink():
        counts["submodule_or_symlink"] += 1
        continue
    raw = file.read_bytes()
    counts[path.parts[0]] += 1
    bytes_read += len(raw)
    exact[hashlib.sha256(raw).hexdigest()].append(str(path))
    try:
        content = raw.decode("utf-8")
    except UnicodeDecodeError:
        continue
    if "\0" in content:
        continue
    text_files += 1
    if path.parts[0] == "third_party":
        continue
    if path.suffix == ".py":
        try:
            module = ast.parse(content, filename=str(path))
        except SyntaxError as e:
            print("PYTHON PARSE", path, str(e))
            continue
        for node in module.body:
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.end_lineno-node.lineno >= 3:
                key = ast.dump(node, include_attributes=False)
                funcs[key].append((str(path), node.lineno, node.end_lineno, node.name))
    if path.suffix not in sources and path.name != "Makefile":
        continue
    # Whitespace and milestone-normalized *candidate* blocks, not equivalence.
    lines = [(i, re.sub(r"\b([MmHh])\d{1,3}\b", r"\1#", " ".join(s.split())))
             for i, s in enumerate(content.splitlines(), 1)
             if s.strip() and not s.lstrip().startswith(("# ", "//", "/*", "* "))]
    for j in range(len(lines)-N+1):
        seq = "\n".join(s for _, s in lines[j:j+N])
        k = hashlib.blake2b(seq.encode(), digest_size=16).digest()
        blocks[k].append((str(path), lines[j][0], lines[j+N-1][0], lines[j][1][:110]))

print("WHOLE TREE", "tracked", len(paths), "text", text_files, "bytes", bytes_read, "roots", dict(sorted(counts.items())))
dups = [v for v in exact.values() if len(v)>1]
print("EXACT FILE GROUPS", len(dups))
for group in dups:
    print("EXACT", " | ".join(group))
groups = [v for v in funcs.values() if len({p for p,*_ in v})>1]
groups.sort(key=lambda v:(len(v)-1)*(v[0][2]-v[0][1]+1), reverse=True)
print("PYTHON IDENTICAL AST GROUPS", len(groups))
for group in groups[:45]:
    print("PY", group[0][3], "lines", group[0][2]-group[0][1]+1, "copies", len(group),
          " ".join(f"{p}:{a}-{b}" for p,a,b,_ in group))
# Prevent many sliding windows of one repeated region from obscuring others.
clusters = defaultdict(list)
for v in blocks.values():
    if len({p for p,*_ in v})<3:
        continue
    files = tuple(sorted({p for p,*_ in v}))
    clusters[files].append(v)
rank = sorted(clusters.items(), key=lambda item:(len(item[0])-1)*len(item[1]), reverse=True)
print("SOURCE BLOCK FILE CLUSTERS", len(rank))
for files, windows in rank[:40]:
    ex = windows[0]
    print("BLOCK", "files",len(files),"windows",len(windows),"start",ex[0][3],
          " ".join(f"{p}:{a}" for p,a,_,_ in ex[:30]))

# Identifier inventory includes scripts, assembly, build configuration and
# tests. Single-occurrence static definitions are candidates, not proof:
# macro token pasting, indirect assembly entry and optional configs matter.
identifiers = Counter()
definitions = []
for path in paths:
    file = ROOT / path
    if not file.is_file() or file.is_symlink():
        continue
    raw = file.read_bytes()
    try:
        content = raw.decode("utf-8")
    except UnicodeDecodeError:
        continue
    identifiers.update(re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", content))
    if path.parts[0] != "kernel" or path.suffix not in (".c", ".h"):
        continue
    for match in re.finditer(r"^(?P<storage>static\s+(?:inline\s+)?)?(?:[A-Za-z_][A-Za-z0-9_]*[ \t*]+)+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*?\)\s*\{", content, re.M):
        name = match["name"]
        if name in ("if", "while", "for", "switch"):
            continue
        definitions.append((str(path), content.count("\n", 0, match.start())+1, name, bool(match["storage"])))
for path, line, name, static in definitions:
    n = identifiers[name]
    if (static and n == 1) or (not static and name.startswith("nas_") and n <= 2):
        print("LOWREF", path, line, name, "occurrences", n, "static", static)
print("KERNEL IDENTIFIER INVENTORY", len(definitions), "candidate definitions; includes optional builds and test references")


# Finer first-party C/ASM duplicate report: ignore comments, normalize only
# architecture-local names, preserve literals, operators, and NAS ABI names.
core_windows=defaultdict(list)
for path in paths:
    if path.parts[0] not in ("kernel", "ports", "user") or path.suffix not in (".c",".h",".S"):
        continue
    file=ROOT/path
    content=file.read_text()
    # Keep actual string contents (including asm symbol names).
    content=re.sub(r"/\*[\s\S]*?\*/", lambda m:"\n"*m[0].count("\n"),content)
    lines=[(i,re.sub(r"\b(?:nas_rv_|nas_arm_|nas_x86_|NAS_RV_|NAS_ARM_|NAS_X86_)", "ISA_", re.sub(r"\s+","",s.split("//",1)[0])))
           for i,s in enumerate(content.splitlines(),1) if s.split("//",1)[0].strip()]
    for j in range(len(lines)-6+1):
        seq="\n".join(s for _,s in lines[j:j+6])
        if len(seq)<110:continue
        core_windows[seq].append((str(path),lines[j][0],lines[j+5][0]))
core_clusters=defaultdict(list)
for seq,loc in core_windows.items():
    files=tuple(sorted({p for p,*_ in loc}))
    if len(files)<2:continue
    core_clusters[files].append((seq,loc))
for files,windows in sorted(core_clusters.items(), key=lambda item:(len(item[0])-1)*len(item[1]), reverse=True)[:55]:
    loc=windows[0][1]
    print("CORE DUP",len(windows),"windows"," | ".join(f"{p}:{a}-{b}" for p,a,b in loc[:6]))
# No earlier removed APIs may reappear as code, including host fixtures.
retired=("nas_eventfd_ready","nas_vm_walk_task_pages","nas_x86_h15_ftdi_attached",
         "nas_task_find","nas_task_child_at","nas_task_fd_kind","nas_task_fd_slot")
for path in paths:
    if path.suffix not in (".c",".h",".S",".s"):continue
    file=ROOT/path
    if not file.is_file():continue
    for name in retired:
        if re.search(r"\b"+name+r"\b",file.read_text(errors="replace")):
            raise SystemExit(f"retired interface still referenced: {path}: {name}")
print("[RETIRED KERNEL API PASS]",len(retired))

print("[CLEANUP INVENTORY PASS] read-only candidate scan, not proof of removable code")
