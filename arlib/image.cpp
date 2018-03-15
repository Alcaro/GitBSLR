#include "image.h"

//image::pixel image::pixel::unpack(imagefmt fmt, uint32_t data)
//{
//
//}

//uint32_t image::pixel::pack(imagefmt fmt, pixel px)
//{
//
//}

//size_t image::pixel::bpp(imageformat fmt)
//{
//	static const uint8_t table[]={0, 2, 3, 2, 4, 2, 4};
//	return table[fmt];
//}

//uint32_t image::get_packed(uint32_t x, uint32_t y)
//{
//	
//}

//void image::set_packed(uint32_t x, uint32_t y, uint32_t px)
//{
//	
//}

//pixel image::get(uint32_t x, uint32_t y)
//{
//	
//}

//void image::set(uint32_t x, uint32_t y, pixel px)
//{
//	
//}

//void image::convert(imagefmt newfmt)
//{
//	
//}

bool image::init_decode(arrayview<byte> data)
{
	this->fmt = ifmt_none;
	return
		init_decode_png(data) ||
		false;
}


#include "test.h"

test("image byte per pixel", "", "imagebase")
{
	assert_eq(image::byteperpix(ifmt_rgb565), 2);
	assert_eq(image::byteperpix(ifmt_rgb888), 3);
	assert_eq(image::byteperpix(ifmt_xrgb1555), 2);
	assert_eq(image::byteperpix(ifmt_xrgb8888), 4);
	assert_eq(image::byteperpix(ifmt_0rgb1555), 2);
	assert_eq(image::byteperpix(ifmt_0rgb8888), 4);
	assert_eq(image::byteperpix(ifmt_argb1555), 2);
	assert_eq(image::byteperpix(ifmt_argb8888), 4);
}
