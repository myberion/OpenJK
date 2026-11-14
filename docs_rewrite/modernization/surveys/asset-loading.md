# Loading assets during a game: what engines do, the measure and the options for the freeze at the first scoreboard

Written on 14 November 2026, for plan step 14 part 9b ([../PLAN.md](../PLAN.md); [../progress/step-14-fixed-tick.md](../progress/step-14-fixed-tick.md) section 16). The client stops when the scoreboard first shows players who joined during the game (about 150 ms noted in step 11; 65 ms per model measured here): their models are loaded then, all in one frame, and no command leaves meanwhile. The owner asked for the cause to be found and corrected at its source rather than for a better moment to freeze. This page records the measure, what recent engines and the Quake 3 family do about loading in play, and the options with their costs.

Each source is labelled as in the other surveys:

- **primary**: the studio's own article, talk slides or source code, or the engine's or library's own documentation;
- **secondary**: a summary written by someone else;
- **community**: forum posts, player guides, pull requests of forks.

## 1. Summary

1. The freeze is the synchronous loading of the players' skins on the main thread. Measured on ten models: 65 ms per model, of which 57 % is the graphics driver compressing each texture as it is handed over, 26 % the reading and decoding of the images (JPEG), 5 % a second search through the shader scripts, 4 % the sounds.
2. Without the driver's compression (`r_ext_compress_textures 0`) the same ten models take 27 ms each, and the map itself loads in 1.16 s instead of 2.20 s.
3. Recent engines never compress a texture when it is loaded: textures are stored in the GPU's format, read and prepared on other threads, and handed to the GPU a few milliseconds per frame. A synchronous load in play is described as the usual cause of a stall.
4. ioquake3 already ships with the driver's compression off; OpenJK kept it on.
5. Nothing is decided. The options are in section 5.

## 2. What this engine does, and what it costs

Read in this tree (paths relative to `codemp/`):

- A player who joins with a model that no present player uses is drawn with another player's model (`cg_deferPlayers` 1, `CG_SetDeferredClientInfo`, `cgame/cg_players.c`). Every waiting model is loaded in one frame once the scoreboard has been up for more than 10 frames (`cgame/cg_scoreboard.c`), which also happens when the local player dies, or on the command `loaddeferred`.
- Loading a model (`CG_LoadClientInfo`) registers its skin, which finds and loads each of its textures (`RE_RegisterSkin`, `R_FindShader`, `R_FindImageFile`), then its sounds (`CG_LoadCISounds`: each one read and decoded at once, `S_RegisterSound`, `client/snd_dma.cpp`).
- A texture is read from the pk3, decoded to RGBA (`R_LoadImage`: `lib/jpeg-9a` for JPEG, libpng, TGA), its mip levels are computed on the CPU (`R_MipMap`), and each level is handed to the driver as RGBA with a compressed internal format (`GL_COMPRESSED_RGB_S3TC_DXT1_EXT`, `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT`, `rd-vanilla/tr_image.cpp`) while `r_ext_compress_textures` is 1, its default: the driver compresses it during the call.
- A name with no shader script is looked for in the hash table of the scripts, then by reading the whole text of every script from its start (`FindShaderInShaderText`, `rd-vanilla/tr_shader.cpp`).

Measured (the method and the table are in the step document, section 16): a sampling profile of the client built by clang, a local game on `mp/ffa3`, ten bots with ten models joining, then `loaddeferred`.

| per model, ten models loaded | as shipped | `r_ext_compress_textures` 0 |
|---|---|---|
| the freeze | 65.3 ms | 27.2 ms |
| `glTexImage2D` (the driver) | 37.4 ms | 2.7 ms |
| images read and decoded (`R_LoadImage`) | 16.7 ms | 14.6 ms |
| shader text searched | about 3 ms | 3.6 ms |
| sounds | 2.8 ms | 1.5 ms |

