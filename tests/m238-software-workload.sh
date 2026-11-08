#!/bin/sh
# Fifteen actual new original signed Alpine ELFs, one real functional cohort.
# Each case executes the named ELF on both independent native QEMU boots.
set -eu
test "$#" -eq 3 && test "$1" = --exercise || exit 2
case "${NAS_M238_BOOT_PHASE:-}" in first|reopen) ;; *) exit 2 ;; esac
phase=$NAS_M238_BOOT_PHASE case_id=$2 elf=$3
busy=/bin/busybox
state=/root/m238-state
target="$state/$case_id.$phase"
test -x "/$elf"
test "$($busy od -An -tx1 -N4 "/$elf" | $busy tr -d ' \t\r\n')" = 7f454c46 || exit 1
test ! -e "$target" || { echo '[M238 FAIL] preseeded original ELF witness' >&2; exit 1; }
mkdir -p "$target"

reject() {
    if "$@" > "$target/rejected.log" 2>&1; then
        echo "[M238 FAIL] malformed input accepted by /$elf" >&2
        exit 1
    fi
    test -s "$target/rejected.log"
}

wasm_input() {
    test -s "$state/add.wat" && test -s "$state/add.wasm"
    test "$($busy od -An -tx1 -N4 "$state/add.wasm" | $busy tr -d ' \t\r\n')" = 0061736d
}

spirv_input() {
    test -s "$state/compute.spvasm" && test -s "$state/compute.spv"
    test "$($busy od -An -tx1 -N4 "$state/compute.spv" | $busy tr -d ' \t\r\n')" = 03022307
}

