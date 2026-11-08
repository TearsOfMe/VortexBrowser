/** va_amigaos.h
 *
 * AmigaOS specific API.
 *
 * @author Hans de Ruiter (at Kea Sigma Delta Limited)
 *
 * Copyright (C) 2019 by A-EON Technology Limited, all rights reserved
 */

#pragma once

#include <va/va.h>


/** Drawable targets for vaPutSurface().
 */
typedef enum VADrawableType_e {
	VADT_RastPort, // Draw to a rastport
	VADT_BitMap, // Draw to a system bitmap
	VADT_W3DNTexture, // Draw to a Warp3D Nova texture
	VADT_W2DSurface, // Draw to a Warp2D surface
} VADrawableType;


/** \brief graphics.library bitmap memory type.
 * Can be used with:
 * - VaCreateSurfaces() - to use an externally allocated bitmap for use when 
 *   decoding/encoding
 * - VaAcquireBufferHandle() - to extract the bitmap pointer for a surface that
 *   uses a bitmap allocated with this flag. The VABufferInfo structure's handle
 *   field will point to the struct BitMap.
 */
#define VA_SURFACE_ATTRIB_MEM_TYPE_BITMAP		0x01000000

/** \brief 2D driver's render manager memory type (e.g., a RadeonHD_RM.resource buffer
 * Can be used with:
 * - VaCreateSurfaces() - to use an externally allocated buffer (e.g., allocated
 *   via Warp3D Nova. This must be accompanied with an VARMSurfaceDescriptor containing
 *   both the layout, and a pointer to the underlying buffer handle
 * - VaExportSurfaceHandle() - to extract the buffer handle. The descriptor parameter should
 *   point to an VARMSurfaceDescriptor which will be filled in with the RM buffer and
 *   layout information. This can be used with either an external RM buffer (allocated
 *   via vaCreateSurfaces(), or a buffer that was allocated internally by the
 *   driver.
 */
#define VA_SURFACE_ATTRIB_MEM_TYPE_RM			0x02000000

typedef struct VARMPlane_s {
	/** \brief this plane's width in pixels. */
	uint32_t width;
	/** \brief this plane's height in pixels */
	uint32_t height;
	/** \brief bytes per pixel for each plane in bytes */
	uint32_t bpp;
	/** \brief pitch for each plane in bytes */
	uint32_t pitches;
	/** \brief offset for each plane in bytes */
	uint32_t offsets;
	/** \brief the number of layers for each plane (e.g., interlaced images may have 2 layers). 
	  */
	uint32_t layer_count;
	/** \brief the number of bytes for each layer. */
	uint32_t layer_size;
	/** \brief the tiling mode (driver-dependent) */
	uint64_t tiling_mode;
} VARMPlane;

typedef struct VARMSurfaceDescriptor_s {
	/** Pointer to the buffer handle. The type depends on the render manager 
	 * (e.g., a RadeonHD_RM.resource buffer)
	 */
	void *bufferHandle;
	
	/** \brief pixel format in fourcc (e.g., VA_FOURCC_UYVY) */
	uint32_t pixel_format;
	/** \brief width in pixels. */
	uint32_t width;
	/** \brief height in pixels. */
	uint32_t height;
	/** \brief total size of the buffer in bytes. */
	uint64_t data_size;
	/** \brief number of planes for planar layout.
	 * NOTE: Some formats have two planes (e.g., NV12 has a luminance and a chroma
	 * plane, and the chroma plane has 2 elements per pixel)
	 */
	uint32_t num_planes;
	VARMPlane planes[6];
	/** \brief flags. See "Surface external buffer descriptor flags". */
	uint32_t flags;
	/** \brief reserved for passing private data */
	void *private_data;
} VARMSurfaceDescriptor;


/** NOTE: The AmigaOS-specific functions can be found in "interfaces/VA.h.
 * Read the autodocs for more.
 */

VADisplay vaGetDisplay(int boardNum);