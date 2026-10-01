#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# Run with: python3 tests/build-cache.py

import json
import os
import pathlib
import subprocess
import tempfile
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANAGER = pathlib.Path(os.environ.get("FORGE_PM", ROOT / "build/forge-cli"))


class BuildCacheTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.base = pathlib.Path(self.temporary.name)
        self.project = self.base / "project"
        self.toolchain = self.base / "toolchain"
        self.tools = self.base / "tools"
        self.log = self.base / "tools.log"
        self.project.mkdir()
        (self.toolchain / "build/bin").mkdir(parents=True)
        (self.toolchain / "build/lib").mkdir(parents=True)
        (self.toolchain / "include").mkdir()
        (self.toolchain / "libexec").mkdir()
        self.tools.mkdir()
        self.write_tools()
        (self.toolchain / "include/forge.h").write_text("runtime header\n")
        (self.toolchain / "build/lib/libforge_runtime.a").write_text("runtime\n")
        (self.toolchain / "build/lib/libforge_std.a").write_text("stdlib\n")
        (self.project / "forge.json").write_text(
            json.dumps({"name": "cache-app", "entry": "main.fg", "dependencies": {}})
        )
        (self.project / "main.fg").write_text(
            'import helper;\nnative main { println(helper.value()); return 0; }\n'
        )
        (self.project / "helper.fg").write_text('fn value(): string { return "one"; }\n')
        self.environment = {
            **os.environ,
            "FORGE_ROOT": str(self.toolchain),
            "FORGE_PM_LOG": str(self.log),
            "CC": str(self.tools / "cc"),
            "PATH": f"{self.tools}:{os.environ['PATH']}",
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_tool(self, path: pathlib.Path, body: str) -> None:
        path.write_text("#!/usr/bin/env python3\n" + textwrap.dedent(body))
        path.chmod(0o755)

    def write_tools(self) -> None:
        common = """
            import os, pathlib, sys
            log = pathlib.Path(os.environ["FORGE_PM_LOG"])
            with log.open("a") as stream:
                stream.write(pathlib.Path(sys.argv[0]).name + " " + " ".join(sys.argv[1:]) + "\\n")
        """
        self.write_tool(
            self.toolchain / "build/bin/forge",
            common
            + """
            if os.environ.get("FORGE_FAKE_FAIL") == "forge":
                raise SystemExit(9)
            output = pathlib.Path(sys.argv[sys.argv.index("-o") + 1])
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text("generated from " + pathlib.Path(sys.argv[1]).read_text())
            """,
        )
        self.write_tool(
            self.tools / "cc",
            common
            + """
            if "--version" in sys.argv:
                print("fake-cc 1")
                raise SystemExit(0)
            output = pathlib.Path(sys.argv[sys.argv.index("-o") + 1])
            output.write_text("#!/bin/sh\\necho cached-app\\n")
            output.chmod(0o755)
            """,
        )
        self.write_tool(
            self.tools / "cmake",
            common
            + """
            if "--version" in sys.argv:
                print("cmake version fake-1")
            elif "-B" in sys.argv:
                directory = pathlib.Path(sys.argv[sys.argv.index("-B") + 1])
                directory.mkdir(parents=True, exist_ok=True)
                (directory / "CMakeCache.txt").write_text("configured\\n")
                (directory / "Makefile").write_text("generated\\n")
            else:
                directory = pathlib.Path(sys.argv[sys.argv.index("--build") + 1])
                (directory / "libnative.a").write_text("archive\\n")
            """,
        )
        self.write_tool(
            self.tools / "npm",
            common
            + """
            if "--version" in sys.argv:
                print("fake-npm 1")
            else:
                module = pathlib.Path(sys.argv[sys.argv.index("--prefix") + 1])
                installed = module / "node_modules"
                installed.mkdir(exist_ok=True)
                (installed / "state").write_bytes((module / "package-lock.json").read_bytes())
            """,
        )
        self.write_tool(self.tools / "node", common + 'print("fake-node 1")\n')
        self.write_tool(
            self.tools / "pkg-config",
            common + 'print("fake-pkg-config 1" if "--version" in sys.argv else "")\n',
        )
        self.write_tool(
            self.toolchain / "libexec/esbuild",
            common
            + """
            output = pathlib.Path(next(value.split("=", 1)[1] for value in sys.argv if value.startswith("--outfile=")))
            output.write_text("bundled\\n")
            """,
        )

    def run_manager(self, *arguments: str, extra_environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        environment = {**self.environment, **(extra_environment or {})}
        return subprocess.run(
            [str(MANAGER), *arguments],
            cwd=self.project,
            env=environment,
            text=True,
            capture_output=True,
            timeout=30,
            check=False,
        )

    def calls(self, tool: str) -> int:
        if not self.log.exists():
            return 0
        return sum(line.startswith(tool + " ") for line in self.log.read_text().splitlines())

    def assert_success(self, result: subprocess.CompletedProcess[str]) -> None:
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_hit_and_input_output_invalidation(self) -> None:
        self.assert_success(self.run_manager("build"))
        self.assert_success(self.run_manager("build"))
        self.assertEqual(self.calls("forge"), 1)
        self.assertEqual(sum("--version" not in line for line in self.log.read_text().splitlines() if line.startswith("cc ")), 1)

        (self.project / "helper.fg").write_text('fn value(): string { return "two"; }\n')
        self.assert_success(self.run_manager("build"))
        (self.project / "forge.lock").write_text('{"format":1,"packages":{}}\n')
        self.assert_success(self.run_manager("build"))
        (self.toolchain / "build/bin/forge").write_text(
            (self.toolchain / "build/bin/forge").read_text() + "\n# compiler update\n"
        )
        self.assert_success(self.run_manager("build"))
        (self.project / "build/app").write_text("modified\n")
        self.assert_success(self.run_manager("build"))
        (self.project / "build/main.c").unlink()
        self.assert_success(self.run_manager("build"))
        self.assertEqual(self.calls("forge"), 6)

    def test_failed_build_never_publishes_hit(self) -> None:
        self.assert_success(self.run_manager("build"))
        (self.project / "main.fg").write_text("broken input\n")
        failed = self.run_manager("build", extra_environment={"FORGE_FAKE_FAIL": "forge"})
        self.assertNotEqual(failed.returncode, 0)
        self.assert_success(self.run_manager("build"))
        self.assert_success(self.run_manager("build"))
        self.assertEqual(self.calls("forge"), 3)

    def test_native_module_rebuild_skips_unchanged_configure(self) -> None:
        module = self.add_module("native-module", {"native": {"library": "native", "cmake_target": "native", "pkg_config": []}})
        (module / "CMakeLists.txt").write_text("add_library(native STATIC native.c)\n")
        (module / "native.c").write_text("int native(void) { return 1; }\n")
        self.assert_success(self.run_manager("build"))
        (module / "native.c").write_text("int native(void) { return 2; }\n")
        self.assert_success(self.run_manager("build"))
        self.assertEqual(sum("cmake -S" in line for line in self.log.read_text().splitlines()), 1)
        self.assertEqual(sum("cmake --build" in line for line in self.log.read_text().splitlines()), 2)
        (module / "CMakeLists.txt").write_text("add_library(native STATIC changed.c)\n")
        self.assert_success(self.run_manager("build"))
        self.assertEqual(sum("cmake -S" in line for line in self.log.read_text().splitlines()), 2)

    def test_browser_npm_cache_tracks_lock_and_install_state(self) -> None:
        module = self.add_module("browser-module", {"javascript": {"entry": "index.js", "package_file": "package.json"}})
        (module / "index.js").write_text("export const value = 1;\n")
        (module / "package.json").write_text('{"name":"browser-module"}\n')
        (module / "package-lock.json").write_text('{"lockfileVersion":3}\n')
        self.assert_success(self.run_manager("build", "--emit-js"))
        self.assert_success(self.run_manager("build", "--emit-js"))
        self.assertEqual(sum("npm ci" in line for line in self.log.read_text().splitlines()), 1)
        (self.project / "main.fg").write_text("native main { return 1; }\n")
        self.assert_success(self.run_manager("build", "--emit-js"))
        self.assertEqual(sum("npm ci" in line for line in self.log.read_text().splitlines()), 1)
        (module / "package-lock.json").write_text('{"lockfileVersion":3,"changed":true}\n')
        self.assert_success(self.run_manager("build", "--emit-js"))
        (module / "node_modules/state").write_text("tampered\n")
        self.assert_success(self.run_manager("build", "--emit-js"))
        self.assertEqual(sum("npm ci" in line for line in self.log.read_text().splitlines()), 3)

    def test_unsupported_symlink_builds_without_caching(self) -> None:
        (self.project / "linked.fg").symlink_to("main.fg")
        self.assert_success(self.run_manager("build"))
        self.assert_success(self.run_manager("build"))
        self.assertFalse((self.project / "build/.forge-build-fingerprint").exists())

    def test_compiler_wrapper_changes_without_version_change(self) -> None:
        self.assert_success(self.run_manager("build"))
        compiler = self.tools / "cc"
        compiler.write_text(compiler.read_text() + "\nprint('wrapper changed')\n")
        result = self.run_manager("build")
        self.assert_success(result)
        self.assertNotIn("Build cache hit", result.stdout)

    def test_cached_browser_run_keeps_browser_behavior(self) -> None:
        self.assert_success(self.run_manager("run", "--emit-js"))
        result = self.run_manager("run", "--emit-js")
        self.assert_success(result)
        self.assertIn("Build cache hit", result.stdout)

    def add_module(self, name: str, bridge: dict[str, dict[str, str | list[str]]]) -> pathlib.Path:
        commit = "a" * 40
        module = self.project / ".forge/packages" / name / commit
        module.mkdir(parents=True)
        (module / "module.fg").write_text("fn module_value(): int { return 1; }\n")
        release = {
            "name": name,
            "version": "1.0.0",
            "description": "cache fixture",
            "repository_url": f"https://github.com/forge-language/{name}",
            "git_commit": commit,
            "module": "module.fg",
            "license": "MIT",
            "dependencies": {},
            **bridge,
        }
        dependencies = {name: "1.0.0"}
        (self.project / "forge.json").write_text(json.dumps({"name": "cache-app", "entry": "main.fg", "dependencies": dependencies}))
        (self.project / "forge.lock").write_text(json.dumps({"registry": "builtin", "roots": dependencies, "packages": {name: release}, "format": 1}))
        return module


if __name__ == "__main__":
    if not MANAGER.is_file():
        raise SystemExit(f"Forge package manager not found: {MANAGER}")
    unittest.main()
