import ast
import os
import subprocess
import sys
from pathlib import Path

import pytest

from pyinsta360 import _sdk

ROOT = Path(__file__).resolve().parent.parent
SDK_DIR = os.environ.get("INSTA360_SDK_DIR")
FAKE_LIB = ROOT / "build" / "fake_sdk" / "libCameraSDK.so"
needs_sdk = pytest.mark.skipif(not SDK_DIR, reason="INSTA360_SDK_DIR is not set")


def run(*cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def imported_sdk_symbols():
    out = run("nm", "-D", "--undefined-only", _sdk.__file__)
    return {line.split()[-1] for line in out.splitlines() if "ins_camera" in line}


def defined_symbols(lib):
    out = run("nm", "-D", "--defined-only", str(lib))
    return {line.split()[-1] for line in out.splitlines() if "ins_camera" in line}


def test_sdk_is_found_through_runpath_not_rpath():
    dynamic = run("readelf", "-d", _sdk.__file__)
    assert "(RUNPATH)" in dynamic
    assert "(RPATH)" not in dynamic
    runpath = next(l for l in dynamic.splitlines() if "(RUNPATH)" in l)
    assert runpath.split("[")[1].rstrip("]").endswith("/lib")
    if SDK_DIR:
        assert str(Path(SDK_DIR).resolve() / "lib") in runpath


def test_extension_needs_the_sdk_by_soname():
    assert "[libCameraSDK.so]" in run("readelf", "-d", _sdk.__file__)


def test_fake_sdk_defines_every_symbol_the_extension_imports():
    missing = imported_sdk_symbols() - defined_symbols(FAKE_LIB)
    assert not missing, "fake SDK lacks: %s" % sorted(missing)


@needs_sdk
def test_real_sdk_exports_every_symbol_the_extension_imports():
    missing = imported_sdk_symbols() - defined_symbols(Path(SDK_DIR) / "lib" / "libCameraSDK.so")
    assert not missing, "real SDK lacks: %s" % sorted(missing)


@needs_sdk
def test_generated_enums_are_current():
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "gen_enums.py"), "--sdk-dir", SDK_DIR, "--check"],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr


def stub_members(path):
    """Public names declared by a stub: {name: set of public members, or None for functions}."""
    declared = {}
    for node in ast.parse(path.read_text()).body:
        if isinstance(node, ast.ClassDef):
            members = set()
            for item in node.body:
                if isinstance(item, ast.FunctionDef):
                    members.add(item.name)
                elif isinstance(item, ast.AnnAssign) and isinstance(item.target, ast.Name):
                    members.add(item.target.id)
            declared[node.name] = {m for m in members if not m.startswith("_")}
        elif isinstance(node, ast.FunctionDef):
            declared[node.name] = None
    return declared


def test_type_stubs_match_the_module():
    pkg = ROOT / "python" / "pyinsta360"
    declared = stub_members(pkg / "_sdk.pyi")
    declared.update(stub_members(pkg / "_sdk_enums.pyi"))
    declared.pop("_Enum", None)
    assert set(declared) == {n for n in dir(_sdk) if not n.startswith("_")}

    for name, members in declared.items():
        if members is None:
            continue
        cls = getattr(_sdk, name)
        if issubclass(cls, BaseException):
            continue
        if hasattr(cls, "__members__"):
            actual = set(cls.__members__)
        else:
            actual = {m for m in dir(cls) if not m.startswith("_")}
        assert members == actual, name
