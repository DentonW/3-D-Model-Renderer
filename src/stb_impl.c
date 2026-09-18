/* The single translation unit that compiles stb's header-only libraries:
 * stb_image reads the texture files a model refers to, stb_image_write saves
 * screenshots. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
