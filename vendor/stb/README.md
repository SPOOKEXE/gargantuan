# stb

[stb_image](https://github.com/nothings/stb) v2.30, public domain, vendored as a
single header because it is one file and has no build of its own.

Used by `EditableImage:Load` to decode PNG, JPEG and the other formats stb
supports. `src/render/ImageDecoder.cpp` is the one translation unit that defines
`STB_IMAGE_IMPLEMENTATION`.