The map's own loading (`CL_InitCGame`): 2.20 s as shipped, 1.16 s without the compression; `R_CreateImage` is 51 % of the first and 10 % of the second.

## 3. What recent engines do

| engine | source | what it does |
|---|---|---|
| Unity | primary: manual, "Loading texture and mesh data"; Unity's blog, "Understanding the Async Upload Pipeline" (2018) | Texture and mesh data are stored apart from their headers and streamed "using multiple threads, over the course of several frames" through one ring buffer (16 MB by default since 2018.3). The upload to the GPU is "time-sliced" on the render thread: 2 ms per frame by default. The synchronous path loads header and data "mainly on the main thread, in a single frame", which the manual says can cause visible pauses. A texture decoded from a file at run time (`LoadImage`) is forced onto the synchronous path |
| Unreal Engine | primary: Epic's documentation, "Texture Streaming Overview" | The texture streamer "uses an async worker thread to reduce the workload on the game thread, so that only the first and last tasks" run in the game's update; visible mip levels are loaded first, and the memory updated per batch is bounded |
| Unreal Engine | community: two guides | A load through a hard reference is synchronous and stalls a frame by 30 to 200 ms; the remedy is an asynchronous request, issued in menus and transitions |
| Godot | primary: its documentation, "Background loading" | `ResourceLoader.load_threaded_request` loads "in threads in the background"; `load()` "blocks your thread, making your game appear unresponsive while the resource is being loaded" |
| GPU texture formats (KTX2, DDS, Basis Universal) | secondary: a Khronos and GPUOpen sample's page, technical articles | Textures are shipped block-compressed (BC1 to BC7 on desktops) with their mip levels, so that nothing is decoded or compressed at load; a Basis file is transcoded to the GPU's format, which avoids "a PNG decode followed by recompression inside the engine at load" |

What they share: the storage format is the GPU's, the reading and any decoding run off the main thread, the upload is bounded per frame, and an asset still loading is replaced by a lower detail or a stand-in.

## 4. The Quake 3 family

