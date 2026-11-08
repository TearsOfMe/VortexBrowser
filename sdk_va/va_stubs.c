/*
 * va_stubs.c - C wrapper stubs for va.library interface methods on AmigaOS 4
 */

#include <exec/types.h>
#include <exec/exec.h>
#include <interfaces/VA.h>
#include <proto/exec.h>
#include <proto/VA.h>
#include <va/va.h>
#include <va/va_amigaos.h>

extern struct VAIFace *IVA;

const char* vaErrorStr(VAStatus error_status) {
    return IVA ? IVA->vaErrorStr(error_status) : "VA: uninitialized";
}

VAMessageCallback vaSetErrorCallback(VADisplay dpy, VAMessageCallback callback, void * user_context) {
    return IVA ? IVA->vaSetErrorCallback(dpy, callback, user_context) : NULL;
}

VAMessageCallback vaSetInfoCallback(VADisplay dpy, VAMessageCallback callback, void * user_context) {
    return IVA ? IVA->vaSetInfoCallback(dpy, callback, user_context) : NULL;
}

int vaDisplayIsValid(VADisplay dpy) {
    return IVA ? IVA->vaDisplayIsValid(dpy) : 0;
}

VAStatus vaSetDriverName(VADisplay dpy, char * driver_name) {
    return IVA ? IVA->vaSetDriverName(dpy, driver_name) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaInitialize(VADisplay dpy, int * major_version, int * minor_revision) {
    return IVA ? IVA->vaInitialize(dpy, major_version, minor_revision) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaTerminate(VADisplay dpy) {
    return IVA ? IVA->vaTerminate(dpy) : VA_STATUS_ERROR_UNKNOWN;
}

const char* vaQueryVendorString(VADisplay dpy) {
    return IVA ? IVA->vaQueryVendorString(dpy) : "AmigaOS4 VA";
}

VAPrivFunc vaGetLibFunc(VADisplay dpy, const char * func) {
    return IVA ? IVA->vaGetLibFunc(dpy, func) : NULL;
}

int vaMaxNumProfiles(VADisplay dpy) {
    return IVA ? IVA->vaMaxNumProfiles(dpy) : 0;
}

int vaMaxNumEntrypoints(VADisplay dpy) {
    return IVA ? IVA->vaMaxNumEntrypoints(dpy) : 0;
}

int vaMaxNumConfigAttributes(VADisplay dpy) {
    return IVA ? IVA->vaMaxNumConfigAttributes(dpy) : 0;
}

VAStatus vaQueryConfigProfiles(VADisplay dpy, VAProfile * profile_list, int * num_profiles) {
    return IVA ? IVA->vaQueryConfigProfiles(dpy, profile_list, num_profiles) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaQueryConfigEntrypoints(VADisplay dpy, VAProfile profile, VAEntrypoint * entrypoint_list, int * num_entrypoints) {
    return IVA ? IVA->vaQueryConfigEntrypoints(dpy, profile, entrypoint_list, num_entrypoints) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaGetConfigAttributes(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib * attrib_list, int num_attribs) {
    return IVA ? IVA->vaGetConfigAttributes(dpy, profile, entrypoint, attrib_list, num_attribs) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaCreateConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib * attrib_list, int num_attribs, VAConfigID * config_id) {
    return IVA ? IVA->vaCreateConfig(dpy, profile, entrypoint, attrib_list, num_attribs, config_id) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaDestroyConfig(VADisplay dpy, VAConfigID config_id) {
    return IVA ? IVA->vaDestroyConfig(dpy, config_id) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaQueryConfigAttributes(VADisplay dpy, VAConfigID config_id, VAProfile * profile, VAEntrypoint * entrypoint, VAConfigAttrib * attrib_list, int * num_attribs) {
    return IVA ? IVA->vaQueryConfigAttributes(dpy, config_id, profile, entrypoint, attrib_list, num_attribs) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaQuerySurfaceAttributes(VADisplay dpy, VAConfigID config, VASurfaceAttrib * attrib_list, unsigned int * num_attribs) {
    return IVA ? IVA->vaQuerySurfaceAttributes(dpy, config, attrib_list, num_attribs) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaCreateSurfaces(VADisplay dpy, unsigned int format, unsigned int width, unsigned int height, VASurfaceID * surfaces, unsigned int num_surfaces, VASurfaceAttrib * attrib_list, unsigned int num_attribs) {
    return IVA ? IVA->vaCreateSurfaces(dpy, format, width, height, surfaces, num_surfaces, attrib_list, num_attribs) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaDestroySurfaces(VADisplay dpy, VASurfaceID * surfaces, int num_surfaces) {
    return IVA ? IVA->vaDestroySurfaces(dpy, surfaces, num_surfaces) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaCreateContext(VADisplay dpy, VAConfigID config_id, int picture_width, int picture_height, int flag, VASurfaceID * render_targets, int num_render_targets, VAContextID * context) {
    return IVA ? IVA->vaCreateContext(dpy, config_id, picture_width, picture_height, flag, render_targets, num_render_targets, context) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaDestroyContext(VADisplay dpy, VAContextID context) {
    return IVA ? IVA->vaDestroyContext(dpy, context) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaCreateBuffer(VADisplay dpy, VAContextID context, VABufferType type, unsigned int size, unsigned int num_elements, void * data, VABufferID * buf_id) {
    return IVA ? IVA->vaCreateBuffer(dpy, context, type, size, num_elements, data, buf_id) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaCreateBuffer2(VADisplay dpy, VAContextID context, VABufferType type, unsigned int width, unsigned int height, unsigned int * unit_size, unsigned int * pitch, VABufferID * buf_id) {
    return IVA ? IVA->vaCreateBuffer2(dpy, context, type, width, height, unit_size, pitch, buf_id) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaBufferSetNumElements(VADisplay dpy, VABufferID buf_id, unsigned int num_elements) {
    return IVA ? IVA->vaBufferSetNumElements(dpy, buf_id, num_elements) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaMapBuffer(VADisplay dpy, VABufferID buf_id, void ** pbuf) {
    return IVA ? IVA->vaMapBuffer(dpy, buf_id, pbuf) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaUnmapBuffer(VADisplay dpy, VABufferID buf_id) {
    return IVA ? IVA->vaUnmapBuffer(dpy, buf_id) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaDestroyBuffer(VADisplay dpy, VABufferID buf_id) {
    return IVA ? IVA->vaDestroyBuffer(dpy, buf_id) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaAcquireBufferHandle(VADisplay dpy, VABufferID buf_id, VABufferInfo * buf_info) {
    return IVA ? IVA->vaAcquireBufferHandle(dpy, buf_id, buf_info) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaReleaseBufferHandle(VADisplay dpy, VABufferID buf_id) {
    return IVA ? IVA->vaReleaseBufferHandle(dpy, buf_id) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaExportSurfaceHandle(VADisplay dpy, VASurfaceID surface_id, uint32_t mem_type, uint32_t flags, void * descriptor) {
    return IVA ? IVA->vaExportSurfaceHandle(dpy, surface_id, mem_type, flags, descriptor) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaBeginPicture(VADisplay dpy, VAContextID context, VASurfaceID render_target) {
    return IVA ? IVA->vaBeginPicture(dpy, context, render_target) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaRenderPicture(VADisplay dpy, VAContextID context, VABufferID * buffers, int num_buffers) {
    return IVA ? IVA->vaRenderPicture(dpy, context, buffers, num_buffers) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaEndPicture(VADisplay dpy, VAContextID context) {
    return IVA ? IVA->vaEndPicture(dpy, context) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaSyncSurface(VADisplay dpy, VASurfaceID render_target) {
    return IVA ? IVA->vaSyncSurface(dpy, render_target) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaQuerySurfaceStatus(VADisplay dpy, VASurfaceID render_target, VASurfaceStatus * status) {
    return IVA ? IVA->vaQuerySurfaceStatus(dpy, render_target, status) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaQuerySurfaceError(VADisplay dpy, VASurfaceID surface, VAStatus error_status, void ** error_info) {
    return IVA ? IVA->vaQuerySurfaceError(dpy, surface, error_status, error_info) : VA_STATUS_ERROR_UNKNOWN;
}

int vaMaxNumImageFormats(VADisplay dpy) {
    return IVA ? IVA->vaMaxNumImageFormats(dpy) : 0;
}

VAStatus vaQueryImageFormats(VADisplay dpy, VAImageFormat * format_list, int * num_formats) {
    return IVA ? IVA->vaQueryImageFormats(dpy, format_list, num_formats) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaCreateImage(VADisplay dpy, VAImageFormat * format, int width, int height, VAImage * image) {
    return IVA ? IVA->vaCreateImage(dpy, format, width, height, image) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaDestroyImage(VADisplay dpy, VAImageID image) {
    return IVA ? IVA->vaDestroyImage(dpy, image) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaSetImagePalette(VADisplay dpy, VAImageID image, unsigned char * palette) {
    return IVA ? IVA->vaSetImagePalette(dpy, image, palette) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaGetImage(VADisplay dpy, VASurfaceID surface, int x, int y, unsigned int width, unsigned int height, VAImageID image) {
    return IVA ? IVA->vaGetImage(dpy, surface, x, y, width, height, image) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaPutImage(VADisplay dpy, VASurfaceID surface, VAImageID image, int src_x, int src_y, unsigned int src_width, unsigned int src_height, int dest_x, int dest_y, unsigned int dest_width, unsigned int dest_height) {
    return IVA ? IVA->vaPutImage(dpy, surface, image, src_x, src_y, src_width, src_height, dest_x, dest_y, dest_width, dest_height) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaDeriveImage(VADisplay dpy, VASurfaceID surface, VAImage * image) {
    return IVA ? IVA->vaDeriveImage(dpy, surface, image) : VA_STATUS_ERROR_UNKNOWN;
}

int vaMaxNumSubpictureFormats(VADisplay dpy) {
    return IVA ? IVA->vaMaxNumSubpictureFormats(dpy) : 0;
}

VAStatus vaQuerySubpictureFormats(VADisplay dpy, VAImageFormat * format_list, unsigned int * flags, unsigned int * num_formats) {
    return IVA ? IVA->vaQuerySubpictureFormats(dpy, format_list, flags, num_formats) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaCreateSubpicture(VADisplay dpy, VAImageID image, VASubpictureID * subpicture) {
    return IVA ? IVA->vaCreateSubpicture(dpy, image, subpicture) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaDestroySubpicture(VADisplay dpy, VASubpictureID subpicture) {
    return IVA ? IVA->vaDestroySubpicture(dpy, subpicture) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaSetSubpictureImage(VADisplay dpy, VASubpictureID subpicture, VAImageID image) {
    return IVA ? IVA->vaSetSubpictureImage(dpy, subpicture, image) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaSetSubpictureChromakey(VADisplay dpy, VASubpictureID subpicture, unsigned int chromakey_min, unsigned int chromakey_max, unsigned int chromakey_mask) {
    return IVA ? IVA->vaSetSubpictureChromakey(dpy, subpicture, chromakey_min, chromakey_max, chromakey_mask) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaSetSubpictureGlobalAlpha(VADisplay dpy, VASubpictureID subpicture, float global_alpha) {
    return IVA ? IVA->vaSetSubpictureGlobalAlpha(dpy, subpicture, global_alpha) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaAssociateSubpicture(VADisplay dpy, VASubpictureID subpicture, VASurfaceID * target_surfaces, int num_surfaces, short src_x, short src_y, unsigned short src_width, unsigned short src_height, short dest_x, short dest_y, unsigned short dest_width, unsigned short dest_height, unsigned int flags) {
    return IVA ? IVA->vaAssociateSubpicture(dpy, subpicture, target_surfaces, num_surfaces, src_x, src_y, src_width, src_height, dest_x, dest_y, dest_width, dest_height, flags) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaDeassociateSubpicture(VADisplay dpy, VASubpictureID subpicture, VASurfaceID * target_surfaces, int num_surfaces) {
    return IVA ? IVA->vaDeassociateSubpicture(dpy, subpicture, target_surfaces, num_surfaces) : VA_STATUS_ERROR_UNKNOWN;
}

int vaMaxNumDisplayAttributes(VADisplay dpy) {
    return IVA ? IVA->vaMaxNumDisplayAttributes(dpy) : 0;
}

VAStatus vaQueryDisplayAttributes(VADisplay dpy, VADisplayAttribute * attr_list, int * num_attributes) {
    return IVA ? IVA->vaQueryDisplayAttributes(dpy, attr_list, num_attributes) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaGetDisplayAttributes(VADisplay dpy, VADisplayAttribute * attr_list, int num_attributes) {
    return IVA ? IVA->vaGetDisplayAttributes(dpy, attr_list, (unsigned int)num_attributes) : VA_STATUS_ERROR_UNKNOWN;
}

VAStatus vaSetDisplayAttributes(VADisplay dpy, VADisplayAttribute * attr_list, int num_attributes) {
    return IVA ? IVA->vaSetDisplayAttributes(dpy, attr_list, (unsigned int)num_attributes) : VA_STATUS_ERROR_UNKNOWN;
}

const char * vaProfileStr(VAProfile profile) {
    return IVA ? IVA->vaProfileStr(profile) : "Unknown profile";
}

const char * vaEntrypointStr(VAEntrypoint entrypoint) {
    return IVA ? IVA->vaEntrypointStr(entrypoint) : "Unknown entrypoint";
}

const char * vaConfigAttribTypeStr(VAConfigAttribType configAttribType) {
    return IVA ? IVA->vaConfigAttribTypeStr(configAttribType) : "Unknown attrib";
}

const char * vaBufferTypeStr(VABufferType bufferType) {
    return IVA ? IVA->vaBufferTypeStr(bufferType) : "Unknown buffer";
}

VADisplay vaGetDisplay(int boardNum) {
    return IVA ? IVA->vaGetDisplay(boardNum) : NULL;
}

VADisplay vaGetDisplayForModeID(ULONG modeID) {
    return IVA ? IVA->vaGetDisplayForModeID(modeID) : NULL;
}

VADisplay vaGetDisplayForScreen(struct Screen * screen) {
    return IVA ? IVA->vaGetDisplayForScreen(screen) : NULL;
}

VAStatus vaPutSurface(VADisplay dpy, VASurfaceID surface, VADrawableType drawableType, void * drawable, short srcx, short srcy, unsigned short srcw, unsigned short srch, short destx, short desty, unsigned short destw, unsigned short desth, unsigned int flags) {
    return IVA ? IVA->vaPutSurface(dpy, surface, drawableType, drawable, srcx, srcy, srcw, srch, destx, desty, destw, desth, flags) : VA_STATUS_ERROR_UNKNOWN;
}
