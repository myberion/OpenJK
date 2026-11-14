# stb_image_write (vendored header)

The single header that writes the images the games save: JPEG and PNG screenshots, the JPEG frames of the video
capture, the screenshot of a saved game. It is compiled inside `shared/rd-common/tr_image_codec.cpp` and nothing else
includes it (survey:
`docs_rewrite/modernization/surveys/image-libraries.md`). The images the game loads are decoded by Wuffs
(`lib/wuffs`), which writes none.

- Source: https://github.com/nothings/stb, file `stb_image_write.h`, version 1.18, at the commit
  `9f755b604db3492df2cbeb39bf84b81e3e3932e3` (12 November 2026), unchanged.
- Licence: MIT or public domain, at the user's choice (the end of the header).
