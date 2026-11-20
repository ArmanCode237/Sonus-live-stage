#!/usr/bin/env python3
"""Normaliza la fecha de modificación del código fuente de Sonus Live Stage.

Uso (copiar a scripts/refresh_project_timestamps.py):
  macOS:   python3 scripts/refresh_project_timestamps.py
  Windows: py -3 scripts\refresh_project_timestamps.py

No modifica contenido ni permisos. Excluye compilaciones, Git y dependencias.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys
import time

EXCLUDED_DIRS = {
    ".git", ".svn", "build", "dist", "node_modules", "CMakeFiles",
    "_deps", ".cache", "__pycache__", ".venv", "venv", "out",
    ".next", ".turbo", ".gradle", "target", "Debug", "Release",
}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Actualizar fechas de modificación de archivos fuente sin tocar compilaciones."
    )
    parser.add_argument(
        "--root", type=Path,
        help="Carpeta raíz del proyecto; por defecto se detecta desde la ubicación del script.",
    )
    parser.add_argument(
        "--future-only", action="store_true",
        help="Modificar únicamente los archivos con fecha en el futuro.",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Mostrar cuántos archivos se modificarían sin cambiar fechas.",
    )
    args = parser.parse_args()
    here = Path(__file__).resolve()
    default_root = here.parent.parent if here.parent.name == "scripts" else Path.cwd()
    root = (args.root or default_root).resolve()
    if not root.is_dir():
        parser.error(f"No existe la carpeta: {root}")

    now_ns = time.time_ns()
    updated = 0
    future = 0
    skipped = 0
    failed = 0
    errors = []

    for folder, dirs, files in os.walk(root, topdown=True, followlinks=False):
        dirs[:] = [d for d in dirs if d not in EXCLUDED_DIRS
                   and not d.startswith("cmake-build-")
                   and not (Path(folder) / d).is_symlink()]
        for filename in files:
            path = Path(folder) / filename
            if path.is_symlink():
                skipped += 1
                continue
            try:
                previous = path.stat()
                is_future = previous.st_mtime_ns > now_ns
                if is_future:
                    future += 1
                if args.future_only and not is_future:
                    skipped += 1
                    continue
                if not args.dry_run:
                    # Conservar fecha de último acceso y cambiar solo modificación.
                    os.utime(path, ns=(previous.st_atime_ns, now_ns))
                updated += 1
            except OSError as exc:
                failed += 1
                if len(errors) < 5:
                    errors.append(f"{path}: {exc}")

    print(f"Proyecto: {root}")
    print(f"Modo: {'SOLO futuros' if args.future_only else 'TODOS los archivos fuente'}"
          + (" (SIMULACIÓN)" if args.dry_run else ""))
    print(f"Archivos {'por actualizar' if args.dry_run else 'actualizados'}: {updated}")
    print(f"Archivos con fecha futura detectados: {future}")
    print(f"Omitidos: {skipped}; errores: {failed}")
    print("Excluidos: .git, build, dist, node_modules y otras carpetas generadas.")
    for error in errors:
        print("ERROR:", error, file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
