#include "image.h"
#include "bytestream.h"

//TODO: use gdi+ or wic or whatever on windows, to save some kilobytes
//https://msdn.microsoft.com/en-us/library/windows/desktop/ee719875(v=vs.85).aspx
//
//https://msdn.microsoft.com/en-us/library/windows/desktop/ee690179(v=vs.85).aspx says
// "CopyPixels is one of the two main image processing routines (the other being Lock) triggering the actual processing."
//so despite the name, it won't create pointless copies in RAM
//
//WIC is present on XP SP3 and Vista, aka WIC is present

//using GdkPixbuf for the same purpose is pointless, no need to care about filesize on linux where
// everyone compiles manually and half of them include debug symbols

//other possibilities:
//https://msdn.microsoft.com/en-us/library/ee719902(v=VS.85).aspx
//https://stackoverflow.com/questions/39312201/how-to-use-gdi-library-to-decode-a-jpeg-in-memory
//https://stackoverflow.com/questions/1905476/is-there-any-way-to-draw-a-png-image-on-window-without-using-mfc

#define MINIZ_HEADER_FILE_ONLY
#include "deps/miniz.c"

//static void tinfl_deinit(tinfl_decompressor* r) {}
//
//#else
//
//# include <zlib.h>
//typedef tinfl_decompressor z_stream;
//typedef tinfl_status int;
//
//static uint32_t mz_crc32(uint32_t crc, const uint8_t* buf, size_t len) { return crc32(crc, buf, len); }
//
//enum {
//	TINFL_FLAG_PARSE_ZLIB_HEADER = 1,
//	TINFL_FLAG_HAS_MORE_INPUT = 2,
//	TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4,
//	TINFL_FLAG_COMPUTE_ADLER32 = 8,
//	
//	TINFL_STATUS_DONE  = Z_STREAM_END
//};
//
//static void tinfl_init(tinfl_decompressor* r)
//{
//	memset(r, 0, sizeof(*r));
//	inflateInit(r);
//}
//
//static tinfl_status tinfl_decompress(tinfl_decompressor* r, const uint8_t * pIn_buf_next, size_t* pIn_buf_size,
//                                     uint8_t * pOut_buf_start, uint8_t * pOut_buf_next, size_t* pOut_buf_size,
//                                     uint32_t decomp_flags)
//{
//	r->next_in = (Bytef*)pIn_buf_next;
//	r->avail_in = *pIn_buf_size;
//	r->next_out = pOut_buf_next;
//	r->avail_out = *pOut_buf_size;
//	return inflate(r, (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) ? Z_NO_FLUSH : Z_SYNC_FLUSH);
//}
//
//static void tinfl_deinit(tinfl_decompressor* r)
//{
//	inflateEnd(r);
//}

//used for color 3, and color 0 bpp<8
template<int color_type, int bpp_in>
static bool unpack_pixels_plte(uint32_t width, uint32_t height, uint32_t* pixels, uint8_t* source, size_t sourcestride,
                               const uint32_t* palette, uint32_t palettelen);

//used for colors 2, 4 and 6, and color 0 bpp=8
template<int color_type, int bpp_in>
static void unpack_pixels(uint32_t width, uint32_t height, uint32_t* pixels, uint8_t* source, size_t sourcestride);

