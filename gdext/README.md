# gdext

GDExtension bridge (C++). The only C++ code that includes Godot headers; links `citysim_core` and godot-cpp.

## Notes

- **Flavors.** Debug builds bind godot-cpp's `editor` target (`libcitysim_gdext.linux.editor.x86_64.so`), which the editor and the games it launches use; release builds bind `template_release`. Set `CITYSIM_GODOT_TARGET` to build another flavor.
- **Docs.** `doc_classes/*.xml` is compiled into the editor and debug flavors for the in-editor help.
- **First start.** Godot 4.7.2 aborts at the end of the first project scan that finds a GDExtension registering a class. Run `tools/godot_prepare.sh` on a fresh checkout (it seeds `.godot/extension_list.cfg`); in the GUI editor, expect to restart once if it closes the first time the extension is found.