case "$case_id:$elf" in
    wasm-assemble:usr/bin/wasm-as)
        if [ "$phase" = first ]; then
            test ! -e "$state/add.wat"
            printf '(module (func (export "add") (param i32 i32) (result i32) (i32.add (local.get 0) (local.get 1))))\n' > "$state/add.wat"
        fi
        test -s "$state/add.wat"
        /usr/bin/wasm-as "$state/add.wat" -o "$target/assembled.wasm" > "$target/stdout.log" 2> "$target/stderr.log"
        test "$($busy od -An -tx1 -N4 "$target/assembled.wasm" | $busy tr -d ' \t\r\n')" = 0061736d
        if [ "$phase" = first ]; then
            $busy cp "$target/assembled.wasm" "$state/add.wasm"
        else
            $busy cmp -s "$target/assembled.wasm" "$state/add.wasm"
        fi
        printf '(module (func (i32.unknown_opcode)))\n' > "$target/bad.wat"
        reject /usr/bin/wasm-as "$target/bad.wat" -o "$target/should-not-exist.wasm"
        test ! -e "$target/should-not-exist.wasm"
        ;;
    wasm-disassemble:usr/bin/wasm-dis)
        wasm_input
        /usr/bin/wasm-dis "$state/add.wasm" -o "$target/roundtrip.wat" > "$target/stdout.log" 2> "$target/stderr.log"
        $busy grep -Fq 'add' "$target/roundtrip.wat" &&
            $busy grep -Fq 'i32.add' "$target/roundtrip.wat"
        printf 'M238notwasm' > "$target/corrupted.wasm"
        reject /usr/bin/wasm-dis "$target/corrupted.wasm" -o "$target/bad.wat"
        ;;
    wasm-optimize:usr/bin/wasm-opt)
        wasm_input
        /usr/bin/wasm-opt "$state/add.wasm" -O2 -o "$target/optimized.wasm" > "$target/stdout.log" 2> "$target/stderr.log"
        /usr/bin/wasm-dis "$target/optimized.wasm" -o "$target/optimized.wat"
        $busy grep -Fq 'add' "$target/optimized.wat" &&
            $busy grep -Fq 'i32.add' "$target/optimized.wat"
        printf 'M238notwasm' > "$target/corrupted.wasm"
        reject /usr/bin/wasm-opt "$target/corrupted.wasm" -O2 -o "$target/bad.wasm"
        ;;
    wasm-merge-modules:usr/bin/wasm-merge)
        wasm_input
        printf '(module (func (export "ten") (result i32) (i32.const 10)))\n' > "$target/ten.wat"
        /usr/bin/wasm-as "$target/ten.wat" -o "$target/ten.wasm"
        /usr/bin/wasm-merge "$state/add.wasm" first "$target/ten.wasm" second -o "$target/merged.wasm" > "$target/stdout.log" 2> "$target/stderr.log"
        /usr/bin/wasm-dis "$target/merged.wasm" -o "$target/merged.wat"
        $busy grep -Fq 'add' "$target/merged.wat" &&
            $busy grep -Fq 'ten' "$target/merged.wat"
        printf 'M238notwasm' > "$target/corrupted.wasm"
        reject /usr/bin/wasm-merge "$state/add.wasm" first "$target/corrupted.wasm" second -o "$target/bad.wasm"
        ;;
    wasm-to-javascript:usr/bin/wasm2js)
        wasm_input
        /usr/bin/wasm2js "$state/add.wasm" -o "$target/translated.js" > "$target/stdout.log" 2> "$target/stderr.log"
        test -s "$target/translated.js"
        $busy grep -Fq 'add' "$target/translated.js" &&
            $busy grep -Eq '(function|=>|export)' "$target/translated.js"
        printf 'M238notwasm' > "$target/corrupted.wasm"
        reject /usr/bin/wasm2js "$target/corrupted.wasm" -o "$target/bad.js"
        ;;
    wasm-spec-assert:usr/bin/wasm-shell)
        printf '(module (func (export "add") (param i32 i32) (result i32) (i32.add (local.get 0) (local.get 1))))\n(assert_return (invoke "add" (i32.const 4) (i32.const 5)) (i32.const 9))\n' > "$target/valid.wast"
        /usr/bin/wasm-shell "$target/valid.wast" > "$target/assertions.log" 2>&1
        $busy grep -Ei '(all checks passed|checks passed)' "$target/assertions.log" >/dev/null
        $busy sed 's/(i32.const 9))/(i32.const 10))/' "$target/valid.wast" > "$target/false.wast"
        $busy cmp -s "$target/false.wast" "$target/valid.wast" && exit 1
        reject /usr/bin/wasm-shell "$target/false.wast"
        ;;
    spirv-assemble:usr/bin/spirv-as)
        if [ "$phase" = first ]; then
            test ! -e "$state/compute.spvasm"
            printf '%s\n' \
                'OpCapability Shader' 'OpMemoryModel Logical GLSL450' \
                'OpEntryPoint GLCompute %main "main"' 'OpExecutionMode %main LocalSize 1 1 1' \
                '%void = OpTypeVoid' '%fn = OpTypeFunction %void' \
                '%main = OpFunction %void None %fn' '%entry = OpLabel' \
                'OpReturn' 'OpFunctionEnd' > "$state/compute.spvasm"
        fi
        test -s "$state/compute.spvasm"
        /usr/bin/spirv-as "$state/compute.spvasm" -o "$target/assembled.spv" > "$target/stdout.log" 2> "$target/stderr.log"
        /usr/bin/spirv-val "$target/assembled.spv" > "$target/valid.log" 2>&1
        test "$($busy od -An -tx1 -N4 "$target/assembled.spv" | $busy tr -d ' \t\r\n')" = 03022307
        if [ "$phase" = first ]; then
            $busy cp "$target/assembled.spv" "$state/compute.spv"
        else
            $busy cmp -s "$target/assembled.spv" "$state/compute.spv"
        fi
        printf 'OpNotARealInstruction\n' > "$target/bad.spvasm"
        reject /usr/bin/spirv-as "$target/bad.spvasm" -o "$target/bad.spv"
        ;;
    spirv-disassemble:usr/bin/spirv-dis)
        spirv_input
        /usr/bin/spirv-dis "$state/compute.spv" -o "$target/disassembled.spvasm" > "$target/stdout.log" 2> "$target/stderr.log"
        $busy grep -Fq 'OpEntryPoint GLCompute' "$target/disassembled.spvasm" &&
            $busy grep -Fq 'OpReturn' "$target/disassembled.spvasm"
        printf 'BADspirv' > "$target/bad.spv"
        reject /usr/bin/spirv-dis "$target/bad.spv" -o "$target/bad.spvasm"
        ;;
    spirv-validate:usr/bin/spirv-val)
        spirv_input
        /usr/bin/spirv-val "$state/compute.spv" > "$target/valid.log" 2>&1
        $busy sed '/^OpMemoryModel /d' "$state/compute.spvasm" > "$target/invalid.spvasm"
        /usr/bin/spirv-as "$target/invalid.spvasm" -o "$target/invalid.spv"
        reject /usr/bin/spirv-val "$target/invalid.spv"
        ;;
    spirv-optimize:usr/bin/spirv-opt)
        spirv_input
        /usr/bin/spirv-opt "$state/compute.spv" -O -o "$target/optimized.spv" > "$target/stdout.log" 2> "$target/stderr.log"
        /usr/bin/spirv-val "$target/optimized.spv"
        /usr/bin/spirv-dis "$target/optimized.spv" -o "$target/optimized.spvasm"
        $busy grep -Fq 'OpEntryPoint GLCompute' "$target/optimized.spvasm"
        printf 'BADspirv' > "$target/bad.spv"
        reject /usr/bin/spirv-opt "$target/bad.spv" -O -o "$target/bad-output.spv"
        ;;
    spirv-control-flow:usr/bin/spirv-cfg)
        spirv_input
        /usr/bin/spirv-cfg "$state/compute.spv" -o "$target/control-flow.dot" > "$target/stdout.log" 2> "$target/stderr.log"
        # The SPIRV-Tools DOT legend alone contains digraph/label; it is NOT
        # proof of a parsed function CFG. Require an actual function-entry box.
        $busy grep -Fq 'Fn ' "$target/control-flow.dot" &&
            $busy grep -Fq ' entry' "$target/control-flow.dot" &&
            $busy grep -Fq 'shape=box' "$target/control-flow.dot"
        printf 'BADspirv' > "$target/bad.spv"
        reject /usr/bin/spirv-cfg "$target/bad.spv" -o "$target/bad.dot"
        ;;
    capstone-decode:usr/bin/cstool)
        /usr/bin/cstool x64 90 > "$target/decoded.txt" 2> "$target/decode.err"
        $busy grep -Eiq '(^|[^[:alpha:]])nop([^[:alpha:]]|$)' "$target/decoded.txt"
        reject /usr/bin/cstool m238-unrecognized-architecture 90
        ;;
    cppcheck-errors:usr/bin/cppcheck)
        printf 'int broken(void) { int a[2]={0,1}; return a[3]; }\n' > "$target/unsafe.c"
        printf 'int safe(void) { int a[2]={0,1}; return a[1]; }\n' > "$target/safe.c"
        reject /usr/bin/cppcheck --enable=all --error-exitcode=1 "$target/unsafe.c"
        $busy grep -Eiq '(arrayIndexOutOfBounds|out of bounds)' "$target/rejected.log"
        /usr/bin/cppcheck --error-exitcode=1 "$target/safe.c" > "$target/safe.log" 2>&1
        ;;
    git-repo-size:usr/bin/git-sizer)
        if [ "$phase" = first ]; then
            test ! -e "$state/sample.git"
            /usr/bin/git init -q "$state/sample.git"
            printf 'NaSeed M238 real persisted git blob alpha\n' > "$state/sample.git/alpha.txt"
            printf 'NaSeed M238 real persisted git blob beta\n' > "$state/sample.git/beta.txt"
            /usr/bin/git -C "$state/sample.git" add alpha.txt beta.txt
            /usr/bin/git -C "$state/sample.git" -c user.name=NaSeed -c user.email=naseed@example.org commit -qm m238-real-commit
        fi
        test -d "$state/sample.git/.git"
        test "$(/usr/bin/git -C "$state/sample.git" rev-list --count HEAD)" = 1
        (cd "$state/sample.git" && /usr/bin/git-sizer --json --json-version=1) > "$target/sizer.json" 2> "$target/sizer.err"
        $busy grep -Eq '"unique_commit_count"[[:space:]]*:[[:space:]]*1([,}]|$)' "$target/sizer.json" &&
            $busy grep -Eq '"unique_blob_count"[[:space:]]*:[[:space:]]*2([,}]|$)' "$target/sizer.json"
        ;;
    actions-yaml-lint:usr/bin/actionlint)
        printf 'name: M238\non: push\njobs:\n  verify:\n    runs-on: ubuntu-latest\n    steps:\n      - run: echo M238\n' > "$target/valid.yml"
        /usr/bin/actionlint "$target/valid.yml" > "$target/valid.log" 2>&1
        printf 'name: M238\non: push\njobs:\n  verify:\n    runs-on: [foo\n    steps:\n      - run: echo M238\n' > "$target/invalid.yml"
        reject /usr/bin/actionlint "$target/invalid.yml"
        ;;
    *) echo "[M238 FAIL] unknown original native ELF case=$case_id elf=$elf" >&2; exit 2 ;;
esac
echo "M238_REAL_FUNCTION_OK case=$case_id phase=$phase original=/$elf"
