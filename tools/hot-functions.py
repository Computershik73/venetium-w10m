#!/usr/bin/env python3
# Where the renderer thread's time goes, by function and by part of the
# engine, from what the phone's sampler (--vw-sample, vw_hang_watch.cc) wrote:
#
#   python tools/hot-functions.py work/phone-650/renderer-profile.txt
#       the whole count since the start: every 16-byte piece of the engine
#       named, the pieces of each function added up, the functions grouped
#       into parts (V8's parser, its GC, Blink's style, layout, paint...)
#   python tools/hot-functions.py work/phone-650/venetium.log
#       only the last "hottest engine code" line of a log
#
# Names come from llvm-symbolizer against the engine the phone ran
# (out/w10m/venetium_engine.dll and its .pdb): run this before the next build.
# Identical code folding gives some small functions another function's name.
import collections
import os
import re
import subprocess
import sys

SRC = os.environ.get("VW_SRC", r"C:\vw\src")
OUT = os.environ.get("VW_OUT", r"out\w10m")
SYMBOLIZER = os.path.join(SRC, r"third_party\llvm-build\Release+Asserts\bin\llvm-symbolizer.exe")
ENGINE = os.path.join(SRC, OUT, "venetium_engine.dll")

# Parts of the engine, by the function's qualified name (first match wins).
PARTS = [
    ("memory copy and fill", r"^(__)?(mem(cpy|set|move|cmp|chr)|__aeabi_mem)|^__mem(cpy|set)|^str(len|cmp|chr)"),
    ("V8 interpreter (bytecode handlers)", r"^Builtins_(.*Handler(_Wide|_ExtraWide)?|InterpreterEntryTrampoline.*|InterpreterEnterAtBytecode.*)$"),
    ("V8 inline caches", r"^Builtins_.*(IC|ICTrampoline|IC_.*)$|^Builtins_.*IC"),
    ("V8 other builtins", r"^Builtins_|^v8_Default_embedded_blob"),
    ("V8 parser", r"v8::internal::(Parser|PreParser|ParserBase|Scanner|.*CharacterStream|AstValueFactory|AstRawString|AstNodeFactory|Scope|DeclarationScope|ClassScope|FunctionLiteral|ParseInfo|.*Parse)"),
    ("V8 bytecode compiler", r"v8::internal::interpreter::|v8::internal::(Compiler|BackgroundCompileTask|.*UnoptimizedCompil|CodeGenerator)"),
    ("V8 optimizing compilers", r"v8::internal::(compiler|maglev|turboshaft|baseline|wasm)::"),
    ("V8 garbage collection", r"cppgc::|v8::internal::(Heap|Scavenger|Scavenge|MarkCompact|MinorMarkSweep|Mark|Sweeper|Concurrent|Evacuat|Remembered|Slot|MutablePage|PageMetadata|MemoryChunk|LocalHeap|.*Visitor|.*GC|Factory|.*Allocator|ArrayBufferSweeper|EphemeronRemembered|Weak)"),
    ("V8 runtime (objects, ICs in C++, strings...)", r"v8::internal::|^v8::"),
    ("Blink bindings (JS <-> DOM)", r"blink::(bindings|V8|ScriptWrappable|.*Bindings|ToV8|NativeValueTraits|DOMWrapperWorld|DOMDataStore|ScriptState|ScriptValue|.*ToV8)|blink::.*v8_"),
    ("Blink style (CSS)", r"blink::(CSS|Style|ComputedStyle|SelectorChecker|SelectorFilter|RuleSet|ElementRuleCollector|MatchResult|MatchedProperties|Selector|StyleResolver|StyleEngine|StyleSheet|MediaQuery|StyleCascade|.*Cascade|CSSParser|.*CSSValue|.*StyleBuilder|StyleRecalc|Invalidation|.*Invalidat)"),
    ("Blink layout and text", r"blink::(Layout|NG|Inline|BlockNode|BlockLayout|FlexLayout|GridLayout|.*Fragment|ShapeResult|HarfBuzz|Font|SimpleFont|TextAutosizer|.*LineBreak|ConstraintSpace|LayoutUnit|PhysicalBox|.*Box)|^hb_|^FT_|harfbuzz"),
    ("Blink paint, prepaint and layerizing", r"blink::(Paint|.*PaintPropertyNode|GeometryMapper|DisplayItem|PaintArtifact|PaintChunk|PaintController|.*Painter|FloatClipRect|PropertyTreeState|CullRect|PrePaint|.*PrePaint|.*Compositor|PendingLayer|.*Layerizer|ObjectPaint|Raster|.*Paint)"),
    ("Blink HTML parser", r"blink::(HTML.*Parser|HTMLTokenizer|HTMLTreeBuilder|HTMLConstructionSite|HTMLToken|.*Tokenizer|HTMLEntity)"),
    ("Blink DOM and events", r"blink::(Node|Element|ContainerNode|Document|Event|.*Event|Attr|.*Collection|TreeScope|ShadowRoot|Text|CharacterData|.*Mutation|EventTarget|.*Listener|.*Observer|NodeTraversal|FlatTreeTraversal|HTML.*Element|SVG)"),
    ("Blink other", r"blink::|WTF::"),
    ("cc and viz (compositor)", r"^cc::|^viz::"),
    ("gfx geometry", r"^gfx::"),
    ("Skia", r"^Sk|^Gr|^sk[a-z_]*::|skia|^SkSL"),
    ("PartitionAlloc and malloc", r"partition_alloc::|allocator_shim|^(malloc|free|calloc|realloc)$|^operator (new|delete)"),
    ("base, mojo, IPC, tracing", r"^base::|^mojo::|^IPC::|^perfetto::|^absl::"),
    ("ICU, zlib, image and media codecs", r"^icu_|^u_|^ucase|^ubrk|zlib|inflate|deflate|^Cr_z|jpeg|^png_|WebP|^ff_|^av|^dav1d|libyuv"),
    ("not named", r"^\?\?"),
]
PART_RES = [(name, re.compile(pattern)) for name, pattern in PARTS]


