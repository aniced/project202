#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#ifndef BI_RGB
	#define BI_RGB       0
	#define BI_BITFIELDS 3
#endif

typedef struct {
	uint8_t r, g, b, a;
} Color;

typedef struct {
	int width, height;
	int pitch;
	int bpp;
	void* pixels;
	size_t palette_length;
	Color* palette;
} Bitmap;

void (*error_handler)(const char* message);
#define error(...) do { error_handler(__VA_ARGS__); goto finally; } while(0)

// little-endian by default
uint16_t read16(FILE* f) {
	int a0 = fgetc(f);
	int a1 = fgetc(f);
	return a1 << 8 | a0;
}

uint32_t read32(FILE* f) {
	int a0 = fgetc(f);
	int a1 = fgetc(f);
	int a2 = fgetc(f);
	int a3 = fgetc(f);
	return a3 << 24 | a2 << 16 | a1 << 8 | a0;
}

char* __Bitmap_pixel_loc(Bitmap* bitmap, int y) {
	return ((char*) bitmap->pixels) + bitmap->pitch * y;
}

// DIBs and Their Use
// https://msdn.microsoft.com/en-us/library/ms969901.aspx

void Bitmap_new(Bitmap* bitmap, const char* filename) {
	if (!filename) error("filename is null");
	FILE* f = fopen(filename, "rb");
	if (!f) error("cannot open the file");

	#define CE do { \
		if (feof(f)) error("unexpected EOF"); \
		if (ferror(f)) error("error in reading the file"); \
	} while (false)

	// BITMAPFILEHEADER (12)
	if (fgetc(f) != 'B') error("bfType must be 'BM'");
	if (fgetc(f) != 'M') error("bfType must be 'BM'");
	uint32_t bfSize = read32(f); CE;
	printf("bfSize = %" PRIu32 "\n", bfSize);
	uint16_t bfReserved1 = read16(f); CE;
	if (bfReserved1 != 0) error("bfReserved1 must be set to 0");
	uint16_t bfReserved2 = read16(f); CE;
	if (bfReserved2 != 0) error("bfReserved2 must be set to 0");
	uint32_t bfOffBits = read32(f); CE;

	// BITMAPINFOHEADER (40)
	uint32_t biSize = read32(f); CE;
	if (biSize == 12) error("BITMAPCOREHEADER is not supported");
	if (biSize == 64) error("BITMAPCOREHEADER2 is not supported");
	int32_t biWidth = read32(f); CE;
	bitmap->width = biWidth;
	int32_t biHeight = read32(f); CE;
	bitmap->height = biHeight;
	int y_dir = -1;
	if (biHeight < 0) {
		y_dir = 1;
		biHeight = -biHeight;
	}
	uint16_t biPlanes = read16(f); CE;
	if (biPlanes != 1) error("biPlanes should always be 1");
	uint16_t biBitCount = read16(f); CE;
	if (biBitCount != 1 && biBitCount != 4 && biBitCount != 8 && biBitCount != 24) error("biBitCount is invalid");
	bitmap->bpp = biBitCount;
	uint32_t biCompression = read32(f); CE;
	if (biCompression != BI_RGB && biCompression != BI_BITFIELDS) error("compression method not supported");
	uint32_t biSizeImage = read32(f); CE;
	int32_t biXPelsPerMeter = read32(f); CE;
	int32_t biYPelsPerMeter = read32(f); CE;
	uint32_t biClrUsed = read32(f); CE;
	uint32_t biClrImportant = read32(f); CE;

	int color_count = biClrUsed;
	if (biClrUsed == 0 && biBitCount < 24) color_count = 1 << biBitCount;
	bitmap->palette_length = color_count;

	bool have_mask_rgb = false;
	uint32_t mask_r = 0;
	uint32_t mask_g = 0;
	uint32_t mask_b = 0;
	bool have_mask_a = false;
	uint32_t mask_a = 0;
	if (biSize >= 40 && biCompression == BI_BITFIELDS) {
		have_mask_rgb = true;
		mask_r = read32(f); CE;
		mask_g = read32(f); CE;
		mask_b = read32(f); CE;
		if (biSize >= 56) { // v3
			have_mask_a = true;
			mask_a = read32(f); CE;
		}
	} else {
		if (biSize >= 52) for (int i = 0; i < 12; i++) {
			fgetc(f); CE;
		}
		if (biSize >= 56) for (int i = 0; i < 4; i++) {
			fgetc(f); CE;
		}
	}

	if (color_count) {
		Color* colors = malloc(sizeof(Color) * color_count);
		if (!colors) error("cannot allocate the palette");
		for (int i = 0; i < color_count; i++) {
			colors[i].b = fgetc(f); CE;
			colors[i].g = fgetc(f); CE;
			colors[i].r = fgetc(f); CE;
			colors[i].a = 255; fgetc(f); CE;
		}
		bitmap->palette = colors;
	}

	// mind the gap and jump
	fseek(f, bfOffBits, SEEK_SET); CE;

	bitmap->pitch = (bitmap->bpp * bitmap->width + 31) >> 5 << 2;
	bitmap->pixels = malloc(bitmap->pitch * bitmap->height);
	printf("Pitch = %d\n", bitmap->pitch);
	for (int y = (y_dir > 0 ? 0 : bitmap->height - 1); y >= 0 && y < bitmap->height; y += y_dir) {
		fread(__Bitmap_pixel_loc(bitmap, y), 1, bitmap->pitch, f); CE;
	}
finally:
	#undef CE
	fclose(f);
	return;
}

void Bitmap_dispose(Bitmap* bitmap) {
	free(bitmap->pixels);
	if (bitmap->palette) free(bitmap->palette);
}

Color Bitmap_get_pixel(Bitmap* bitmap, int x, int y) {
	Color r;
	uint8_t* start = (uint8_t*) __Bitmap_pixel_loc(bitmap, y);
	switch (bitmap->bpp) {
	case 1:
		return bitmap->palette[(start[x >> 3] >> (~x & 7)) & 1];
	case 4:
		return bitmap->palette[(start[x >> 1] >> (x % 2 ? 0 : 4)) & 15];
	case 8:
		return bitmap->palette[start[x]];
	case 24:
		start += 3 * x;
		r.r = start[2];
		r.g = start[1];
		r.b = start[0];
		r.a = 255;
		break;
	default:
		error("troublesome bits/pixel");
	}
finally:
	return r;
}

void dummyerror(const char* s) {
	puts(s);
	exit();
}

int main(int argc, char** argv) {
	error_handler = dummyerror;
	Bitmap b;
	Bitmap_new(&b, "sample3.bmp");
	Bitmap_dispose(&b);
	return 0;
}
