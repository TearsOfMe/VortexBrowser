#ifndef VORTEXBROWSE_PROTOCOL_H
#define VORTEXBROWSE_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VortexBrowse Binary Streaming Protocol (Raspberry Pi 4 <-> AmigaOS 4 PPC)
 *
 * - All multi-byte fields are transmitted in Big-Endian (Network Byte Order)
 *   for zero-overhead native processing on AmigaOS 4 (PowerPC).
 * - Structs are 2-byte aligned/packed via #pragma pack(push, 2).
 * - Each packet on the TCP stream starts with a 1-byte uint8_t msg_type,
 *   immediately followed by the corresponding packed header struct and
 *   optional variable-length payload (for MSG_FRAME and MSG_DOWNLOAD_CHUNK).
 * ============================================================================ */

#define VORTEXBROWSE_DEFAULT_PORT   7788
#define VORTEXBROWSE_DEFAULT_WIDTH  1024
#define VORTEXBROWSE_DEFAULT_HEIGHT 768
#define VORTEXBROWSE_DOWNLOAD_CHUNK_SIZE (64 * 1024)

/* Message Types (1 byte on wire preceding each message struct) */
#define MSG_FRAME             0x01  /* Pi -> Amiga: Frame update with LZ4 payload */
#define MSG_INPUT             0x02  /* Amiga -> Pi: Mouse, keyboard, or URL command */
#define MSG_DOWNLOAD_START    0x03  /* Pi -> Amiga: Incoming file download notification */
#define MSG_DOWNLOAD_RESPONSE 0x04  /* Amiga -> Pi: Accept (1) or Reject (0) download */
#define MSG_DOWNLOAD_CHUNK    0x05  /* Pi -> Amiga: 64KB data chunk for active download */
#define MSG_DOWNLOAD_COMPLETE 0x06  /* Pi -> Amiga: Download finished notification */
#define MSG_AUDIO             0x07  /* Pi -> Amiga: Streaming PCM S16BE audio chunk */
#define MSG_NAV_UPDATE        0x08  /* Pi -> Amiga: Current browser URL changed */
#define MSG_CONTEXT_MENU      0x09  /* Pi -> Amiga: Native context menu target info under cursor */
#define MSG_PAGE_PROGRESS     0x0A  /* Pi -> Amiga: Page loading progress (0-100 percent) */
#define MSG_CLIPBOARD         0x0B  /* Bi-directional: Native clipboard text sync */

/* Pixel Formats for MSG_FRAME.format */
#define FMT_ARGB32            1     /* 32-bit Big-Endian ARGB (PIXF_A8R8G8B8, 4 bytes/px) */
#define FMT_RGB565            2     /* 16-bit Big-Endian RGB565 (PIXF_R5G6B5, 2 bytes/px) */
#define FMT_YUV420P           3     /* 12-bit Planar YUV420p (PIXF_YUV420P, Y/U/V planes, 1.5 bytes/px) */

/* Compression Codecs for MSG_FRAME.codec and MSG_AUDIO.codec */
#define CODEC_RAW             0     /* Uncompressed raw payload */
#define CODEC_LZ4             1     /* LZ4 raw block (lz4.block, store_size=False) */
#define CODEC_LZ4_SLICE       2     /* Dirty scanline band: [uint16_t y_offset, uint16_t slice_h] + LZ4 block */
#define CODEC_JPEG            3     /* Baseline Motion-JPEG */
#define CODEC_LZ4_RECT        4     /* 2D Bounding Box sub-rect: [MsgRectHdr x,y,w,h] + LZ4 block */

/* Input Event Types for MSG_INPUT.event_type */
#define INPUT_CLICK           1     /* Mouse click at (x, y); key_code: 1=Left, 2=Middle, 3=Right */
#define INPUT_MOUSE_MOVE      2     /* Mouse move to (x, y) */
#define INPUT_WHEEL           3     /* Mouse wheel scroll (delta_y) */
#define INPUT_KEY_DOWN        4     /* Key press (key_code = Amiga rawkey/ASCII, text_or_url = UTF-8/key name) */
#define INPUT_KEY_UP          5     /* Key release */
#define INPUT_GOTO_URL        6     /* Navigate to URL or command (@BACK, @FORWARD, @RELOAD) in text_or_url */
#define INPUT_MOUSE_DOWN      7     /* Mouse button press down */
#define INPUT_MOUSE_UP        8     /* Mouse button release up */

