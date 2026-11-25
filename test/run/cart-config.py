# SPDX-License-Identifier: MIT
"""Fingerprint the resolved cart build independently of the source build id."""
import argparse
import hashlib
import re
from pathlib import Path


def tool_inputs(args, variable, registered):
    """Find file inputs in the effective argv from the SDK's n64tool recipe."""
    scalar = {"-a", "--align", "-s", "--offset", "-t", "--title", "-l", "--size", "-P", "--padding",
              "-C", "--category", "-R", "--region", "-o", "--output"}
    header = {"-h", "--header"}
    argv = iter(args)
    def operand(arg):
        value = next(argv, None)
        if value is None:
            raise ValueError(f"{variable}: missing value for {arg}")
        return value

    for arg in argv:
        if arg in {"-T", "--toc"}:
            continue
        if arg in scalar:
            operand(arg)
        elif arg in header:
            # Header options read a file, unlike the packer's scalar settings.
            yield declared_file(variable, arg, operand(arg), registered)
        elif arg.startswith(("-", "@")):
            raise ValueError(f"{variable}: {arg} is not permitted in packer flags")
        elif variable == "N64_TOOLFILES":
            path = Path(arg)
            if not path.is_file():
                raise ValueError(f"{variable}: packer member {arg!r} is not a file")
            yield path  # positional members are themselves declared inputs
        else:
            yield declared_file(variable, arg, arg, registered)


DIR_FLAGS = ("-idirafter", "-isystem", "-iquote", "-I", "-L")
FILE_FLAGS = ("--section-ordering-file", "--just-symbols", "--script", "-imacros", "-include", "-T", "-R")
SAFE_FLAGS = {
    "-g", "-Wall", "-Wextra", "-Werror", "--fatal-warnings", "--gc-sections", "--emit-relocs",
    "-ffast-math", "-fno-fast-math", "-ftrapping-math", "-fno-associative-math",
    "-ffunction-sections", "-fdata-sections", "-MMD", "-MP",
}
SAFE_PARAMS = {
    "-march", "-mtune", "-mabi", "-std", "-ffp-contract", "-falign-functions",
    "-ffile-prefix-map", "-fmacro-prefix-map", "-fdebug-prefix-map",
    "-fdiagnostics-color", "-ftrivial-auto-var-init", "-Werror", "-Wno-error",
}

# Refuse expansion even inside quotes. Allow only the SDK's literal prefix
# map quoting and the packer's whole-argument quoting (title/member paths).
SHELL_META = frozenset("~$`;|&<>*?[]{}\\!#()")
SHELL_WORD = re.compile(r'''[^\s'"]+|"[^"\r\n]*"|'[^'\r\n]*' ''', re.X)


def literal_words(variable, value, *, root=None, quoted_operands=False):
    metacharacter = next((c for c in value if c in SHELL_META or (c.isspace() and c not in " \t")), None)
    if metacharacter is not None:
        raise ValueError(f"{variable}: shell metacharacter {metacharacter!r} is not permitted")
    if root is not None:
        value = re.sub(rf'(?<!\S)-ffile-prefix-map="{re.escape(str(root))}"=(?=\s|$)',
                       f"-ffile-prefix-map={root}=", value)
    words = []
    position = 0
    for match in SHELL_WORD.finditer(value):
        token = match[0]
        if match.start() != position and not value[position:match.start()].isspace():
            break
        if match.start() and not value[match.start() - 1].isspace():
            break
        if token[0] in "\"'":
            if not quoted_operands or token[1:].startswith(("-", "@")):
                raise ValueError(f"{variable}: quote may not hide an option or split a word: {token!r}")
            token = token[1:-1]
        words.append(token)
        position = match.end()
    if value[position:].strip():
        raise ValueError(f"{variable}: unsupported quoting near {value[position:]!r}")
    return words


