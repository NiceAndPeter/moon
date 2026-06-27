/*
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "moon.h"

#include "mmem.h"


inline constexpr int EOZ = -1;  // end of stream

typedef class Zio ZIO;

// Forward declaration for inline function
MOONI_FUNC int moonZ_fill (ZIO *z);


class Mbuffer {
public:
  char *buffer;
  size_t n;
  size_t buffsize;

  // Default constructor
  Mbuffer() noexcept : buffer(nullptr), n(0), buffsize(0) {}
};

inline void moonZ_initbuffer([[maybe_unused]] moon_State *L, Mbuffer *buff) {
    new (buff) Mbuffer();
}

inline char* moonZ_buffer(Mbuffer *buff) {
    return buff->buffer;
}

inline size_t moonZ_sizebuffer(Mbuffer *buff) {
    return buff->buffsize;
}

inline size_t moonZ_bufflen(Mbuffer *buff) {
    return buff->n;
}

inline void moonZ_buffremove(Mbuffer *buff, int i) {
    buff->n -= cast_sizet(i);
}

inline void moonZ_resetbuffer(Mbuffer *buff) {
    buff->n = 0;
}


inline void moonZ_resizebuffer(moon_State *L, Mbuffer *buff, size_t size) {
    buff->buffer = moonM_reallocvchar(L, buff->buffer, buff->buffsize, size);
    buff->buffsize = size;
}

inline void moonZ_freebuffer(moon_State *L, Mbuffer *buff) {
    moonZ_resizebuffer(L, buff, 0);
}


MOONI_FUNC void moonZ_init (moon_State *L, ZIO *z, moon_Reader reader,
                                        void *data);
MOONI_FUNC size_t moonZ_read (ZIO* z, void *b, size_t n);  // read next n bytes

MOONI_FUNC const void *moonZ_getaddr (ZIO* z, size_t n);


// --------- Private Part ------------------

class Zio {
public:
  size_t n;  // bytes still unread
  const char *p;  // current position in buffer
  moon_Reader reader;  // reader function
  void *data;  // additional data
  moon_State *L;  // Lua state (for reader)

  // Constructor
  Zio(moon_State *L_arg, moon_Reader reader_arg, void *data_arg) noexcept
    : n(0), p(nullptr), reader(reader_arg), data(data_arg), L(L_arg) {}
};

inline int zgetc(ZIO *z) {
    return ((z->n--) > 0) ? cast_uchar(*(z->p++)) : moonZ_fill(z);
}

#endif