bool image::init_decode_png(arrayview<byte> pngdata)
{
	struct pngchunk : public bytestream {
		uint32_t type;
		uint32_t len;
	};
	class pngreader : public bytestream {
	public:
		pngreader(arrayview<byte> bytes) : bytestream(bytes) {}
		
		uint32_t peektype()
		{
			if (remaining() < 8) return 0;
			return u32bat(tell()+4);
		}
		
		pngchunk chunk_raw()
		{
			pngchunk ret;
			ret.type = 0; // invalid value, looks like an unrecognized critical chunk and will propagate outwards
			
			if (remaining() < 4) return ret;
			ret.len = u32b();
			
			if (ret.len >= 0x80000000 || remaining() < 4 + ret.len + 4) return ret;
			
			uint32_t crc32_actual = mz_crc32(MZ_CRC32_INIT, peekbytes(4+ret.len).ptr(), 4+ret.len);
			
			ret.type = u32b();
			ret.reset(bytes(ret.len));
			
			uint32_t crc32_exp = u32b();
			if (crc32_actual != crc32_exp) ret.type = 0;
			
			return ret;
		}
		
		//Returns the next recognized or critical chunk. (Only tRNS is recognized.)
		pngchunk chunk()
		{
			while (true)
			{
				pngchunk ret = chunk_raw();
				if (ret.type == 0x74524E53) return ret; // tRNS
				if (!(ret.type&0x20000000)) return ret; // critical
			}
		}
	};
	
	
	pngreader reader(pngdata);
	
	if (reader.remaining() < 8 || !reader.signature("\x89PNG\r\n\x1A\n"))
	{
	fail: // putting this up here to avoid 99999 'jump to label foo crosses initialization of bar' errors
		this->storage = NULL; // forcibly deallocate - it leaves this->pixels as a dead pointer, but caller won't use that.
		return false;
	}
	
	pngchunk IHDR = reader.chunk_raw(); // no ancillary chunks allowed before IHDR
	if (IHDR.type != 0x49484452) goto fail; // IHDR must be the first chunk
	if (IHDR.len != 13) goto fail; // the IHDR chunk is always 13 bytes
	
	uint32_t width  = this->width  = IHDR.u32b();
	uint32_t height = this->height = IHDR.u32b();
	
	// Greyscale             - 0
	// Truecolour            - 2
	// Indexed-colour        - 3
	// Greyscale with alpha  - 4
	// Truecolour with alpha - 6
	
	uint8_t bits_per_sample = IHDR.u8();
	uint8_t color_type = IHDR.u8();
	uint8_t comp_meth = IHDR.u8();
	uint8_t filter_meth = IHDR.u8();
	uint8_t interlace_meth = IHDR.u8();
	
	if (width == 0 || height == 0 || width >= 0x80000000 || height >= 0x80000000) goto fail;
	if (bits_per_sample >= 32 || color_type > 6 || comp_meth != 0 || filter_meth != 0 || interlace_meth > 1) goto fail;
	if (bits_per_sample > 8) goto fail; // bpp=16 is allowed by the png standard, but not by this program
	static const uint32_t bpp_allowed[7] = { 0x00010116, 0x00000000, 0x00010100, 0x00000116, 0x00010100, 0x00000000, 0x00010100 };
	if (((bpp_allowed[color_type]>>bits_per_sample)&1) == false) goto fail;
	
	uint32_t palette[256];
	unsigned palettelen = 0;
	
	if (color_type == 3)
	{
		pngchunk PLTE = reader.chunk();
		if (PLTE.type != 0x504C5445) goto fail;
		
		if (PLTE.len == 0 || PLTE.len%3 || PLTE.len/3 > (1u<<bits_per_sample)) goto fail;
		palettelen = PLTE.len/3;
		for (unsigned i=0;i<palettelen;i++)
		{
			palette[i] = 0xFF000000 | PLTE.u24b();
		}
	}
	
	bool has_alpha = (color_type >= 4);
	uint32_t transparent = 0; // if you see this value (after filtering), make it transparent
	                          // for rgb, this value is ARGB, A=0xFF
	
	pngchunk tRNS_IDAT = reader.chunk();
	if (tRNS_IDAT.type == 0x504C5445) // PLTE
	{
		if (color_type == 2 || color_type == 6) // it's allowed on those two types
			tRNS_IDAT = reader.chunk();
		//else fall through and let IEND handler whine
	}
	pngchunk IDAT;
	if (!has_alpha && tRNS_IDAT.type == 0x74524E53) // tRNS
	{
		pngchunk tRNS = tRNS_IDAT;
		has_alpha = true;
		
		if (color_type == 0)
		{
			if (tRNS.len != 2) goto fail;
			transparent = tRNS.u16b();
			if (transparent >= 1u<<bits_per_sample) goto fail;
			if (bits_per_sample == 8) transparent = 0xFF000000 + transparent*0x010101;
		}
		if (color_type == 2)
		{
			if (tRNS.len != 6) goto fail;
			
			uint16_t r = tRNS.u16b();
			uint16_t g = tRNS.u16b();
			uint16_t b = tRNS.u16b();
			
			if ((r|g|b) >= 1<<bits_per_sample) goto fail;
			
			transparent = 0xFF000000 | r<<16 | g<<8 | b;
		}
		if (color_type == 3)
		{
			if (tRNS.len == 0 || tRNS.len > palettelen) goto fail;
			
			for (size_t i=0;i<tRNS.len;i++)
			{
				palette[i] = (palette[i]&0x00FFFFFF) | tRNS.u8()<<24;
			}
		}
		if (color_type == 4) goto fail;
		if (color_type == 6) goto fail;
		
		IDAT = reader.chunk();
	}
	else IDAT = tRNS_IDAT; // could be neither tRNS or IDAT, in which case the while loop won't enter, and the IEND checker will whine
	
	
	this->stride = sizeof(uint32_t) * width;
	
	int samples_per_px = (0x04021301 >> (color_type*4))&15;
	size_t bytes_per_line_raw = (bits_per_sample*samples_per_px*width + 7)/8;
	
	//+4 and +2 to let filters read outside the image, and to ensure there's enough
	// space to decode and convert the image to the desired output format
	//+7*u32 because the decoder overshoots on low bitdepths
	//unpacking (png -> uint32_t) needs one pixel of buffer, though compiler considerations say minimum one scanline
	//unfiltering needs another scanline of buffer, plus another one must exist in front
	//
	//however, there's another another issue - what scanline size?
	// output must be packed, raw is pngpack+1, unfiltering needs at least pngpack+4
	//the correct offsets are
	//scan_out = width * (3 or 4)
	//scan_packed = whatever+4
	//scan_filtered = whatever+1
	//start_out = 0
	//start_packed = end - height*scan_packed - scan_packed
	//start_filtered = end - height*scan_filtered
	
	//deinterleaving probably isn't doable in-place at all
	
	size_t nbytes = max(this->stride, bytes_per_line_raw+4) * (height+2) + sizeof(uint32_t)*7;
	this->storage = malloc(nbytes);
	
	this->pixels = (uint8_t*)this->storage;
	this->fmt = (has_alpha ? ifmt_argb8888 : ifmt_xrgb8888); // xrgb8888 is probably faster than rgb888, and a lot easier
	
	uint8_t* inflate_end = (uint8_t*)this->storage + nbytes;
	uint8_t* inflate_start = inflate_end - (bytes_per_line_raw+1)*height;
	uint8_t* inflate_at = inflate_start;
	
	tinfl_decompressor infl;
	tinfl_init(&infl);
	
	while (IDAT.type == 0x49444154)
	{
		size_t chunklen = IDAT.len;
		size_t inflate_outsize = inflate_end-inflate_at;
		uint32_t flags = TINFL_FLAG_HAS_MORE_INPUT | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | TINFL_FLAG_PARSE_ZLIB_HEADER;
		tinfl_status status = tinfl_decompress(&infl, IDAT.bytes(chunklen).ptr(), &chunklen,
		                                       inflate_start, inflate_at, &inflate_outsize,
		                                       flags);
		if (status < 0) goto fail;
		
		inflate_at += inflate_outsize;
		
		IDAT = reader.chunk_raw(); // all IDAT chunks must be consecutive
	}
	
	size_t zero = 0;
	size_t inflate_outsize = inflate_end-inflate_at;
	tinfl_status status = tinfl_decompress(&infl, NULL, &zero, inflate_start, inflate_at, &inflate_outsize,
	                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | TINFL_FLAG_PARSE_ZLIB_HEADER);
	inflate_at += inflate_outsize;
	if (status != TINFL_STATUS_DONE) goto fail;
	if (inflate_at != inflate_end) goto fail; // too little data (if too much, status is TINFL_STATUS_HAS_MORE_OUTPUT)
	
	pngchunk IEND = IDAT;
	if (IEND.type != 0x49454E44) IEND = reader.chunk(); // ancillary between IDAT and IEND is fine, just discard that
	if (IEND.type != 0x49454E44 || IEND.len != 0) goto fail;
	
	
	if (interlace_meth == 1)
	{
puts("unsupported: Adam7");
goto fail;
	}
	
//for(uint8_t*n=inflate_start;n<inflate_at;n++)printf("%.2X ",*n);
//puts("");
	
	size_t defilter_out_line = 4 + bytes_per_line_raw;
	uint8_t* defilter_start = inflate_end - height*defilter_out_line - defilter_out_line;
	
	memset(defilter_start - defilter_out_line, 0, defilter_out_line); // y=-1 is all zeroes
	
	size_t filter_bpp = (bits_per_sample*samples_per_px + 7)/8;
	size_t filter_width = bytes_per_line_raw;
	for (size_t y=0;y<height;y++)
	{
		uint8_t* defilter_out = defilter_start + y*defilter_out_line;
		*(defilter_out++) = 0; // x=-1 is all zeroes
		*(defilter_out++) = 0;
		*(defilter_out++) = 0;
		*(defilter_out++) = 0;
		
		uint8_t* defilter_in = inflate_start + y*(bytes_per_line_raw+1);
		switch (*(defilter_in++))
		{
		case 0: // None
			memcpy(defilter_out, defilter_in, bytes_per_line_raw);
			break;
		
		case 1: // Sub
			memcpy(defilter_out, defilter_in, bytes_per_line_raw);
			for (size_t x=0;x<filter_width;x++)
				defilter_out[x] += defilter_out[x-filter_bpp];
			break;
		
		case 2: // Up
			memcpy(defilter_out, defilter_in, bytes_per_line_raw);
			for (size_t x=0;x<filter_width;x++)
				defilter_out[x] += defilter_out[x-defilter_out_line];
			break;
		
		case 3: // Average
			memcpy(defilter_out, defilter_in, bytes_per_line_raw);
			
			for (size_t x=0;x<filter_width;x++)
			{
				uint8_t a = defilter_out[x-filter_bpp];
				uint8_t b = defilter_out[x-defilter_out_line];
				defilter_out[x] += (a+b)/2;
			}
			break;
		
		case 4: // Paeth
			memcpy(defilter_out, defilter_in, bytes_per_line_raw);
			
			for (size_t x=0;x<filter_width;x++)
			{
				int a = defilter_out[x-filter_bpp];
				int b = defilter_out[x-defilter_out_line];
				int c = defilter_out[x-defilter_out_line-filter_bpp];
				
				int p = a+b-c;
				int pa = abs(p-a);
				int pb = abs(p-b);
				int pc = abs(p-c);
				
				int prediction;
				if (pa <= pb && pa <= pc) prediction = a;
				else if (pb <= pc) prediction = b;
				else prediction = c;
				
				defilter_out[x] += prediction;
			}
			break;
		
		default:
			goto fail;
		}
	}
	//this argument list is huge, I'm not repeating it 11 times
	
	defilter_start += 4; // skip the x=-1 = 0 padding
#define UNPACK_ARGS width, height, (uint32_t*)(uint8_t*)this->storage, defilter_start, defilter_out_line
	bool ok = true;
	if (color_type == 0 && bits_per_sample < 8)
	{
		//treating gray as a palette makes things a fair bit easier, especially with tRNS handling
		static const uint32_t gray_1bpp[] = { 0xFF000000, 0xFFFFFFFF };
		static const uint32_t gray_2bpp[] = { 0xFF000000, 0xFF555555, 0xFFAAAAAA, 0xFFFFFFFF };
		static const uint32_t gray_4bpp[] = { 0xFF000000, 0xFF111111, 0xFF222222, 0xFF333333,
		                                      0xFF444444, 0xFF555555, 0xFF666666, 0xFF777777,
		                                      0xFF888888, 0xFF999999, 0xFFAAAAAA, 0xFFBBBBBB,
		                                      0xFFCCCCCC, 0xFFDDDDDD, 0xFFEEEEEE, 0xFFFFFFFF };
		
		const uint32_t * gray_local;
		if (bits_per_sample == 1) gray_local = gray_1bpp;
		if (bits_per_sample == 2) gray_local = gray_2bpp;
		if (bits_per_sample == 4) gray_local = gray_4bpp;
		
		uint32_t gray_trns[16];
		
		if (UNLIKELY(has_alpha))
		{
			memcpy(gray_trns, gray_local, sizeof(uint32_t) * 1<<bits_per_sample);
			gray_trns[transparent] = 0;
			gray_local = gray_trns;
		}
		
		if (bits_per_sample == 1) ok = unpack_pixels_plte<0,1>(UNPACK_ARGS, gray_local, 2);
		if (bits_per_sample == 2) ok = unpack_pixels_plte<0,2>(UNPACK_ARGS, gray_local, 4);
		if (bits_per_sample == 4) ok = unpack_pixels_plte<0,4>(UNPACK_ARGS, gray_local, 16);
	}
	
	if (color_type == 0 && bits_per_sample == 8) unpack_pixels<0,8>(UNPACK_ARGS);
	if (color_type == 2 && bits_per_sample == 8) unpack_pixels<2,8>(UNPACK_ARGS);
	if (color_type == 3 && bits_per_sample == 1) ok = unpack_pixels_plte<3,1>(UNPACK_ARGS, palette, palettelen);
	if (color_type == 3 && bits_per_sample == 2) ok = unpack_pixels_plte<3,2>(UNPACK_ARGS, palette, palettelen);
	if (color_type == 3 && bits_per_sample == 4) ok = unpack_pixels_plte<3,4>(UNPACK_ARGS, palette, palettelen);
	if (color_type == 3 && bits_per_sample == 8) ok = unpack_pixels_plte<3,8>(UNPACK_ARGS, palette, palettelen);
	if (color_type == 4 && bits_per_sample == 8) unpack_pixels<4,8>(UNPACK_ARGS);
	if (color_type == 6 && bits_per_sample == 8) unpack_pixels<6,8>(UNPACK_ARGS);
	
	if (!ok) goto fail;
	
	if (transparent >= 0xFF000000)
	{
		uint32_t* pixels = (uint32_t*)(uint8_t*)this->storage;
		for (size_t i=0;i<width*height;i++)
		{
			if (pixels[i] == transparent) pixels[i] = 0;
		}
	}
	
#undef UNPACK_ARGS
	
	return true;
}

