#!/usr/bin/env python3
"""Local, bounded Ghidra control for Ocarina Couch Edition investigations.

The tool never copies or edits its input binary. Ghidra projects, logs, and
metadata exports default to the user's private .codex directory, outside Git.
Run this with the Python environment that has the matching PyGhidra wheel.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Optional


DEFAULT_TOOL_ROOT = Path.home() / ".codex" / "tools" / "ocz-ghidra-12.1.4"
DEFAULT_GHIDRA_HOME = DEFAULT_TOOL_ROOT / "ghidra_12.1.4_PUBLIC"
DEFAULT_JAVA_HOME = DEFAULT_TOOL_ROOT / "jdk-21.0.12.1+1"
LOCAL_APP_DATA = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
DEFAULT_DATA_ROOT = LOCAL_APP_DATA / "OCZ-Ghidra" / "ocz-reverse"
REPO_ROOT = Path(__file__).resolve().parents[2]
MAX_HEAP = "1536m"
HEADLESS_MAX_HEAP = "1536M"


class ToolError(RuntimeError):
    """An expected environment, input, or Ghidra operation failure."""


def _path_from_env(name: str, fallback: Path) -> Path:
    value = os.environ.get(name)
    return Path(value).expanduser().resolve() if value else fallback.resolve()


def resolve_installation() -> tuple[Path, Path]:
    ghidra_home = _path_from_env("GHIDRA_INSTALL_DIR", DEFAULT_GHIDRA_HOME)
    java_home = _path_from_env("GHIDRA_JAVA_HOME", DEFAULT_JAVA_HOME)
    if not os.environ.get("GHIDRA_JAVA_HOME") and os.environ.get("JAVA_HOME"):
        java_home = Path(os.environ["JAVA_HOME"]).expanduser().resolve()
    return ghidra_home, java_home


def ghidra_version(ghidra_home: Path) -> str | None:
    properties = ghidra_home / "Ghidra" / "application.properties"
    if not properties.is_file():
        return None
    for line in properties.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"\s*application\.version\s*=\s*(.*?)\s*$", line)
        if match:
            return match.group(1)
    return None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def n64_format(path: Path) -> str | None:
    with path.open("rb") as stream:
        magic = stream.read(4)
    return {
        bytes.fromhex("80371240"): "z64-big-endian",
        bytes.fromhex("37804012"): "v64-byte-swapped",
        bytes.fromhex("40123780"): "n64-little-endian",
    }.get(magic)


def private_path(path: Path) -> bool:
    resolved = path.expanduser().resolve()
    try:
        resolved.relative_to(REPO_ROOT)
        return False
    except ValueError:
        return True


def require_runtime() -> tuple[Path, Path, Path]:
    ghidra_home, java_home = resolve_installation()
    headless = ghidra_home / "support" / "analyzeHeadless.bat"
    java = java_home / "bin" / "java.exe"
    if not headless.is_file():
        raise ToolError(f"Ghidra Headless Analyzer not found: {headless}")
    if not java.is_file():
        raise ToolError(f"Ghidra Java runtime not found: {java}")
    if not (3, 9) <= sys.version_info[:2] <= (3, 14):
        raise ToolError("PyGhidra requires CPython 3.9 through 3.14 for this Ghidra release")
    if importlib.util.find_spec("pyghidra") is None:
        raise ToolError("PyGhidra is not installed in this Python. Run this with the OCZ Ghidra venv.")
    return ghidra_home, java_home, headless


def run_headless(
    headless: Path,
    ghidra_home: Path,
    java_home: Path,
    args: list[str],
    log_path: Path,
    timeout_seconds: int,
) -> tuple[int, str]:
    powershell = shutil.which("powershell.exe") or shutil.which("powershell")
    if not powershell:
        raise ToolError("Windows PowerShell was not found; Headless Analyzer cannot be launched")

    # Keep untrusted/user-supplied paths in an environment value, never in the
    # fixed PowerShell command text. PowerShell splatting preserves arguments.
    command = (
        "$ErrorActionPreference = 'Stop'; "
        "$ghArgs = ConvertFrom-Json -InputObject $env:OCZ_GHIDRA_ARGV; "
        "$env:GHIDRA_INSTALL_DIR = $env:OCZ_GHIDRA_HOME; "
        "$env:JAVA_HOME = $env:OCZ_GHIDRA_JAVA_HOME; "
        "$env:PATH = \"$env:JAVA_HOME\\bin;$env:PATH\"; "
        "$env:GHIDRA_HEADLESS_MAXMEM = '1536M'; "
        "$env:JAVA_TOOL_OPTIONS = '-Xmx1536m -XX:ActiveProcessorCount=4'; "
        "& $env:OCZ_GHIDRA_HEADLESS @ghArgs; exit $LASTEXITCODE"
    )
    env = os.environ.copy()
    env.update(
        {
            "OCZ_GHIDRA_ARGV": json.dumps(args),
            "OCZ_GHIDRA_HOME": str(ghidra_home),
            "OCZ_GHIDRA_JAVA_HOME": str(java_home),
            "OCZ_GHIDRA_HEADLESS": str(headless),
        }
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        process = subprocess.Popen(
            [powershell, "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", command],
            stdout=log,
            stderr=subprocess.STDOUT,
            env=env,
            creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
        )
        try:
            code = process.wait(timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            # This PID is the PowerShell process created above. Its child tree
            # is limited to this invocation's Headless Analyzer and Java VM.
            subprocess.run(
                ["taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=15)
            raise ToolError(f"Headless Analyzer exceeded {timeout_seconds}s; log: {log_path}")

    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    return code, text


def run_headless_smoke(ghidra_home: Path, java_home: Path, headless: Path) -> dict[str, Any]:
    log = DEFAULT_DATA_ROOT / "logs" / "headless-help.log"
    code, output = run_headless(headless, ghidra_home, java_home, ["-help"], log, 60)
    usable = "-import" in output and "-process" in output and "analyzeHeadless" in output
    return {"functional": usable, "exitCode": code, "log": str(log)}


def run_pyghidra_smoke(ghidra_home: Path, java_home: Path) -> dict[str, Any]:
    os.environ["JAVA_HOME"] = str(java_home)
    os.environ["PATH"] = str(java_home / "bin") + os.pathsep + os.environ.get("PATH", "")
    os.environ["JAVA_TOOL_OPTIONS"] = f"-Xmx{MAX_HEAP} -XX:ActiveProcessorCount=4"
    os.environ["GHIDRA_INSTALL_DIR"] = str(ghidra_home)
    import pyghidra

    pyghidra.start(install_dir=ghidra_home)
    from ghidra.framework import Application

    version = str(Application.getApplicationVersion())
    return {"functional": pyghidra.started(), "version": version}


def command_doctor(args: argparse.Namespace) -> int:
    ghidra_home, java_home = resolve_installation()
    headless = ghidra_home / "support" / "analyzeHeadless.bat"
    java = java_home / "bin" / "java.exe"
    result: dict[str, Any] = {
        "ghidraHome": str(ghidra_home),
        "ghidraVersion": ghidra_version(ghidra_home),
        "headlessAnalyzer": headless.is_file(),
        "javaHome": str(java_home),
        "javaExecutable": java.is_file(),
        "python": platform.python_version(),
        "pyghidraImportable": importlib.util.find_spec("pyghidra") is not None,
        "jpypeImportable": importlib.util.find_spec("jpype") is not None,
        "globalEnvironmentChanged": False,
    }
    if args.smoke:
        if not result["headlessAnalyzer"] or not result["javaExecutable"]:
            raise ToolError("Ghidra or Java runtime files are missing")
        if not result["pyghidraImportable"]:
            raise ToolError("PyGhidra is not importable from this Python environment")
        result["pyghidraSmoke"] = run_pyghidra_smoke(ghidra_home, java_home)
        result["headlessSmoke"] = run_headless_smoke(ghidra_home, java_home, headless)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    ok = (
        result["headlessAnalyzer"]
        and result["javaExecutable"]
        and result["pyghidraImportable"]
        and result["jpypeImportable"]
        and bool(result["ghidraVersion"])
    )
    if args.smoke:
        ok = ok and result["pyghidraSmoke"]["functional"] and result["headlessSmoke"]["functional"]
    return 0 if ok else 2


def _project_and_program_names(source: Path, digest: str, project_name: Optional[str]) -> tuple[str, str]:
    safe_stem = re.sub(r"[^A-Za-z0-9._-]+", "_", source.stem).strip("._-") or "program"
    project = project_name or f"ocz_{safe_stem}_{digest[:10]}"
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,120}", project):
        raise ToolError("Project name must contain only letters, numbers, '.', '_' or '-'")
    return project, source.name


def _headless_project_args(
    project_dir: Path,
    project_name: str,
    source: Path,
    program_name: str,
    project_exists: bool,
    reimport: bool,
    skip_analysis: bool,
    analysis_timeout: int,
    max_cpu: int,
    processor: Optional[str],
    compiler: Optional[str],
    loader: Optional[str],
    loader_options: list[tuple[str, str, str]],
    pre_script: Optional[Path],
    post_script: Optional[Path],
    pre_script_args: list[str],
    post_script_args: list[str],
    log_path: Path,
) -> list[str]:
    argv = [str(project_dir), project_name]
    if project_exists and not reimport:
        argv.extend(["-process", program_name])
    else:
        argv.extend(["-import", str(source)])
        if project_exists and reimport:
            argv.append("-overwrite")
    argv.extend(["-max-cpu", str(max_cpu), "-log", str(log_path)])
    if skip_analysis:
        argv.append("-noanalysis")
    else:
        argv.extend(["-analysisTimeoutPerFile", str(analysis_timeout)])
    if processor:
        argv.extend(["-processor", processor])
    if compiler:
        argv.extend(["-cspec", compiler])
    if loader:
        argv.extend(["-loader", loader])
    for _, value, headless_option in loader_options:
        argv.extend([headless_option, value])
    scripts = [script for script in (pre_script, post_script) if script]
    if scripts:
        script_dirs = list(dict.fromkeys(str(script.parent) for script in scripts))
        argv.extend(["-scriptPath", os.pathsep.join(script_dirs)])
    if pre_script:
        argv.extend(["-preScript", pre_script.name, *pre_script_args])
    if post_script:
        argv.extend(["-postScript", post_script.name, *post_script_args])
    return argv


def _loader_options(args: argparse.Namespace, source: Path) -> list[tuple[str, str, str]]:
    requested = (
        ("RDRAM dump file", args.loader_rdram, "-loader-rdram"),
        ("Signature file", args.loader_signature, "-loader-signature"),
        ("PIF file", args.loader_pif, "-loader-pif"),
        ("Modem.bin file", args.loader_modem, "-loader-modem"),
    )
    options: list[tuple[str, str, str]] = []
    for name, raw_path, headless_option in requested:
        if not raw_path:
            continue
        path = Path(raw_path).expanduser().resolve(strict=True)
        if not path.is_file():
            raise ToolError(f"{name} is not a file: {path}")
        if path == source:
            raise ToolError(f"{name} cannot refer to the input ROM itself")
        options.append((name, str(path), headless_option))
    if options and args.loader != "N64LoaderWVLoader":
        raise ToolError("N64 loader files require --loader N64LoaderWVLoader")
    return options


def _script_path(raw_path: Optional[str]) -> Optional[Path]:
    if not raw_path:
        return None
    path = Path(raw_path).expanduser().resolve(strict=True)
    if not path.is_file():
        raise ToolError(f"Ghidra script is not a file: {path}")
    return path


def _pyghidra_project(
    source: Path,
    project_dir: Path,
    project_name: str,
    program_name: str,
    ghidra_home: Path,
    java_home: Path,
    project_exists: bool,
    reimport: bool,
    reanalyze: bool,
    skip_analysis: bool,
    analysis_timeout: int,
    processor: Optional[str],
    compiler: Optional[str],
    loader_name: Optional[str],
    loader_options: list[tuple[str, str, str]],
    pre_script: Optional[Path],
    post_script: Optional[Path],
    pre_script_args: list[str],
    post_script_args: list[str],
    export_args: argparse.Namespace,
) -> tuple[dict[str, Any], str]:
    os.environ["JAVA_HOME"] = str(java_home)
    os.environ["PATH"] = str(java_home / "bin") + os.pathsep + os.environ.get("PATH", "")
    os.environ["JAVA_TOOL_OPTIONS"] = f"-Xmx{MAX_HEAP} -XX:ActiveProcessorCount=4"
    os.environ["GHIDRA_INSTALL_DIR"] = str(ghidra_home)
    import pyghidra

    pyghidra.start(install_dir=ghidra_home)
    project = pyghidra.open_project(project_dir, project_name, create=not project_exists)
    try:
        if not project_exists or reimport:
            builder = pyghidra.program_loader().project(project).source(str(source)).name(program_name)
            if loader_name:
                builder = builder.loaders(loader_name)
            for option_name, value, _ in loader_options:
                builder = builder.addLoaderArg(option_name, value)
            if processor:
                builder = builder.language(processor)
            if compiler:
                builder = builder.compiler(compiler)
            with builder.load() as loaded:
                loaded.save(pyghidra.task_monitor(analysis_timeout))

        with pyghidra.program_context(project, "/" + program_name) as program:
            script_results: list[dict[str, str]] = []
            if export_args.engine == "pyghidra" and pre_script:
                stdout, stderr = pyghidra.ghidra_script(pre_script, project, program, pre_script_args)
                script_results.append({"stage": "pre", "path": str(pre_script), "stdout": stdout, "stderr": stderr})
            if not skip_analysis and (not project_exists or reanalyze):
                pyghidra.analyze(program, pyghidra.task_monitor(analysis_timeout))
                program.save("OCZ bounded analysis", pyghidra.task_monitor(30))
            if export_args.engine == "pyghidra" and post_script:
                stdout, stderr = pyghidra.ghidra_script(post_script, project, program, post_script_args)
                script_results.append({"stage": "post", "path": str(post_script), "stdout": stdout, "stderr": stderr})
            result = _export_metadata(program, source, pyghidra, export_args)
            if script_results:
                result["scripts"] = script_results
        return result, "/" + program_name
    finally:
        project.close()


def _function_references(program: Any, function: Any, max_references: int = 80) -> dict[str, list[dict[str, str]]]:
    manager = program.getFunctionManager()
    reference_manager = program.getReferenceManager()
    incoming: dict[str, dict[str, str]] = {}
    for reference in reference_manager.getReferencesTo(function.getEntryPoint()):
        address = reference.getFromAddress()
        caller = manager.getFunctionContaining(address)
        item = {
            "from": str(address),
            "function": caller.getName() if caller else "",
            "type": str(reference.getReferenceType()),
        }
        incoming[item["from"] + item["function"]] = item
        if len(incoming) >= max_references:
            break

    outgoing: dict[str, dict[str, str]] = {}
    instructions = program.getListing().getInstructions(function.getBody(), True)
    for instruction in instructions:
        for reference in instruction.getReferencesFrom():
            if not reference.getReferenceType().isCall():
                continue
            target = reference.getToAddress()
            callee = manager.getFunctionAt(target) or manager.getFunctionContaining(target)
            item = {
                "to": str(target),
                "function": callee.getName() if callee else "",
                "type": str(reference.getReferenceType()),
            }
            outgoing[item["to"] + item["function"]] = item
            if len(outgoing) >= max_references:
                break
        if len(outgoing) >= max_references:
            break
    return {"incoming": list(incoming.values()), "calls": list(outgoing.values())}


def _export_metadata(program: Any, source: Path, pyghidra: Any, args: argparse.Namespace) -> dict[str, Any]:
    function_manager = program.getFunctionManager()
    regexes = [re.compile(pattern, re.IGNORECASE) for pattern in args.function_regex]
    functions: list[Any] = []
    for function in function_manager.getFunctions(True):
        if regexes and not any(regex.search(function.getName()) for regex in regexes):
            continue
        functions.append(function)
        if len(functions) >= args.max_functions:
            break

    decompiler = None
    decompile_count = min(args.decompile_limit, len(functions))
    if decompile_count:
        from ghidra.app.decompiler import DecompInterface

        decompiler = DecompInterface()
        decompiler.openProgram(program)

    selected: list[dict[str, Any]] = []
    try:
        for index, function in enumerate(functions):
            entry = {
                "name": function.getName(),
                "entry": str(function.getEntryPoint()),
                "namespace": function.getParentNamespace().getName(True),
                "signature": str(function.getSignature()),
                "bodyBytes": int(function.getBody().getNumAddresses()),
                "thunk": bool(function.isThunk()),
                "references": _function_references(program, function),
            }
            if decompiler is not None and index < decompile_count:
                result = decompiler.decompileFunction(function, args.decompile_timeout, pyghidra.task_monitor(args.decompile_timeout))
                if result and result.decompileCompleted() and result.getDecompiledFunction():
                    entry["decompiledC"] = result.getDecompiledFunction().getC()[: args.max_decompiled_chars]
                else:
                    entry["decompileError"] = str(result.getErrorMessage()) if result else "No result"
            selected.append(entry)
    finally:
        if decompiler is not None:
            decompiler.dispose()

    language = program.getLanguage()
    compiler = program.getCompilerSpec()
    blocks = []
    for block in program.getMemory().getBlocks():
        blocks.append(
            {
                "name": block.getName(),
                "start": str(block.getStart()),
                "end": str(block.getEnd()),
                "size": int(block.getSize()),
                "read": bool(block.isRead()),
                "write": bool(block.isWrite()),
                "execute": bool(block.isExecute()),
            }
        )
    return {
        "schemaVersion": 1,
        "input": source.name,
        "inputSha256": sha256_file(source),
        "program": program.getName(),
        "languageId": str(language.getLanguageID()),
        "compilerSpecId": str(compiler.getCompilerSpecID()),
        "imageBase": str(program.getImageBase()),
        "totalFunctions": int(function_manager.getFunctionCount()),
        "selectedFunctions": selected,
        "selectionCapped": len(selected) >= args.max_functions,
        "memoryBlocks": blocks,
    }


def command_analyze(args: argparse.Namespace) -> int:
    ghidra_home, java_home, headless = require_runtime()
    source = Path(args.input).expanduser().resolve(strict=True)
    if not source.is_file():
        raise ToolError(f"Input is not a file: {source}")
    rom_format = n64_format(source)
    if rom_format and not args.loader:
        raise ToolError(
            f"Detected {rom_format}; refusing raw import. Specify a reviewed and installed N64 loader."
        )
    if args.loader and rom_format is None:
        raise ToolError("A custom loader was requested, but the input did not match a supported N64 ROM header")

    digest = sha256_file(source)
    loader_options = _loader_options(args, source)
    pre_script = _script_path(args.pre_script)
    post_script = _script_path(args.post_script)
    scripts = [script for script in (pre_script, post_script) if script]
    if scripts and args.engine == "both":
        raise ToolError("Ghidra scripts require one explicit engine; use --engine headless for Java or --engine pyghidra for Python")
    if args.engine == "pyghidra" and any(script.suffix.lower() != ".py" for script in scripts):
        raise ToolError("PyGhidra scripts must use the .py extension")
    if args.engine == "headless" and any(script.suffix.lower() == ".py" for script in scripts):
        raise ToolError("Headless Analyzer cannot run PyGhidra .py scripts; use --engine pyghidra")
    project_name, program_name = _project_and_program_names(source, digest, args.project_name)
    project_dir = Path(args.project_dir).expanduser().resolve() if args.project_dir else DEFAULT_DATA_ROOT / "projects"
    if not private_path(project_dir):
        raise ToolError("Ghidra projects must be outside the Git checkout")
    if any(part.startswith(".") for part in project_dir.parts if part != project_dir.anchor):
        raise ToolError("Ghidra rejects project paths with dot-prefixed folders; use a normal local data path")
    project_dir.mkdir(parents=True, exist_ok=True)
    project_file = project_dir / f"{project_name}.gpr"
    project_exists = project_file.is_file()
    if loader_options and project_exists and not args.reimport:
        raise ToolError("Loader options apply during import; use --reimport to replace this project's program")
    if project_exists and args.reimport and args.engine == "pyghidra":
        raise ToolError("Use the Headless Analyzer engine for explicit re-import; PyGhidra-only mode does not replace projects")
    if args.skip_analysis and args.reanalyze:
        raise ToolError("--skip-analysis and --reanalyze are mutually exclusive")
    skip_analysis = args.skip_analysis or (project_exists and not args.reimport and not args.reanalyze)
    if rom_format and not args.function_regex:
        raise ToolError("N64 analysis requires at least one focused --function-regex to limit exported findings")
    if rom_format and not skip_analysis and not args.allow_full_rom_analysis:
        raise ToolError(
            "Full-file analysis of an N64 ROM is disabled by default. Use a module/function-scoped workflow; "
            "the --allow-full-rom-analysis switch is reserved for an explicitly justified pass."
        )

    logs_dir = DEFAULT_DATA_ROOT / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    headless_log = logs_dir / f"{project_name}.headless.log"
    headless_run_log = logs_dir / f"{project_name}.wrapper.log"
    if args.engine in ("headless", "both"):
        argv = _headless_project_args(
            project_dir,
            project_name,
            source,
            program_name,
            project_exists,
            args.reimport,
            skip_analysis,
            args.analysis_timeout,
            args.max_cpu,
            args.processor,
            args.compiler,
            args.loader,
            loader_options,
            pre_script,
            post_script,
            args.pre_script_arg,
            args.post_script_arg,
            headless_log,
        )
        code, output = run_headless(
            headless, ghidra_home, java_home, argv, headless_run_log, args.timeout_seconds
        )
        if "REPORT SCRIPT ERROR" in output:
            tail = "\n".join(output.splitlines()[-40:])
            raise ToolError(f"Headless Analyzer reported a script error. Log: {headless_log}\n{tail}")
        if code != 0:
            tail = "\n".join(output.splitlines()[-40:])
            raise ToolError(f"Headless Analyzer failed with exit code {code}. Log: {headless_log}\n{tail}")

    if args.engine == "headless":
        print(json.dumps({"functional": True, "project": str(project_file), "log": str(headless_log)}, indent=2))
        return 0

    project_exists = project_file.is_file()
    pyghidra_reimport = args.reimport and args.engine == "pyghidra"
    metadata, _ = _pyghidra_project(
        source,
        project_dir,
        project_name,
        program_name,
        ghidra_home,
        java_home,
        project_exists,
        pyghidra_reimport,
        args.reanalyze and args.engine == "pyghidra",
        skip_analysis,
        args.analysis_timeout,
        args.processor,
        args.compiler,
        args.loader,
        loader_options,
        pre_script,
        post_script,
        args.pre_script_arg,
        args.post_script_arg,
        args,
    )
    if sha256_file(source) != digest:
        raise ToolError("Input SHA-256 changed during analysis; source may have been modified externally")
    output = Path(args.output).expanduser().resolve() if args.output else DEFAULT_DATA_ROOT / "results" / f"{project_name}.json"
    if not private_path(output):
        raise ToolError("Metadata output must be outside the Git checkout")
    if output == source:
        raise ToolError("Metadata output cannot overwrite the input binary")
    output.parent.mkdir(parents=True, exist_ok=True)
    metadata["ghidraVersion"] = ghidra_version(ghidra_home)
    metadata["inputN64Format"] = rom_format
    metadata["engine"] = args.engine
    metadata["analysisPerformed"] = not skip_analysis
    metadata["projectName"] = project_name
    metadata["programPath"] = "/" + program_name
    output.write_text(json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"functional": True, "project": str(project_file), "metadata": str(output)}, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run local, bounded Ghidra analysis for OCZ reverse-engineering work.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    doctor = subparsers.add_parser("doctor", help="Check Ghidra, Java, Python, PyGhidra, and optionally execute smoke checks")
    doctor.add_argument("--smoke", action="store_true", help="Start PyGhidra and execute the official Headless Analyzer help path")
    doctor.set_defaults(handler=command_doctor)

    analyze = subparsers.add_parser("analyze", help="Import/reuse a local program, analyze it, and export selected metadata")
    analyze.add_argument("--input", required=True, help="Local binary path; the source file is read only")
    analyze.add_argument("--engine", choices=("both", "headless", "pyghidra"), default="both")
    analyze.add_argument("--project-dir", help="Ghidra project parent directory; defaults under local app data")
    analyze.add_argument("--project-name", help="Persistent Ghidra project name; defaults to an input-hash name")
    analyze.add_argument("--output", help="Metadata JSON path; defaults under local app data")
    analyze.add_argument("--processor", help="Explicit Ghidra language ID; never guessed for a raw ROM")
    analyze.add_argument("--compiler", help="Explicit Ghidra compiler specification ID")
    analyze.add_argument("--loader", help="Explicit installed Ghidra loader name, e.g. N64LoaderWVLoader")
    analyze.add_argument("--loader-rdram", help="N64LoaderWV RDRAM dump captured while target code was loaded")
    analyze.add_argument("--loader-signature", help="N64LoaderWV signature or N64 symbol file")
    analyze.add_argument("--loader-pif", help="N64LoaderWV PIF ROM image")
    analyze.add_argument("--loader-modem", help="N64LoaderWV modem ROM image")
    analyze.add_argument("--pre-script", help="Ghidra script to run before auto-analysis")
    analyze.add_argument("--pre-script-arg", action="append", default=[], help="Argument passed to the pre-analysis script")
    analyze.add_argument("--post-script", help="Ghidra script to run after auto-analysis")
    analyze.add_argument("--post-script-arg", action="append", default=[], help="Argument passed to the post-analysis script")
    analyze.add_argument("--function-regex", action="append", default=[], help="Repeatable case-insensitive function-name filter")
    analyze.add_argument("--max-functions", type=int, default=500, help="Maximum selected functions exported")
    analyze.add_argument("--decompile-limit", type=int, default=0, help="Maximum selected functions to decompile")
    analyze.add_argument("--decompile-timeout", type=int, default=30, help="Seconds per selected function decompile")
    analyze.add_argument("--max-decompiled-chars", type=int, default=12000, help="Maximum local C text exported per function")
    analyze.add_argument("--analysis-timeout", type=int, default=120, help="Per-file analysis timeout in seconds")
    analyze.add_argument("--timeout-seconds", type=int, default=900, help="Whole Headless Analyzer process timeout")
    analyze.add_argument("--max-cpu", type=int, default=2, help="Maximum concurrent Ghidra analysis workers")
    analyze.add_argument("--skip-analysis", action="store_true", help="Import/open only; do not run auto-analysis")
    analyze.add_argument("--reanalyze", action="store_true", help="Explicitly rerun analysis on an existing project")
    analyze.add_argument("--allow-full-rom-analysis", action="store_true", help="Explicitly allow whole-file auto-analysis of an N64 ROM")
    analyze.add_argument("--reimport", action="store_true", help="Replace the project program from the input; otherwise reuse it")
    analyze.set_defaults(handler=command_analyze)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if getattr(args, "max_functions", 1) < 1:
        parser.error("--max-functions must be at least 1")
    if getattr(args, "decompile_limit", 0) < 0:
        parser.error("--decompile-limit cannot be negative")
    if getattr(args, "analysis_timeout", 1) < 1 or getattr(args, "timeout_seconds", 1) < 1:
        parser.error("Timeouts must be positive")
    if getattr(args, "max_cpu", 1) < 1 or getattr(args, "max_cpu", 1) > 16:
        parser.error("--max-cpu must be between 1 and 16")
    try:
        return args.handler(args)
    except (ToolError, OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
