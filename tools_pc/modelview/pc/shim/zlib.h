/* modelview: the few zlib entry points port/convert/conv_common.c uses, backed by pc/mv_zlib.cpp (no zlib in the
 * Windows g++ toolchain). Only inflate is implemented; deflate reports an error (the viewer never writes models). */
#ifndef MV_ZLIB_SHIM_H
#define MV_ZLIB_SHIM_H
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef struct z_stream_s {
    const Bytef *next_in; uInt avail_in;
    Bytef *next_out; uInt avail_out;
    void *state;
} z_stream;
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_BUF_ERROR (-5)
#define Z_NO_FLUSH 0
#define Z_FINISH 4
#define Z_DEFLATED 8
#define Z_DEFAULT_STRATEGY 0
int inflateInit2(z_stream *z, int bits);
int inflate(z_stream *z, int flush);
int inflateEnd(z_stream *z);
int deflateInit2(z_stream *z, int level, int method, int bits, int mem, int strategy);
int deflate(z_stream *z, int flush);
int deflateEnd(z_stream *z);
#ifdef __cplusplus
}
#endif
#endif
