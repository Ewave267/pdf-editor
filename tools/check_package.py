#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate an extracted release archive using the existing integration harness."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('package', type=Path, help='extracted top-level package directory')
parser.add_argument('harness', type=Path, help='built viewer-tests executable')
args = parser.parse_args()
package = args.package.resolve()
with tempfile.TemporaryDirectory(prefix='pdf-editor-relocated-') as temporary:
    root = Path(temporary) / 'installation'
    shutil.copytree(package, root)
    binary = root / 'bin/pdf-form-editor'
    worker = root / 'bin/pdf-render-worker'
    library = root / 'lib/pdf-form-editor/libpdfium.so'
    assert library.is_file()
    for executable in (binary, worker):
        dynamic = subprocess.check_output(['readelf', '-d', executable], text=True)
        for line in dynamic.splitlines():
            if 'RPATH' in line or 'RUNPATH' in line:
                assert '.deps' not in line and 'build-' not in line, line
        dependencies = subprocess.check_output(['ldd', executable], text=True)
        assert 'not found' not in dependencies, dependencies
        assert '.deps' not in dependencies, dependencies
    linkage = subprocess.check_output(['ldd', worker], text=True)
    pdfium_line = next(line for line in linkage.splitlines() if 'libpdfium.so =>' in line)
    resolved = Path(pdfium_line.split('=>', 1)[1].split(' (', 1)[0].strip()).resolve()
    assert resolved == library, pdfium_line
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
    for key in ('LD_LIBRARY_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_PLUGIN_PATH'):
        env.pop(key, None)
    subprocess.run([str(binary), '--version'], env=env, check=True)
    with tempfile.TemporaryFile() as diagnostics:
        process = subprocess.Popen([str(binary)], env=env, stderr=diagnostics)
        try:
            process.wait(timeout=3)
            raise RuntimeError(f'Installed GUI exited prematurely: {process.returncode}')
        except subprocess.TimeoutExpired:
            pass
        finally:
            process.terminate()
            process.wait(timeout=5)
        diagnostics.seek(0)
        output = diagnostics.read().decode(errors='replace')
        assert 'failed to load component' not in output.lower(), output
    # Harness is copied solely for validation; it is never shipped in the archive.
    shutil.copy2(args.harness.resolve(), root / 'bin/viewer-tests')
    probe = args.harness.resolve().parent / 'xfa-probe-worker'
    if probe.exists():
        shutil.copy2(probe, root / 'bin/xfa-probe-worker')
    subprocess.run([str(root / 'bin/viewer-tests')], env=env, check=True)
print('Relocated package startup, runtime linkage and integration checks passed.')
