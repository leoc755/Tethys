/* luna_webp.c - WebP decoding for Luna (uses the bundled libwebp, see third_party/libwebp).
 * GDI+ cannot read WebP, so the C# view hands the bytes to this function. */
#include "luna.h"
#include <stdlib.h>
#include <string.h>
#include "src/webp/decode.h"
#include "src/webp/demux.h"

/* Returns width*height*4 bytes of BGRA (not premultiplied), top row first; free with luna_free.
 * Lossy, lossless and alpha images work. For an animated WebP the first frame is returned.
 * NULL on any failure or when the image is larger than 100 megapixels. */
LUNA_API unsigned char* luna_decode_webp(const unsigned char* data, int len, int* width, int* height)
{
    WebPBitstreamFeatures feat;

    if (width) *width = 0;
    if (height) *height = 0;
    if (!data || len < 12 || !width || !height) return NULL;
    if (WebPGetFeatures(data, (size_t)len, &feat) != VP8_STATUS_OK) return NULL;
    if (feat.width <= 0 || feat.height <= 0 || (long long)feat.width * (long long)feat.height > 100000000LL) return NULL;

    if (!feat.has_animation) {
        int w = 0, h = 0;
        uint8_t* px = WebPDecodeBGRA(data, (size_t)len, &w, &h);
        if (!px) return NULL;
        *width = w;
        *height = h;
        return px;
    }

    {   /* animation: decode the first frame only */
        WebPData wd;
        WebPAnimDecoderOptions opts;
        WebPAnimDecoder* dec;
        WebPAnimInfo info;
        uint8_t* frame = NULL;
        unsigned char* out = NULL;
        int ts = 0;
        size_t bytes;

        wd.bytes = data;
        wd.size = (size_t)len;
        if (!WebPAnimDecoderOptionsInit(&opts)) return NULL;
        opts.color_mode = MODE_BGRA;
        opts.use_threads = 0;
        dec = WebPAnimDecoderNew(&wd, &opts);
        if (!dec) return NULL;
        if (WebPAnimDecoderGetInfo(dec, &info) && WebPAnimDecoderGetNext(dec, &frame, &ts) && frame) {
            bytes = (size_t)info.canvas_width * (size_t)info.canvas_height * 4u;
            out = (unsigned char*)malloc(bytes);
            if (out) {
                memcpy(out, frame, bytes);
                *width = (int)info.canvas_width;
                *height = (int)info.canvas_height;
            }
        }
        WebPAnimDecoderDelete(dec);
        return out;
    }
}
