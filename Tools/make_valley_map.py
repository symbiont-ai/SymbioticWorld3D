"""Create the empty /Game/Maps/Valley level from the command line.

Run once (the editor opens, executes this, and exits):

  "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ^
     "<project>/SymbioticWorld.uproject" -run=pythonscript -script="<project>/Tools/make_valley_map.py"

or, if the commandlet path fails, open the editor with
  -ExecutePythonScript="<project>/Tools/make_valley_map.py"

The level is intentionally empty: ASWGameMode spawns the floor, lights, sky
and fog at runtime, so nothing hand-authored is required for the sim to run.
Replace it with a Fab/Quixel valley later and set bSpawnEnvironment=false on
the game mode.
"""
import unreal

MAP_PATH = "/Game/Maps/Valley"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

if eas.does_asset_exist(MAP_PATH):
    unreal.log(f"{MAP_PATH} already exists; nothing to do")
else:
    ok = les.new_level(MAP_PATH)
    unreal.log(f"new_level -> {ok}")
    saved = les.save_current_level()
    unreal.log(f"save_current_level -> {saved}")

unreal.log("make_valley_map done")