//returns false if it uses anything outside the palette
template<int color_type, int bpp_in>
static bool unpack_pixels_plte(uint32_t width, uint32_t height, uint32_t* pixels, uint8_t* source, size_t sourcestride,
                               const uint32_t* palette, uint32_t palettelen)
{
	for (uint32_t y=0;y<height;y++)
	{
		//in this function, all pixels have one pixel per sample
		
		//this will write 7 pixels out of bounds for bpp=1 and size=int*8+1; this will be absorbed by our overallocations
		//TODO: test on 1*1, 1*2, 1*7, 1*8, 1*9, 1*1023, 1*1024 and 1*1025, bpp=1
		for (uint32_t byte=0;byte<((uint64_t)width*bpp_in+7)/8;byte++)
		{
			uint8_t packed = source[byte];
#define WRITE(xs, idx) { if (color_type == 3 && (idx) >= palettelen) return false; pixels[byte*8/bpp_in + (xs)] = palette[(idx)]; }
			if (bpp_in == 1)
			{
				WRITE(0, (packed>>7)&1);
				WRITE(1, (packed>>6)&1);
				WRITE(2, (packed>>5)&1);
				WRITE(3, (packed>>4)&1);
				WRITE(4, (packed>>3)&1);
				WRITE(5, (packed>>2)&1);
				WRITE(6, (packed>>1)&1);
				WRITE(7, (packed>>0)&1);
			}
			if (bpp_in == 2)
			{
				WRITE(0, (packed>>6)&3);
				WRITE(1, (packed>>4)&3);
				WRITE(2, (packed>>2)&3);
				WRITE(3, (packed>>0)&3);
			}
			if (bpp_in == 4)
			{
				WRITE(0, (packed>>4)&15);
				WRITE(1, (packed>>0)&15);
			}
			if (bpp_in == 8)
			{
				WRITE(0, (packed>>0)&255);
			}
#undef WRITE
		}
		
		pixels += width;
		source += sourcestride;
	}
	
	return true;
}