- ioquake3 registers `r_ext_compressed_textures` with the default 0 in both of its renderers (primary: `code/renderergl1/tr_init.c` and `code/renderergl2/tr_init.c`, read on 14 November 2026): the driver compresses nothing unless the player asks. OpenJK's `r_ext_compress_textures` defaults to 1 (primary: `codemp/rd-vanilla/tr_init.cpp`, upstream and this tree).
- ioquake3's second renderer loads `.dds` files in the GPU's format when its setting is on (community: pull requests of a fork, which describe the same setting as gating both the `.dds` lookup and the driver's compression; its source file was not read).
- Quake Live draws every enemy with one model by default, a habit taken from the players' own configurations, for equal visibility and a model that fits the hit box (community: Steam discussions; no statement of the developers was found). Vanilla Quake 3 has `cg_forceModel` only. A side effect is that a joining player brings nothing to load.
- libjpeg-turbo, the JPEG library most projects use today, decodes 1.9 to 4.3 times as fast as libjpeg v8d on x86-64 (primary: its own performance report, for version 1.5.x; later versions are said to be faster on recent processors). This tree builds `lib/jpeg-9a`, in plain C.

## 5. Options

No message changes with any of them. The times are per model, from section 2, on the owner's machine with clang; an estimate is marked.

| option | the freeze per model | memory | work |
|---|---|---|---|
| A: the driver no longer compresses (`r_ext_compress_textures` 0, as ioquake3) | 65 to 27 ms (measured); the map loads in 1.16 s instead of 2.20 s | textures take 4 times the video memory of DXT5 and 8 times that of DXT1 (32 bits per pixel against 8 and 4); the total of a map is not measured | one line, and what to do with a stored value of 1 |
| B: the second search of the shader text removed | about 3.6 ms less (measured share) | none | a few lines, once the hash table is checked to hold every script |
| C: a faster JPEG decoder (libjpeg-turbo) | of the 14.6 ms of reading and decoding, the decoding part divided by 2 to 4 (estimate from the library's figures) | none | a library to vendor and build with its assembly code on Windows and Linux |
| D: images read and decoded on another thread, handed to the GPU a few milliseconds per frame | the main thread keeps the upload: about 5 ms per model without the compression, spread over frames (estimate); the model appears a few frames late | the decoded images waiting, a few megabytes | the largest: a worker thread in the renderer, the file system reached from it, a queue |
| E: textures stored in the GPU's format (`.dds`) | no decoding and no compression (not measured) | as today's compressed textures | the game's assets converted, and the players' own skins, which are JPEG files in pk3s anyone makes |
| F: every enemy drawn with one model | nothing to load at a join | less | a choice of the game, not of the engine |
| G: the load moved to a moment without input (dead, spectating) | unchanged, out of the way | none | a few lines; a stand-in model until then |

A and B correct the two parts of the cause that are plain waste; together they leave about 23 ms per model, two to three ticks, and five new players loaded at once would still stop the client for about 115 ms. Only D removes the stop, as the engines of section 3 do; C shortens it; E removes its two largest parts but needs every asset converted; F and G go around it.

## 6. The owner's choices

None yet (14 November 2026).

## 7. What was not read or verified

- The measure is one machine, one graphics driver and the clang build: the driver's share depends on the driver. MSVC and Linux are not measured.
- The model file itself (`.glm`) does not show in the measure: in a local game the server part of the program had loaded it. Against a remote server the client loads it.
- The video memory of a map with and without the compression is not measured.
- The Khronos wiki page on S3TC refused automated reading (HTTP 403), and the text of NVIDIA's paper on `ARB_texture_compression` could not be extracted: no primary source is quoted here on the speed of a driver's compression, only this page's own measure.
- Unreal's 30 to 200 ms and its remedies come from two guides, not from Epic. Epic's page on texture streaming gives its figures for version 4.15.
- No engineering source was found on how Valorant, Counter-Strike 2 or Overwatch load the players' models; that they are loaded before a round starts is not established here.
- ioquake3's `.dds` loading is described from a fork's pull requests.
- libjpeg-turbo's licence and its compatibility with libjpeg's interface were not read.

## 8. Sources

- Unity manual, "Loading texture and mesh data": https://docs.unity3d.com/2023.1/Documentation/Manual/LoadingTextureandMeshData.html
- Unity blog, "Optimizing loading performance: Understanding the Async Upload Pipeline": https://unity.com/blog/engine-platform/understanding-the-async-upload-pipeline
- Epic, "Texture Streaming Overview": https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-overview-for-unreal-engine
- Godot documentation, "Background loading": https://docs.godotengine.org/en/stable/tutorials/io/background_loading.html
- libjpeg-turbo, "Performance": https://libjpeg-turbo.org/About/Performance
- ioquake3, `code/renderergl1/tr_init.c` and `code/renderergl2/tr_init.c`: https://github.com/ioquake/ioq3
- Vulkan-Samples, texture compression with Basis Universal: https://gpuopen-librariesandsdks.github.io/Vulkan-Samples/samples/performance/texture_compression_basisu/
- techoverflow, compressed texture formats and KTX2 loading: https://techoverflow.net/2026/09/29/compressed-texture-formats-in-vulkan-bc-astc-etc2-and-ktx2-loading/
- bugnet.io, an asynchronous load still stalling the game thread: https://bugnet.io/blog/how-to-fix-unreal-async-asset-load-stalling-game-thread
- khimananda.com, Unreal Engine fundamentals: https://khimananda.com/blog/unreal-engine-fundamentals
- openmohaa fork, pull requests 17 and 61 (the `.dds` lookup and the compression setting): https://github.com/ssshoshi/openmohaa/pull/61
- Steam discussions of Quake Live on the forced enemy model: https://steamcommunity.com/app/282440/discussions/0/490125737474384062/ and https://steamcommunity.com/app/282440/discussions/0/3288067088111000292/
