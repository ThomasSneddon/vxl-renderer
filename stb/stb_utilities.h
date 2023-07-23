#ifndef _STB_UTILITIES_H_
#define _STB_UTILITIES_H_

#ifdef STB_IMAGE_UTILITIES_IMPLEMENTATION
void stbi_set_png_compression_level(int level)
{
	stbi_write_png_compression_level = level;
}
#else

#ifdef __cplusplus
extern "C" {
#endif
extern void stbi_set_png_compression_level(int level);
#ifdef __cplusplus
};
#endif
#endif

#endif