template<int color_type, int bpp_in>
static void unpack_pixels(uint32_t width, uint32_t height, uint32_t* pixels, uint8_t* source, size_t sourcestride)
{
	//bpp_in is ignored, it's always 8
	
	size_t nsamp;
	if (color_type == 0) nsamp = 1; // gray
	if (color_type == 2) nsamp = 3; // rgb
	if (color_type == 4) nsamp = 2; // gray+alpha
	if (color_type == 6) nsamp = 4; // rgba
	
	for (uint32_t y=0;y<height;y++)
	{
		for (uint32_t x=0;x<width;x++)
		{
			uint8_t* sourceat = source + x*nsamp;
			
			if (color_type == 0)
				pixels[x] = 0xFF000000 | sourceat[0]*0x010101;
			if (color_type == 2)
				pixels[x] = 0xFF000000 | sourceat[0]<<16 | sourceat[1]<<8 | sourceat[2]<<0;
			if (color_type == 4)
				pixels[x] = sourceat[1]<<24 | sourceat[0]*0x010101;
			if (color_type == 6)
				pixels[x] = sourceat[3]<<24 | sourceat[0]<<16 | sourceat[1]<<8 | sourceat[2]<<0;
		}
		
		pixels += width;
		source += sourcestride;
	}
}

#include "test.h"

