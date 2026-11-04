#ifndef VORTEXBROWSER_RENDERER_ALTIVEC_H
#define VORTEXBROWSER_RENDERER_ALTIVEC_H

#include <stddef.h>
#include <stdint.h>
#include <exec/types.h>

void altivec_fast_memcpy(void * restrict dst, const void * restrict src, size_t n);
void altivec_convert_rgb565_to_argb32(const uint16_t * restrict src16, uint32_t * restrict dst32, uint16_t w);
void altivec_convert_argb32_to_rgb565(const uint32_t * restrict src32, uint16_t * restrict dst16, uint16_t w);

#endif /* VORTEXBROWSER_RENDERER_ALTIVEC_H */
