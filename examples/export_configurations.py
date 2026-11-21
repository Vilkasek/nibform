"""Exports every configuration of a project to STEP, one file each.

    nibform --run export_configurations.py part.nibform [folder]

The files are named after the project and the configuration (part-Small.step) and go
into the folder (the project's own, if none is given).
"""
import os
import sys

import nibform as nf

if len(sys.argv) < 2:
    sys.exit("Usage: nibform --run export_configurations.py project.nibform [folder]")
path = sys.argv[1]
folder = sys.argv[2] if len(sys.argv) > 2 else (os.path.dirname(path) or ".")
os.makedirs(folder, exist_ok=True)

doc = nf.open(path)
name = os.path.splitext(os.path.basename(path))[0]
if not doc.configurations:
    sys.exit(f"{path} has no configurations")
for configuration in doc.configurations:
    doc.apply_configuration(configuration)
    if doc.errors:
        print(f"{configuration}: skipped, it doesn't build:", *doc.errors)
        continue
    target = os.path.join(folder, f"{name}-{configuration}.step")
    doc.export(target)
    print(f"{configuration}: {target}")
