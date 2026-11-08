"""Run real Debian dependency analysis with a deliberately missing plugin ABI."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="trailmate-native-package-") as temporary:
    root = Path(temporary)
    producer = root / "producer"
    producer.mkdir()
    (root / "dependency.c").write_text("int fixture_dependency(void) { return 42; }\n")
    (root / "plugin.c").write_text("extern int fixture_dependency(void); int fixture_plugin(void) { return fixture_dependency(); }\n")
    dependency = root / "libtrailmate_missing_fixture.so.0"
    plugin = producer / "libcoreclrtraceptprovider.so"
    subprocess.run(["cc", "-shared", "-fPIC", str(root / "dependency.c"),
                    "-Wl,-soname,libtrailmate_missing_fixture.so.0", "-o", str(dependency)], check=True)
    subprocess.run(["cc", "-shared", "-fPIC", str(root / "plugin.c"),
                    str(dependency), "-o", str(plugin)], check=True)
    dependency.unlink()
    script = root / "check.cmake"
    result_file = root / "depends.txt"
    script.write_text("\n".join([
        'set(CPACK_PACKAGE_NAME trailmate-test)',
        'set(CPACK_DEBIAN_PACKAGE_DEPENDS "ca-certificates")',
        'set(TRAIL_MATE_NATIVE_DEPENDENCY_BINARIES /bin/true)',
        f'set(TRAIL_MATE_NATIVE_DEPENDENCY_PRODUCER "{producer}")',
        f'set(TRAIL_MATE_NATIVE_DEPENDENCY_WORK "{root / "work"}")',
        f'include("{repo / "apps/linux_lvgl/cmake/native_package_dependencies.cmake"}")',
        f'file(WRITE "{result_file}" "${{CPACK_DEBIAN_PACKAGE_DEPENDS}}")',
    ]))
    subprocess.run(["cmake", "-P", str(script)], check=True)
    depends = result_file.read_text()
    assert depends.startswith("ca-certificates, ") and "libc6" in depends, depends
    # A missing dependency in any ordinary bundled ELF must remain fatal.
    plugin.rename(producer / "required-plugin.so")
    result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
    assert result.returncode and "libtrailmate_missing_fixture.so.0" in result.stderr, result.stderr
print("Native package dependency regression passed: optional tracing only; required ELF failures remain fatal")