def declared_file(variable, option, value, registered, search=()):
    path = Path(value)
    for directory in (Path.cwd(), *search) if not path.is_absolute() else (Path("/"),):
        candidate = (directory / path).resolve()
        if candidate.exists():
            if candidate in registered and candidate.is_file():
                return candidate
            break  # an earlier unregistered file shadows a registered one
    raise ValueError(f"{variable}: {option} names undeclared side input {value!r}; register it in TR64_CART_INPUTS")


def check_flags(variable, flags, registered, roots, sdk_roots):
    """Accept the cart's known flag grammar, not opaque compiler/linker extensions."""
    words = literal_words(variable, flags, root=roots[0])
    args = []
    for word in words:
        if word.startswith(("-Wl,", "-Wa,", "-Wp,")):
            prefix, *parts = word.split(",")
            args.extend((f"{prefix},{part}", part) for part in parts)
        else:
            args.append((word, word))
    # Drivers expand response files before consuming option operands.
    for origin, arg in args:
        if arg.startswith("@") or arg.partition("=")[0] in ("-specs", "--specs"):
            raise ValueError(f"{variable}: {origin} is not permitted in cart flags (opaque side input)")
    argv = iter(args)
    dirs = {option: [] for option in DIR_FLAGS}
    files, libs = [], []
    ed64 = variable == "N64_ED64ROMCONFIGFLAGS"
    for origin, arg in argv:
        def operand(value=None):
            if value is not None:
                return value
            item = next(argv, None)
            if item is None:
                raise ValueError(f"{variable}: missing value for {origin}")
            return item[1]

        if ed64:
            if arg in ("--rtc", "--regionfree"):
                continue
            if arg in ("--savetype", "--expansionpak", "--controller1", "--controller2", "--controller3", "--controller4"):
                operand()
                continue
        elif option := next((opt for opt in DIR_FLAGS + FILE_FLAGS if arg == opt or
                            (arg.startswith(opt) if not opt.startswith("--") else arg.startswith(opt + "="))), None):
            joined = arg[len(option):]
            if option.startswith("--"):
                joined = joined.removeprefix("=")
            value = operand(joined or None)
            if value.startswith("="):
                raise ValueError(f"{variable}: {origin} uses an unsupported sysroot-relative path: {value!r}")
            if option in dirs:
                path = Path(value).resolve()
                if not any(path.is_relative_to(root) for root in roots):
                    raise ValueError(f"{variable}: {origin} searches outside registered roots: {value!r}")
                dirs[option].append(path)
            else:
                files.append((option, origin, value))
            continue
        elif arg in SAFE_FLAGS or re.fullmatch(r"-(?:O[0-3sgz]?|g[0-3]|Wno-[\w-]+|[DU][A-Za-z_]\w*(?:=.*)?)", arg):
            continue
        elif arg.partition("=")[0] in SAFE_PARAMS and "=" in arg:
            continue
        elif arg in ("--wrap", "-D", "-U"):
            operand()
            continue
        elif re.fullmatch(r"-l[\w+-]+", arg):
            libs.append((origin, arg[2:]))
            continue
        elif not arg.startswith(("-", "@")):
            files.append(("", origin, arg))
            continue
        raise ValueError(f"{variable}: {origin} is not permitted in cart flags (undeclared or opaque side inputs)")

    includes = dirs["-iquote"] + dirs["-I"] + dirs["-isystem"] + dirs["-idirafter"]
    for option, origin, value in files:
        search = dirs["-L"] if option in ("-T", "--script") else includes if option in ("-include", "-imacros") else ()
        declared_file(variable, origin, value, registered, search)
    for origin, name in libs:
        for directory in dirs["-L"]:
            for suffix in (".so", ".a"):
                path = (directory / f"lib{name}{suffix}").resolve()
                if path.is_file() and path not in registered and not any(path.is_relative_to(root) for root in sdk_roots):
                    raise ValueError(f"{variable}: {origin} names undeclared side input {str(path)!r}; register it in TR64_CART_INPUTS")
        if name not in ("dragon", "dragonsys", "m", "c") and not any(
            (directory / f"lib{name}.a").resolve() in registered for directory in dirs["-L"]
        ):
            raise ValueError(f"{variable}: {origin} is not a pinned SDK library or a registered archive")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--vars", type=Path, required=True)
    parser.add_argument("--source-id", type=Path, required=True)
    for name in ("root", "build", "sdk", "gcc"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--makefile", type=Path, action="append", default=[])
    parser.add_argument("--probe", type=Path, action="append", default=[])
    parser.add_argument("--check-flags", action="append", default=[])
    parser.add_argument("inputs", type=Path, nargs="+")
    args = parser.parse_args()
    registered = {p.resolve() for p in args.inputs}
    settings = dict(line.split("=", 1) for line in args.vars.read_text().split("\n") if line)
    trust_roots = tuple(getattr(args, name).resolve() for name in ("root", "build", "sdk", "gcc"))
    try:
        tool_flags = literal_words("N64_TOOLFLAGS", settings.get("N64_TOOLFLAGS", ""), quoted_operands=True)
        tool_members = literal_words("N64_TOOLFILES", settings.get("N64_TOOLFILES", ""), quoted_operands=True)
        for var in ("N64_ROM_ELFCOMPRESS", "TR64_HOT_DATA_PHASE", "TR64_POOL_PHASE"):
            if (value := settings.get(var, "").strip()) and not re.fullmatch(r"[0-9]+", value):
                raise ValueError(f"{var}: expected a decimal number, got {value!r}")
        for var in args.check_flags:
            check_flags(var, settings.get(var, ""), registered, trust_roots, trust_roots[2:])
        tool_files = [*tool_inputs(tool_flags, "N64_TOOLFLAGS", registered),
                      *tool_inputs(tool_members, "N64_TOOLFILES", registered)]
    except ValueError as error:
        parser.error(str(error))
    roots = sorted(((str(getattr(args, name).resolve()), f"<{name}>")
                    for name in ("root", "build", "sdk", "gcc")), key=lambda pair: -len(pair[0]))

    def normalize(value):
        for directory, name in roots:
            value = value.replace(directory, name)
        return value

    digest = hashlib.sha256(b"tr64-cart-config-v1\0")
    # Ignore runner-only environment variables even if they use a TR64_ prefix.
    # Include every resolved variable referenced by the project or SDK recipes.
    used = set()
    for makefile in args.makefile:
        used.update(re.findall(r"\$\(([A-Z][A-Z0-9_]*)\)|\$\{([A-Z][A-Z0-9_]*)\}|"
                               r"^\s*ifn?def\s+([A-Z][A-Z0-9_]*)", makefile.read_text(), re.M))
    names = {name for match in used for name in match if name}
    for var, value in settings.items():
        if var in names or var in args.check_flags:
            digest.update(normalize(f"{var}={value}").encode() + b"\0")
    for arg in [*tool_flags, "--", *tool_members]:
        digest.update(normalize(arg).encode() + b"\0")
    # Probe modules outside the source-id trees also include private source
    # and generated inputs (e.g. save-native.c and gate_place.h). Follow the
    # cart's quoted includes in lookup order, without depending on stale .d files.
    pending = args.probe[:]
    probe_inputs = {}
    while pending:
        path = pending.pop().resolve()
        if path in probe_inputs:
            continue
        probe_inputs[path] = None
        for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', path.read_text(), re.M):
            for directory in (path.parent, args.root, args.root / "include", args.build / "generated"):
                child = directory / name
                if child.is_file() and child.name != args.out.name:
                    pending.append(child)
                    break
    for path in dict.fromkeys([*(p.resolve() for p in args.makefile + args.inputs + tool_files), *probe_inputs]):
        digest.update(normalize(str(path)).encode() + b"\0")
        with path.open("rb") as stream:
            digest.update(hashlib.file_digest(stream, "sha256").digest())
    text = args.source_id.read_text() + f'#define TR64_CONFIG_ID "{digest.hexdigest()}"\n'
    if not args.out.exists() or args.out.read_text() != text:
        args.out.write_text(text)


if __name__ == "__main__":
    main()
