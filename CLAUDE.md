# Reflow

3D mesh editor built with C++, OpenGL 3.3, GLFW, ImGui, and GLM.

## Build

```bash
# Build (MSYS2 bash on Windows, VS 18 2026)
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" \
  C:/Users/NoSig/Documents/reflow/build/reflow.vcxproj \
  -p:Configuration=Debug -p:Platform=x64 -verbosity:minimal

# Kill before rebuild if running
taskkill //F //IM reflow.exe 2>/dev/null

# Launch (GUI app, use start so it doesn't block shell)
start C:/Users/NoSig/Documents/reflow/build/Debug/reflow.exe
```

## Architecture

- **Half-edge mesh** data structure (`HEdge`, `Face`, `MeshVertex`, `Edge`)
- `src/main.cpp` — window, input callbacks, render loop, transform modes, all keybindings
- `src/mesh/mesh.cpp` — mesh operations (primitives, picking, extrude, delete, loop cut, triangulate)
- `src/mesh/mesh.h` — data structures and declarations
- `src/renderer/` — camera, shaders, grid
- `src/ui/ui.cpp` — ImGui UI (panels, menus, add primitive popup)
- `res/reflow.ico` + `res/reflow.rc` — Windows icon resource

## Key conventions

- Global state uses `g_` prefix static variables in main.cpp
- Transform modes: 0=none, 1=grab, 2=scale, 3=rotate, 4=edge slide
- Selection modes: Object, Vertex, Edge, Face
- Topology rebuild pattern: clear hedges/faces/edges, rebuild from face vertex loops, link twins, build edge list, recalc_normals, rebuild_gpu
- `push_undo()` before any mesh-modifying operation
- GPU buffers (vao/vbo/ebo) are NOT copied in undo snapshots — rebuilt on restore
- Font: Segoe UI 14pt with 2.5x FontGlobalScale

## Keybindings (Blender-style)

- G=grab, S=scale, R=rotate, GG=edge slide
- Ctrl+R=loop cut, E=extrude, X=delete menu
- Ctrl+T=triangulate, Alt+J=untriangulate
- Alt+RMB=edge loop select, Ctrl+Z=undo, Ctrl+Alt+Z=redo
- 1/3/7=front/right/top view, 5=ortho toggle
