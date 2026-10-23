"""Package the already-built Windows application and its corresponding source."""
import importlib.metadata
import json
from pathlib import Path
import shutil
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "build/author-tool-dist/DBL-patcher"
PACKAGES = ["numpy", "trimesh", "matplotlib", "pillow", "contourpy", "cycler", "kiwisolver",
            "pyparsing", "python-dateutil", "six", "packaging", "fonttools", "tzdata", "pyinstaller",
            "PySide6", "PySide6_Essentials", "PySide6_Addons", "shiboken6", "PySide6-Fluent-Widgets",
            "PySideSix-Frameless-Window", "darkdetect", "pywin32"]


def main():
    if not (APP / "DBL-patcher.exe").is_file():
        raise SystemExit("Build the application first.")
    report = ROOT / "build/author-tool-packaged-smoke.json"
    if not json.loads(report.read_text())["passed"] or json.loads(report.read_text()).get('version') != '0.9':
        raise SystemExit("Packaged smoke test must pass before packaging.")
    shutil.copy2(ROOT / "tools/author_tool/README.md", APP / "README.md")
    shutil.copy2(ROOT / "LICENSE", APP / "LICENSE")
    shutil.copy2(report, APP / "VALIDATION.json")
    versions = {}
    license_archive = zipfile.ZipFile(APP / 'licenses.zip', 'w', zipfile.ZIP_DEFLATED)
    for package in PACKAGES:
        distribution = importlib.metadata.distribution(package)
        versions[package] = distribution.version
        for relative in distribution.files or []:
            # Preserve full nested license trees, including vendored component notices.
            if any("license" in part.lower() or "copying" in part.lower() or "notice" in part.lower()
                   for part in relative.parts):
                source = Path(distribution.locate_file(relative))
                if source.is_file():
                    license_archive.write(source, package + "/" + relative.as_posix())
    python_root = Path(sys.base_prefix)
    for source in [*python_root.glob("LICENSE*"), *python_root.glob("tcl/*/license.terms")]:
        if source.is_file():
            license_archive.write(source, "python-tcl/" + source.relative_to(python_root).as_posix())
    license_archive.close()
    (APP / "BUILD.json").write_text(json.dumps({"version": "0.9", "python": sys.version,
                                               "packages": versions, "in_game_tested": False}, indent=2) + "\n")
    source_zip = ROOT / "build/DBL-patcher-0.9-source.zip"
    source_files = [ROOT / "LICENSE", ROOT / "tools/nif_mesh.py", ROOT / "tools/nif_document.py",
                    ROOT / "tools/author_tool_entry.py", ROOT / "tests/test_author_tool.py"]
    source_files += [p for p in (ROOT / "tools/author_tool").iterdir() if p.is_file()]
    source_files += [p for p in (ROOT/'tests').glob('test_author*.py') if p not in source_files]
    with zipfile.ZipFile(source_zip, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source_files):
            archive.write(path, path.relative_to(ROOT).as_posix())
    output = ROOT / "build/DBL-patcher-0.9-Windows.zip"
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(APP.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(APP.parent).as_posix())
    print(output)
    print(source_zip)


if __name__ == "__main__":
    main()



