# Wuffs (vendored release file)

The single-file C release of Wuffs, which decodes every image format the games load: JPEG, PNG and TGA.
It is compiled inside `shared/rd-common/tr_image_codec.cpp`, with the modules that file selects, and nothing else
includes it: the renderers call the functions declared in their `rd-common/tr_common.h` (survey and measurements:
`docs_rewrite/modernization/surveys/image-libraries.md`).

- Source: https://github.com/google/wuffs, file `release/c/wuffs-v0.4.c` at the tag `v0.4.0-alpha.12`
  (commit `7f70dda95c77b10c087c0908790837b5e9c15713`), unchanged.
- Licence: MIT or Apache 2.0, at the user's choice; taken here under the MIT licence (`LICENSE-MIT`), which can be
  combined with the GNU GPL version 2.
- Version: 0.4 is the first version with a JPEG decoder and is not declared stable by its authors; its interface may
  change. The file is pinned: to update it, replace it with the same file of a newer tag, build, and run
  `docs_rewrite/modernization/tools/imagebench/imagebench.sh`.