/* Big-Endian conversion helpers (No-Op on PowerPC / Big-Endian AmigaOS 4) */
#if defined(__BIG_ENDIAN__) || defined(__PPC__) || defined(__powerpc__) || \
    (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
    #define VB_BE16(x) (x)
    #define VB_BE32(x) (x)
    #define VB_BE64(x) (x)
#else
    #define VB_BE16(x) __builtin_bswap16(x)
    #define VB_BE32(x) __builtin_bswap32(x)
    #define VB_BE64(x) __builtin_bswap64(x)
#endif

#pragma pack(push, 2)

/* 0x01: MSG_FRAME (10 bytes)
 * Wire layout: [uint8_t msg_type=0x01] [MsgFrame (10B)] [uint8_t lz4_payload[payload_len]]
 * Python struct format: ">HHBBI"
 */
typedef struct MsgFrame {
    uint16_t width;        /* Rendered viewport width on Pi (Big-Endian) */
    uint16_t height;       /* Rendered viewport height on Pi (Big-Endian) */
    uint8_t  format;       /* 1 = ARGB32, 2 = RGB565 */
    uint8_t  codec;        /* 1 = LZ4 */
    uint32_t payload_len;  /* Compressed payload size in bytes (Big-Endian) */
} MsgFrame;

/* 0x02: MSG_INPUT (522 bytes under pack(2): 1B event_type + 1B pad + 4*2B + 512B)
 * Wire layout: [uint8_t msg_type=0x02] [MsgInput (522B)]
 * Python struct format: ">BxhhhH512s"
 */
typedef struct MsgInput {
    uint8_t  event_type;       /* 1=CLICK, 2=MOUSE_MOVE, 3=WHEEL, 4=KEY_DOWN, 5=KEY_UP, 6=GOTO_URL */
    int16_t  x;                /* X coordinate relative to web viewport (Big-Endian) */
    int16_t  y;                /* Y coordinate relative to web viewport (Big-Endian) */
    int16_t  delta_y;          /* Vertical scroll wheel delta (Big-Endian) */
    uint16_t key_code;         /* Key code or mouse button ID (Big-Endian) */
    char     text_or_url[512]; /* Null-terminated URL, key name, or text input */
} MsgInput;

/* 0x03: MSG_DOWNLOAD_START (268 bytes)
 * Wire layout: [uint8_t msg_type=0x03] [MsgDownloadStart (268B)]
 * Python struct format: ">IQ256s"
 */
typedef struct MsgDownloadStart {
    uint32_t download_id;             /* Unique download identifier (Big-Endian) */
    uint64_t total_size;              /* Total file size in bytes, 0 if unknown (Big-Endian) */
    char     suggested_filename[256]; /* Null-terminated suggested file name */
} MsgDownloadStart;

/* 0x04: MSG_DOWNLOAD_RESPONSE (6 bytes under pack(2): 4B download_id + 1B accepted + 1B tail pad)
 * Wire layout: [uint8_t msg_type=0x04] [MsgDownloadResponse (6B)]
 * Python struct format: ">IBx"
 */
typedef struct MsgDownloadResponse {
    uint32_t download_id; /* Unique download identifier (Big-Endian) */
    uint8_t  accepted;    /* 1 = Accept and start streaming, 0 = Cancel download */
} MsgDownloadResponse;

/* 0x05: MSG_DOWNLOAD_CHUNK (8 bytes)
 * Wire layout: [uint8_t msg_type=0x05] [MsgDownloadChunk (8B)] [uint8_t data[chunk_len]]
 * Python struct format: ">II"
 */
typedef struct MsgDownloadChunk {
    uint32_t download_id; /* Unique download identifier (Big-Endian) */
    uint32_t chunk_len;   /* Byte length of following raw chunk payload (Big-Endian) */
} MsgDownloadChunk;

/* 0x06: MSG_DOWNLOAD_COMPLETE (4 bytes)
 * Wire layout: [uint8_t msg_type=0x06] [MsgDownloadComplete (4B)]
 * Python struct format: ">I"
 */
typedef struct MsgDownloadComplete {
    uint32_t download_id; /* Unique download identifier (Big-Endian) */
} MsgDownloadComplete;

/* 0x07: MSG_AUDIO (12 bytes)
 * Wire layout: [uint8_t msg_type=0x07] [MsgAudio (12B)] [uint8_t payload[payload_len]]
 * Python struct format: ">HBBII"
 */
typedef struct MsgAudio {
    uint16_t sample_rate; /* Sample frequency in Hz, e.g. 22050 (Big-Endian) */
    uint8_t  channels;    /* 1 = Mono, 2 = Stereo */
    uint8_t  codec;       /* 0 = Raw S16BE, 1 = LZ4 S16BE */
    uint32_t raw_len;     /* Uncompressed PCM byte length (Big-Endian) */
    uint32_t payload_len; /* Compressed/transmitted byte length (Big-Endian) */
} MsgAudio;

/* 0x08: MSG_NAV_UPDATE (512 bytes)
 * Wire layout: [uint8_t msg_type=0x08] [MsgNavUpdate (512B)]
 * Python struct format: ">512s"
 */
typedef struct MsgNavUpdate {
    char url[512]; /* Null-terminated current browser URL */
} MsgNavUpdate;

/* 0x09: MSG_CONTEXT_MENU (1152 bytes)
 * Wire layout: [uint8_t msg_type=0x09] [MsgContextMenu (1152B)]
 * Python struct format: ">512s128s512s"
 */
typedef struct MsgContextMenu {
    char link_url[512];  /* Null-terminated <a href> URL under cursor, or empty */
    char link_text[128]; /* Null-terminated visible link text / title (ISO-8859-1 / ASCII), or empty */
    char media_url[512]; /* Null-terminated <img/video/audio src> URL under cursor, or empty */
} MsgContextMenu;

/* 0x0A: MSG_PAGE_PROGRESS (66 bytes under pack(2): 1B percent + 1B pad + 64B status)
 * Wire layout: [uint8_t msg_type=0x0A] [MsgProgress (66B)]
 * Python struct format: ">Bx64s"
 */
typedef struct MsgProgress {
    uint8_t percent;    /* 0..100 */
    uint8_t pad;        /* Pad byte to align to 2 bytes */
    char    status[64]; /* Null-terminated status text, e.g. "Lade 45%..." */
} MsgProgress;

/* 0x0B: MSG_CLIPBOARD (4 bytes text_len)
 * Wire layout: [uint8_t msg_type=0x0B] [MsgClipboard (4B)] [char utf8_text[text_len]]
 * Python struct format: ">I"
 */
typedef struct MsgClipboard {
    uint32_t text_len;  /* Byte length of following UTF-8 text string (Big-Endian) */
} MsgClipboard;

/* CODEC_LZ4_RECT: 2D Bounding Box Sub-Rectangle Header (8 bytes)
 * Precedes LZ4 compressed sub-rectangle payload for CODEC_LZ4_RECT
 * Python struct format: ">HHHH"
 */
typedef struct MsgRectHdr {
    uint16_t x;  /* Sub-rectangle left X in viewport pixels */
    uint16_t y;  /* Sub-rectangle top Y in viewport pixels */
    uint16_t w;  /* Sub-rectangle width in pixels */
    uint16_t h;  /* Sub-rectangle height in pixels */
} MsgRectHdr;

#pragma pack(pop)

/* Compile-time size verification to ensure binary compatibility between PPC C and Python */
typedef char vb_assert_msg_frame_size[(sizeof(MsgFrame) == 10) ? 1 : -1];
typedef char vb_assert_msg_input_size[(sizeof(MsgInput) == 522) ? 1 : -1];
typedef char vb_assert_msg_dl_start_size[(sizeof(MsgDownloadStart) == 268) ? 1 : -1];
typedef char vb_assert_msg_dl_resp_size[(sizeof(MsgDownloadResponse) == 6) ? 1 : -1];
typedef char vb_assert_msg_dl_chunk_size[(sizeof(MsgDownloadChunk) == 8) ? 1 : -1];
typedef char vb_assert_msg_dl_comp_size[(sizeof(MsgDownloadComplete) == 4) ? 1 : -1];
typedef char vb_assert_msg_audio_size[(sizeof(MsgAudio) == 12) ? 1 : -1];
typedef char vb_assert_msg_nav_size[(sizeof(MsgNavUpdate) == 512) ? 1 : -1];
typedef char vb_assert_msg_ctx_size[(sizeof(MsgContextMenu) == 1152) ? 1 : -1];
typedef char vb_assert_msg_prog_size[(sizeof(MsgProgress) == 66) ? 1 : -1];
typedef char vb_assert_msg_clip_size[(sizeof(MsgClipboard) == 4) ? 1 : -1];
typedef char vb_assert_msg_rect_size[(sizeof(MsgRectHdr) == 8) ? 1 : -1];

#ifdef __cplusplus
}
#endif

#endif /* VORTEXBROWSE_PROTOCOL_H */
