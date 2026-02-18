#include "texture.h"

bool Texture::uploaded() const { return image != VK_NULL_HANDLE; }

Texture Texture::solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
							 bool srgb)
{
	Texture tex;
	tex.width = 1;
	tex.height = 1;
	tex.pixels = {r, g, b, a};
	tex.isSrgb = srgb;
	return tex;
}
