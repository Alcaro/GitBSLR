#pragma once
#include "global.h"
#include "array.h"

enum imagefmt {
	ifmt_none,
	
	ifmt_rgb565, // for most of these, 'pixels' is a reinterpreted array of native-endian u16 or u32, highest bit listed first
	ifmt_rgb888, // exception: this one; it's three u8s, red first
	ifmt_xrgb1555, //x bits can be anything and should be ignored
	ifmt_xrgb8888,
	ifmt_0rgb1555, //0 bits are always 0
	ifmt_0rgb8888,
	ifmt_argb1555, //a=1 is opaque; alpha is not premultiplied
	ifmt_argb8888,
};

struct image : nocopy {
	uint32_t width;
	uint32_t height;
	
	imagefmt fmt;
	size_t stride; // Distance, in bytes, between the start positions of each row in 'pixels'. The first row starts at *pixels.
	//If stride isn't equal to width * byteperpix(fmt), the padding contains undefined data. Never access it.
	//Not necessarily writable. It's the caller's job to keep track of that.
	void* pixels;
	
	//Contains nothing useful, it's for internal memory management only.
	autofree<uint8_t> storage;
	
	//Converts the image to the given image format.
	void convert(imagefmt newfmt);
	//Inserts the given image at the given coordinates. If that would place the new image partially outside the target,
	// the excess pixels are ignored.
	void insert(const image& other, int32_t x, int32_t y);
	
	template<typename T> arrayvieww<T> view()
	{
		size_t nbyte = stride*(height-1) + width*byteperpix(fmt);
		return arrayvieww<T>((T*)pixels, nbyte/sizeof(T));
	}
	
	//Result is undefined for ifmt_none and unknown formats.
	static uint8_t byteperpix(imagefmt fmt)
	{
		return 4 - (fmt&1)*2 - (fmt == ifmt_rgb888);
	}
	
	void init_new(uint32_t width, uint32_t height, imagefmt fmt)
	{
		size_t stride = byteperpix(fmt)*width;
		size_t nbytes = stride*height;
		storage = malloc(nbytes);
		init_buf(arrayview<byte>(storage, nbytes), width, height, stride, fmt);
	}
	void init_buf(arrayview<byte> pixels, uint32_t width, uint32_t height, size_t stride, imagefmt fmt)
	{
		this->pixels = (void*)pixels.ptr();
		this->width = width;
		this->height = height;
		this->stride = stride;
		this->fmt = fmt;
	}
	
	bool init_decode(arrayview<byte> data);
	
	//Always emits valid argb8888. If all A bits are 0xFF, reports format xrgb8888.
	bool init_decode_png(arrayview<byte> pngdata);
};
