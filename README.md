# model-renderer

A small OpenGL 3.3 model viewer in C++: assimp reads the file, and the
renderer draws it with the shading from
[Rock-Generator](https://github.com/DentonW/Rock-Generator) — the same
three-point light rig, the same ground plane and grid, the same supersampled
offscreen buffer. That renderer was a tkinter widget in Python; this is the
same picture with any model assimp can open in front of it.

```
model-renderer sponza.gltf
```

![A cliff-chunk rock from Rock-Generator, drawn by model-renderer](docs/rock.png)

The shaders and camera are line-for-line ports. With the same rock exported
from Rock-Generator as glb, obj, fbx or ply, a frame from this program matches
the Python viewport's to within a few pixels along the axis lines. The one
deliberate difference is the grid, drawn a little heavier here
(`--grid-width`).

## Building

CMake and a C++17 compiler, nothing else. [CPM](https://github.com/cpm-cmake/CPM.cmake)
(vendored in `cmake/`) fetches and builds the three dependencies as part of
the configure step:

| | |
|---|---|
| [glfw](https://github.com/glfw/glfw) 3.4 | window, GL context, input |
| [assimp](https://github.com/assimp/assimp) 6.0.5 | model import |
| [stb](https://github.com/nothings/stb) | texture decoding, PNG screenshots |

```bash
cmake -S . -B build
cmake --build build --config Release
```

The binary lands in `build/bin`. The first configure clones assimp and builds
every importer it ships with, which takes a few minutes; later builds only
compile this project. Setting `CPM_SOURCE_CACHE` to a directory shares those
clones between build trees:

```bash
cmake -S . -B build -DCPM_SOURCE_CACHE=~/.cache/cpm
```

There is no GL loader dependency. `src/gl33.h` declares the sixty-odd entry
points this program uses and resolves them through `glfwGetProcAddress`, which
avoids glad's and gl3w's Python-at-configure-time requirement.

## Controls

| | |
|---|---|
| left-drag | orbit |
| right-drag or middle-drag | pan |
| wheel | zoom |
| double-click | frame the model |
| `F` | frame the model |
| `W` | wireframe: over the surface, then alone, then off |
| `S` | flat shading |
| `G` | grid |
| `B` | ground plane and axis gnomon |
| `T` | textures |
| `V` | vertex colours (off shows the material colour) |
| `C` | back-face culling |
| `P` | screenshot into the working directory |
| `R` | reload the file from disk |
| `Esc` | quit |

Dropping a file onto the window loads it, so the window can be opened empty
and fed afterwards.

## Options

```
model-renderer [options] [model-file]

  --size WxH         window size (default 1280x800)
  --ss N             supersampling factor, 1-4 (default 2)
  --fov DEG          vertical field of view (default 38)
  --yaw DEG          starting view: angle around the model (default 34)
  --pitch DEG        starting view: angle above the ground, -83 to 83 (default 20)
  --flat             start with flat shading
  --wire             start with the wireframe over the surface
  --wire-only        start with the wireframe alone
  --no-ground        hide the ground plane and axis gnomon
  --no-grid          keep the ground, drop the grid lines
  --grid-width PX    grid line width in window pixels (default 1)
  --no-cull          draw back faces too, for inconsistent winding
  --no-textures      shade with material colours only
  --albedo R,G,B     colour for models with no colour of their own
  --exposure X       overall brightness multiplier
  --crease DEG       smoothing limit for meshes that arrive without normals
  --z-up             rotate a Z-up model into this Y-up world
  --screenshot FILE  render one frame to a PNG and exit
```

`--screenshot` opens no visible window, which makes it usable for turntables
and for checking a model from a script:

```bash
model-renderer --size 1600x1200 --ss 3 --screenshot rock.png rock.obj
model-renderer --yaw 90 --pitch -30 --screenshot underside.png rock.obj
```

## How it draws

Four programs, all in `src/shaders.h`, carried over from the Python:

- **background** — a vertical gradient on one oversized triangle.
- **ground** — a single upward-facing quad at the bottom of the model's
  bounding box, shaded analytically: a radial contact shadow under the model,
  grid lines held at a constant width in window pixels (via `fwidth`) at any
  angle, and a fade to the horizon colour. It is back-face culled, so it
  disappears when the camera goes below it.
- **mesh** — key light, fill light, hemisphere ambient (sky above, bounce
  below) and a rim term. Flat shading takes the face normal from
  `dFdx`/`dFdy`, so it needs no second copy of the geometry.
- **line** — the wireframe (dark over the surface, light on its own) and the
  axis gnomon.

Everything is drawn into an offscreen buffer at 2x the window resolution with
a 24-bit depth buffer and blitted down, which antialiases the silhouettes and
keeps flat faces out of z-fighting.

The scene assimp returns is flattened at load: node transforms are baked into
the vertices (normals through the cofactor matrix, with winding and normals
both corrected where a transform mirrors), and the result goes into one vertex
buffer and one index buffer drawn as a list of ranges, one per mesh. The wireframe's unique-edge
buffer is built the first time the overlay is switched on, since it costs a
sort over every triangle corner.

## Notes on models

- **Colour.** A mesh's vertex colours, when it has them, are its albedo, as
  they were in the rock generator. Its exporters also write the average
  vertex colour into the material, as a fallback for software that ignores
  vertex colours; multiplying the two would darken every rock. `V` switches to
  the material colour instead. A mesh without vertex colours uses its
  material's diffuse or base colour, and either way the diffuse texture
  multiplies on top. Where the file names no colour at all, the mesh is drawn
  in the `--albedo` grey. That includes the stand-in materials assimp invents
  for files that have none. It also covers pure-black materials, which is what
  exporters write when they can't translate the real shader (Maya's Arnold
  materials come out of its OBJ exporter that way). Vertex-colour layers that
  are solid black or solid white are placeholders and are ignored.
- **Colour space.** glTF stores vertex and material colours as linear values;
  they are converted to sRGB on load, since that is the space the shading
  works in. Texels are used as authored (image files are already sRGB).
- **Normals.** Whatever the file provides is kept. Meshes that arrive without
  normals get smooth ones, with `--crease` as the limit: edges sharper than
  that stay sharp. The Python split hard edges itself at the same point in the
  pipeline; assimp's `GenSmoothNormals` does the same job here.
- **Textures.** The diffuse/base-colour map is loaded, including textures
  embedded in `.glb` and `.fbx`. Texels are used as authored, in the same
  space as the vertex colours, rather than being converted to linear — that is
  what keeps this renderer's output matching the Python's. Materials with an
  alpha channel are cut out at 0.5, so foliage and fences keep their shape.
- **Orientation.** Y-up is assumed. `--z-up` rotates a Z-up file (much CAD,
  some Blender exports) into place.