def part_of(function):
    head = function.split("(", 1)[0]
    for name, regex in PART_RES:
        if regex.search(head):
            return name
    return "other"


def symbolize(offsets):
    query = "".join("0x%x\n" % offset for offset in offsets)
    result = subprocess.run(
        [SYMBOLIZER, "--obj=" + ENGINE, "--relative-address", "--functions=linkage",
         "--demangle", "--no-inlines"],
        input=query, capture_output=True, text=True, check=True)
    blocks = [b for b in result.stdout.split("\n\n") if b.strip()]
    names = []
    for offset, block in zip(offsets, blocks):
        name = block.strip().splitlines()[0]
        names.append(name if name != "??" else "?? (engine+0x%x)" % offset)
    return names


def from_profile(path):
    totals = {}
    modules = {}
    pieces = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            words = line.split()
            if not words or words[0].startswith("#"):
                continue
            if words[0] == "piece":
                pieces.append((int(words[1], 16), int(words[2])))
            elif words[0] == "module":
                modules[" ".join(words[1:-1])] = int(words[-1])
            else:
                totals[words[0]] = int(words[1])
    samples = totals.get("samples", 0)
    if not samples or not pieces:
        sys.exit("an empty profile")
    pct = lambda n: 100.0 * n / samples
    print("%d samples of the renderer thread in tasks" % samples)
    print("  code V8 compiled %.1f%%, the engine (with V8's builtins) %.1f%%, other %.1f%%"
          % (pct(totals.get("jit", 0)), pct(totals.get("engine", 0) + totals.get("builtins", 0)),
             pct(totals.get("other", 0))))
    for name, n in sorted(modules.items(), key=lambda kv: -kv[1]):
        if pct(n) >= 0.5:
            print("  %s %.1f%%" % (name, pct(n)))
    # The middle of each 16-byte piece, its Thumb bit cleared.
    names = symbolize([(offset + 8) & ~1 for offset, _ in pieces])
    by_function = collections.Counter()
    for (_, n), name in zip(pieces, names):
        by_function[name] += n
    by_part = collections.Counter()
    for name, n in by_function.items():
        by_part[part_of(name)] += n
    by_part["code V8 compiled (JIT)"] += totals.get("jit", 0)
    for name, n in modules.items():
        by_part["system DLLs"] += n
    print("\nBy part (of all the thread's samples in tasks):")
    for name, n in by_part.most_common():
        print("  %5.1f%%  %s" % (pct(n), name))
    print("\nBy function:")
    for name, n in by_function.most_common(60):
        print("  %5.1f%%  %-40s %s" % (pct(n), part_of(name)[:40], name[:140]))


def from_log(paths):
    line = None
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            for text in f:
                if "hottest engine code" in text:
                    line = text
    if not line:
        sys.exit("no 'hottest engine code' line")
    print(line.strip()[:160] + " ...")
    pieces = re.findall(r"engine\+0x([0-9a-f]+) ([0-9.]+)%", line)
    if not pieces:
        sys.exit("no addresses")
    names = symbolize([(int(offset, 16) + 8) & ~1 for offset, _ in pieces])
    by_function = collections.OrderedDict()
    for (_, percent), name in zip(pieces, names):
        by_function[name] = by_function.get(name, 0.0) + float(percent)
    for name, percent in sorted(by_function.items(), key=lambda kv: -kv[1]):
        print("%5.1f%%  %s" % (percent, name[:150]))


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: hot-functions.py <renderer-profile.txt | venetium.log>...")
    if sys.argv[1].endswith(".txt"):
        from_profile(sys.argv[1])
    else:
        from_log(sys.argv[1:])


if __name__ == "__main__":
    main()