test("PNG", "array,imagebase", "image")
{
	uint8_t png_plus[] = { 
		0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
		0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x01,0x00,0x00,0x00,0x00,0x7E,0x53,0x88,
		0x12,0x00,0x00,0x00,0x0E,0x49,0x44,0x41,0x54,0x78,0x01,0x63,0x58,0xC0,0xC0,0xC0,
		0xB0,0x00,0x00,0x03,0xC6,0x01,0x41,0x13,0xC5,0xF5,0x8B,0x00,0x00,0x00,0x00,0x49,
		0x45,0x4E,0x44,0xAE,0x42,0x60,0x82
	};
	
	image im;
	assert(im.init_decode_png(png_plus));
	assert_eq(im.width, 3);
	assert_eq(im.height, 3);
	
	arrayview<uint32_t> pixels = im.view<uint32_t>();
	size_t stride = im.stride/sizeof(uint32_t);
	assert_eq(pixels[stride*0+0]&0xFFFFFF, 0xFFFFFF);
	assert_eq(pixels[stride*0+1]&0xFFFFFF, 0x000000);
	assert_eq(pixels[stride*0+2]&0xFFFFFF, 0xFFFFFF);
	assert_eq(pixels[stride*1+0]&0xFFFFFF, 0x000000);
	assert_eq(pixels[stride*1+1]&0xFFFFFF, 0x000000);
	assert_eq(pixels[stride*1+2]&0xFFFFFF, 0x000000);
	assert_eq(pixels[stride*2+0]&0xFFFFFF, 0xFFFFFF);
	assert_eq(pixels[stride*2+1]&0xFFFFFF, 0x000000);
	assert_eq(pixels[stride*2+2]&0xFFFFFF, 0xFFFFFF);
}

