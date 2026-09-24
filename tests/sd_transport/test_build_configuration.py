"""Exercise the dependency transformation without importing SCons or building."""

import ast
import os
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def load_audio_hook(project, environment):
    source = ast.parse((ROOT / "scripts/platformio-pre.py").read_text(encoding="utf-8"))
    function = next(node for node in source.body
                    if isinstance(node, ast.FunctionDef)
                    and node.name == "configure_explicit_audio_driver_instances")
    namespace = {"os": os, "project_dir": str(project), "pio_env": environment,
                 "is_esp32_env": True}
    exec(compile(ast.Module(body=[function], type_ignores=[]), "audio-hook", "exec"), namespace)
    return namespace[function.name]


class AudioConfigurationTest(unittest.TestCase):
    def test_explicit_instances_are_opt_in_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / ".pio/libdeps/wio_tracker_l2/audio-driver"
            (library / "src").mkdir(parents=True)
            (library / "library.properties").write_text("version=0.3.1\n", encoding="utf-8")
            header = library / "src/AudioDriver.h"
            original = ("namespace audio_driver {\nclass AudioDriverES8311Class {};\n"
                        "// -- Drivers\nstatic AudioDriverES8311Class AudioDriverES8311;\n"
                        "}  // namespace audio_driver\n")
            header.write_text(original, encoding="utf-8")
            hook = load_audio_hook(root, "wio_tracker_l2")
            hook()
            patched = header.read_text(encoding="utf-8")
            self.assertIn("#ifndef TRAIL_MATE_AUDIO_DRIVER_EXPLICIT_INSTANCES\n", patched)
            self.assertIn("class AudioDriverES8311Class {};\n// -- Drivers", patched)
            self.assertIn("#endif\n\n}  // namespace audio_driver", patched)
            hook()
            self.assertEqual(header.read_text(encoding="utf-8"), patched)

    def test_shared_spi_environments_do_not_touch_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            # No audio-driver dependency exists: these environments must return
            # before attempting any file lookup or mutation.
            for environment in ("tdeck", "tdeck_debug", "tlora_pager_sx1262",
                                "tlora_pager_lr1121", "tlora_pager_sx1262_debug"):
                load_audio_hook(Path(temporary), environment)()
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_unknown_dependency_fails_without_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / ".pio/libdeps/wio_tracker_l2/audio-driver"
            (library / "src").mkdir(parents=True)
            (library / "library.properties").write_text("version=0.4.0\n", encoding="utf-8")
            header = library / "src/AudioDriver.h"
            header.write_text("unexpected upstream source", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                load_audio_hook(root, "wio_tracker_l2")()
            self.assertEqual(header.read_text(encoding="utf-8"), "unexpected upstream source")


if __name__ == "__main__":
    unittest.main()
