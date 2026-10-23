import argparse
from pathlib import Path
import json

from . import core


def main():
    parser = argparse.ArgumentParser(description="Dynamic Bottle Liquids author tool; no command opens the desktop editor.")
    commands = parser.add_subparsers(dest="command")
    inspect = commands.add_parser("inspect", help="List supported shapes in a NIF")
    inspect.add_argument("nif", type=Path)
    fit = commands.add_parser("fit", help="Calculate one bottle and save a portable author project")
    fit.add_argument("nif", type=Path); fit.add_argument("--cavity", type=int, required=True, help="Cavity source block number")
    fit.add_argument("--target", type=int, help="Optional manual glass target block number")
    fit.add_argument("--axis"); fit.add_argument("--clearance", type=float, default=.16)
    fit.add_argument("--asset-root", type=Path, help="Original mod Data folder containing Materials")
    fit.add_argument("--trim", type=float, default=.005)
    fit.add_argument("--pack", required=True); fit.add_argument("--id", required=True)
    fit.add_argument("--mesh-path", required=True); fit.add_argument("--group", default="spirits")
    fit.add_argument("--hide-liquid", action="store_true"); fit.add_argument("--output", type=Path, required=True)
    export = commands.add_parser("export", help="Export a saved project as a compatibility patch ZIP")
    export.add_argument("project", type=Path); export.add_argument("output", type=Path)
    smoke = commands.add_parser("self-test", help="Test the packaged editor with a caller-supplied prepared bottle")
    smoke.add_argument("nif", type=Path); smoke.add_argument("report", type=Path)
    smoke.add_argument('--liquid',type=int,help='Select an original liquid block explicitly for the UI smoke test')
    args = parser.parse_args()
    try:
        if args.command == "inspect":
            model = core.load_mesh(args.nif)
            print(json.dumps([{k: m[k] for k in ("block", "name", "vertices", "triangles", "shader", "material")} for m in model["meshes"]], indent=2))
        elif args.command == "fit":
            model = core.load_mesh(args.nif)
            shapes = {m["block"]: m for m in model["meshes"]}
            if args.cavity not in shapes or (args.target is not None and args.target not in shapes):
                raise ValueError("Select block numbers listed by inspect.")
            from .preparation import calculate
            result = calculate(model, shapes[args.cavity], target=shapes.get(args.target), axis=args.axis,
                               clearance=args.clearance, trim=args.trim, asset_root=args.asset_root)
            project = core.new_project(args.pack)
            core.add_profile(project, model, args.cavity, result['preparation']['target_block'], result, args.id, args.mesh_path, args.group)
            print(core.save_project(project, args.output))
            print(json.dumps(result["audit"], indent=2))
        elif args.command == "export":
            manifest=json.loads((args.project/'project.json').read_text(encoding='utf-8'))
            if manifest.get('format')=='dbl-author-workspace':
                from .workspace import Workspace
                workspace=Workspace();workspace.open(args.project);project=workspace.project()
            else:project=core.load_project(args.project)
            print(core.export_patch(project,args.output))
        elif args.command == "self-test":
            from .smoke import run
            run(args.nif, args.report, args.liquid)
        else:
            from .qt_app import main as gui
            gui()
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"Author tool: {error}\n")


if __name__ == "__main__":
    main()