static const char * pngsuite[] = {
	//this is PngSuite
	//it's a good start, but it leaves a lot of stuff untested:
	//- invalid sizes: 32*0px, 2^31*32px, etc (spec says 2^31-1 is max)
	//- 2^31-1*2^31-1px image that's technically legal but takes several exabytes of RAM to render
	//- 2^31-1*1 image, may provoke a few integer overflows
	//- checksum error in an ancillary chunk
	//- non-square interlaced images, at least 1*n and n*1 for every n<=9, but preferably n*m for all n,m<=9
	//- funky sizes like 1*1024 and 1024*1
	//- PLTE chunk length not divisible by 3
	//- PLTE with 1 color
	//- PLTE with 0 colors
	//- PLTE on grayscale
	//- multiple PLTE
	//- paletted image where some pixels use colors not in the PLTE
	//- overlong PLTE - <=16-color PLTE but 8 bits per pixel
	//- paletted image with too long tRNS
	//- for bit width 16, a tRNS chunk saying RGB 0x0001*3 is transparent, and image contains 1,1,2 that should not be transparent
	//- tRNS on grayscale
	//- tRNS on gray+alpha / RGBA
	//- tRNS with values out of bounds
	//- tRNS with wrong size
	//- tRNS larger than the palette
	//- size-0 tRNS with paletted data
	//- filter type other than 0
	//- filter type 0, but some scanlines have filters > 4
	//- filter != 0 on y=0
	//- test whether the Paeth filter breaks ties properly
	//- IDAT/ordering shenanigans with bit width < 16
	//- actual ordering shenanigans, as opposed to just splitting the IDATs
	//- too much or too little data in IDATs
	//- compressed data trying to read outside the sliding window, if applicable
	//- unexpected EOFs
	//- random crap after IEND
	//finally, the reference images are bad; they're not bulk downloadable, and they're GIF, rather than PNGs without fancy features
	//instead, I ran all of them through 'pngout -c6 -f0 -d8 -s4 -y -force'
	//I also ran the invalid x*.png, and pngout-unsupported *16.png, through 'truncate -s0'
	//all png test suites I could find are just variants of PngSuite
	
	"basn0g01.png", "basn0g02.png", "basn0g04.png", "basn0g08.png", /*"basn0g16.png",*/
	"basn2c08.png", /*"basn2c16.png",*/
	"basn3p01.png", "basn3p02.png", "basn3p04.png", "basn3p08.png",
	"basn4a08.png", /*"basn4a16.png",*/
	"basn6a08.png", /*"basn6a16.png",*/
	
	/*"basi0g01.png",*/ /*"basi0g02.png",*/ /*"basi0g04.png",*/ /*"basi0g08.png",*/ /*"basi0g16.png",*/
	/*"basi2c08.png",*/ /*"basi2c16.png",*/
	/*"basi3p01.png",*/ /*"basi3p02.png",*/ /*"basi3p04.png",*/ /*"basi3p08.png",*/
	/*"basi4a08.png",*/ /*"basi4a16.png",*/
	/*"basi6a08.png",*/ /*"basi6a16.png",*/
	
	/*"s01i3p01.png",*/ "s01n3p01.png", /*"s02i3p01.png",*/ "s02n3p01.png", /*"s03i3p01.png",*/ "s03n3p01.png",
	/*"s04i3p01.png",*/ "s04n3p01.png", /*"s05i3p02.png",*/ "s05n3p02.png", /*"s06i3p02.png",*/ "s06n3p02.png",
	/*"s07i3p02.png",*/ "s07n3p02.png", /*"s08i3p02.png",*/ "s08n3p02.png", /*"s09i3p02.png",*/ "s09n3p02.png",
	/*"s32i3p04.png",*/ "s32n3p04.png", /*"s33i3p04.png",*/ "s33n3p04.png", /*"s34i3p04.png",*/ "s34n3p04.png",
	/*"s35i3p04.png",*/ "s35n3p04.png", /*"s36i3p04.png",*/ "s36n3p04.png", /*"s37i3p04.png",*/ "s37n3p04.png",
	/*"s38i3p04.png",*/ "s38n3p04.png", /*"s39i3p04.png",*/ "s39n3p04.png", /*"s40i3p04.png",*/ "s40n3p04.png",
	
	/*"bgai4a08.png",*/ /*"bgai4a16.png",*/
	"bgan6a08.png", /*"bgan6a16.png",*/
	"bgwn6a08.png", /*"bggn4a16.png",*/
	"bgbn4a08.png", /*"bgyn6a16.png",*/
	
	/*"tbwn0g16.png",*/ "tbwn3p08.png", /*"tbgn2c16.png",*/ "tbgn3p08.png", "tbbn0g04.png", /*"tbbn2c16.png",*/
	"tbbn3p08.png", "tbyn3p08.png", "tp0n0g08.png", "tp0n2c08.png", "tp0n3p08.png", "tp1n3p08.png", "tm3n3p02.png",
	"tbrn2c08.png", // this one isn't listed on the pngsuite homepage
	
	/*"g03n0g16.png",*/ "g03n2c08.png", "g03n3p04.png", /*"g04n0g16.png",*/ "g04n2c08.png", "g04n3p04.png",
	/*"g05n0g16.png",*/ "g05n2c08.png", "g05n3p04.png", /*"g07n0g16.png",*/ "g07n2c08.png", "g07n3p04.png",
	/*"g10n0g16.png",*/ "g10n2c08.png", "g10n3p04.png", /*"g25n0g16.png",*/ "g25n2c08.png", "g25n3p04.png",
	
	// they're f0..f4 on the pngsuite homepage, rather than f00..f04
	"f00n0g08.png", "f00n2c08.png", "f01n0g08.png", "f01n2c08.png",
	"f02n0g08.png", "f02n2c08.png", "f03n0g08.png", "f03n2c08.png",
	"f04n0g08.png", "f04n2c08.png",
	"f99n0g04.png", // this one isn't listed on the pngsuite homepage, probably adaptive
	
	/*"pp0n2c16.png",*/ "pp0n6a08.png", "ps1n0g08.png", /*"ps1n2c16.png",*/ "ps2n0g08.png", /*"ps2n2c16.png",*/
	
	"cs3n3p08.png", "cs5n2c08.png", "cs5n3p08.png", "cs8n2c08.png", "cs8n3p08.png", /*"cs3n2c16.png",*/
	// both 3 and 13 significant bits are 'cs3', but there are only two cs3 so it's one of each. and only one has 16 bits
	
	"cdfn2c08.png", "cdhn2c08.png", "cdsn2c08.png", "cdun2c08.png",
	"ccwn2c08.png", "ccwn3p08.png",
	"ch1n3p04.png", "ch2n3p08.png",
	"cm7n0g04.png", "cm9n0g04.png", "cm0n0g04.png",
	
	"ct0n0g04.png",  "ct1n0g04.png",  "ctzn0g04.png",  "cten0g04.png", "ctfn0g04.png", "ctgn0g04.png", "cthn0g04.png", "ctjn0g04.png",
	"exif2c08.png", 
	
	// all chunk ordering tests are 16bpp, sigh. can't use them
	/*"oi1n0g16.png",*/ /*"oi1n2c16.png",*/ /*"oi2n0g16.png",*/ /*"oi2n2c16.png",*/
	/*"oi4n0g16.png",*/ /*"oi4n2c16.png",*/ /*"oi9n0g16.png",*/ /*"oi9n2c16.png",*/
	
	"z00n2c08.png", "z03n2c08.png", "z06n2c08.png", "z09n2c08.png",
	
	// these are supposed to fail rendering, so they're compared with zero-size files
	// fun fact: an older version of the test contained a 0x0 image
	// fun fact: pngout accepts the ones with bad checksums (xhd, xcs)
	// funner fact: pngout segfaults on the one with missing IDAT (xdt)!
	"xs1n0g01.png", "xs2n0g01.png", "xs4n0g01.png", "xs7n0g01.png", "xcrn0g04.png", "xlfn0g04.png", "xhdn0g08.png",
	"xc1n0g08.png", "xc9n2c08.png", "xd0n2c08.png", "xd3n2c08.png", "xd9n2c08.png", "xdtn0g01.png", "xcsn0g01.png",
	
	"PngSuite.png", // it's included in the zip, why not?
	
	NULL
};

#include "file.h"
test("PngSuite", "array,imagebase,file", "png")
{
	for (int i=42*0;pngsuite[i];i++)
	{
		image im;
		image ref;
		
		bool expect = ref.init_decode_png(file::readall(file::cwd()+"/arlib/test/png/reference/"+pngsuite[i]));
		
		if (expect)
		{
//printf("\r                                        \r(%i) %s... ", i, pngsuite[i]);
			assert(im.init_decode_png(file::readall(file::cwd()+"/arlib/test/png/"+pngsuite[i])));
			assert_eq(im.width, ref.width);
			assert_eq(im.height, ref.height);
			
			arrayview<uint32_t> imp = im.view<uint32_t>();
			arrayview<uint32_t> refp = ref.view<uint32_t>();
			
//for (size_t i=0;i<ref.height*ref.width;i++)
//{
//if(i%ref.width==0)puts("");
//printf("%.8X ",refp[i]);
//}
//puts("\n");
//for (size_t i=0;i<im.height*im.width;i++)
//{
//if(i%im.width==0)puts("");
//printf("%.8X ",imp[i]);
//}
//puts("\n");
			for (size_t i=0;i<ref.height*ref.width;i++)
			{
//printf("%lu,%u,%u\n",i,imp[i],refp[i]);
				if (refp[i]&0xFF000000) assert_eq(imp[i], refp[i]);
				else assert_eq(imp[i]&0xFF000000, 0);
			}
		}
		else
		{
			assert(!im.init_decode_png(file::readall(file::cwd()+"/arlib/test/png/"+pngsuite[i])));
		}
	}
}
