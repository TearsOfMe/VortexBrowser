#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <devices/ahi.h>
#include <liblz4/lz4.h>

#include "audio.h"

BOOL audio_init(VortexApp *app)
{
    AudioState *a = &app->audio;
    memset(a, 0, sizeof(AudioState));

    a->ahi_mp = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    if (!a->ahi_mp) {
        DBG_LOG("[Audio] WARNING: Could not allocate AHI MsgPort.\n");
        return FALSE;
    }

    a->io[0] = (struct AHIRequest *)IExec->AllocSysObjectTags(
        ASOT_IOREQUEST,
        ASOIOR_ReplyPort, a->ahi_mp,
        ASOIOR_Size,      sizeof(struct AHIRequest),
        TAG_END
    );
    if (!a->io[0]) {
        audio_cleanup(app);
        return FALSE;
    }

    a->io[0]->ahir_Version = 4;
    if (IExec->OpenDevice("ahi.device", AHI_DEFAULT_UNIT, (struct IORequest *)a->io[0], 0) != IOERR_SUCCESS) {
        DBG_LOG("[Audio] WARNING: Could not open ahi.device unit 0 (continuing without sound).\n");
        a->io[0]->ahir_Std.io_Device = NULL;
        audio_cleanup(app);
        return FALSE;
    }
    a->device_open = TRUE;

    a->io[1] = (struct AHIRequest *)IExec->AllocSysObjectTags(
        ASOT_IOREQUEST,
        ASOIOR_Duplicate, a->io[0],
        TAG_END
    );
    if (!a->io[1]) {
        audio_cleanup(app);
        return FALSE;
    }

    for (int i = 0; i < VB_AUDIO_SLOTS; i++) {
        a->pcm_buf[i] = (uint8_t *)IExec->AllocVecTags(
            VB_AUDIO_BUF_SIZE,
            AVT_Type,           MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE
        );
        if (!a->pcm_buf[i]) {
            audio_cleanup(app);
            return FALSE;
        }
    }

    DBG_LOG("[Audio] AHI double-buffered streaming ready (2 HW IORequests, %d ring slots).\n",
           VB_AUDIO_SLOTS);
    return TRUE;
}

uint32 audio_get_signal_mask(VortexApp *app)
{
    AudioState *a = &app->audio;
    if (!a->device_open || !a->ahi_mp) {
        return 0;
    }
    return (1UL << a->ahi_mp->mp_SigBit);
}

static void submit_pending_to_hardware(AudioState *a)
{
    while (a->hw_in_flight < 2 && a->ring_unsubmitted > 0) {
        int hw_idx   = (a->hw_oldest + a->hw_in_flight) & 1;
        int ring_idx = a->ring_next_submit;

        struct AHIRequest *req  = a->io[hw_idx];
        struct AHIRequest *prev = (a->hw_in_flight == 1) ? a->io[a->hw_oldest] : NULL;

        req->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
        req->ahir_Std.io_Command = CMD_WRITE;
        req->ahir_Std.io_Data    = a->pcm_buf[ring_idx];
        req->ahir_Std.io_Length  = a->pcm_len[ring_idx];
        req->ahir_Std.io_Offset  = 0;
        req->ahir_Frequency      = (ULONG)a->pcm_rate[ring_idx];
        req->ahir_Type           = (a->pcm_ch[ring_idx] == 1) ? AHIST_M16S : AHIST_S16S;
        req->ahir_Volume         = 0x10000;
        req->ahir_Position       = 0x8000;
        req->ahir_Link           = prev;

        IExec->SendIO((struct IORequest *)req);

        a->io_active[hw_idx] = TRUE;
        a->hw_in_flight++;
        a->ring_next_submit = (a->ring_next_submit + 1) % VB_AUDIO_SLOTS;
        a->ring_unsubmitted--;
        a->chunks_played++;
    }
}

void audio_poll(VortexApp *app)
{
    AudioState *a = &app->audio;
    if (!a->device_open) {
        return;
    }

    /* Reap completed hardware AHIRequest(s) without blocking */
    while (a->hw_in_flight > 0) {
        int oldest = a->hw_oldest;
        struct AHIRequest *req = a->io[oldest];
        if (req && a->io_active[oldest] && IExec->CheckIO((struct IORequest *)req) != NULL) {
            IExec->WaitIO((struct IORequest *)req);
            a->io_active[oldest] = FALSE;
            a->hw_oldest = oldest ^ 1;
            a->hw_in_flight--;

            if (a->ring_count > 0) {
                a->ring_head = (a->ring_head + 1) % VB_AUDIO_SLOTS;
                a->ring_count--;
            }
        } else {
            break;
        }
    }

    /* Immediately feed next queued software slot(s) into AHI double-buffer */
    submit_pending_to_hardware(a);

    /* Clear any lingering signal on the MsgPort so WaitSelect will sleep */
    if (a->ahi_mp) {
        IExec->SetSignal(0, 1UL << a->ahi_mp->mp_SigBit);
    }
}

void audio_handle_packet(
    VortexApp      *app,
    const MsgAudio *hdr,
    const uint8_t  *payload,
    uint32_t        payload_len
)
{
    AudioState *a = &app->audio;
    if (!a->device_open || a->muted) {
        return;
    }

    audio_poll(app);

    if (a->ring_count >= VB_AUDIO_SLOTS) {
        return;
    }

    uint16_t sample_rate = VB_BE16(hdr->sample_rate);
    uint32_t raw_len     = VB_BE32(hdr->raw_len);
    if (sample_rate == 0 || raw_len == 0 || raw_len > VB_AUDIO_BUF_SIZE) {
        return;
    }

    int slot = a->ring_tail;
    uint8_t *dst_pcm = a->pcm_buf[slot];

    if (hdr->codec == CODEC_LZ4) {
        int dec = LZ4_decompress_safe(
            (const char *)payload,
            (char *)dst_pcm,
            (int)payload_len,
            (int)raw_len
        );
        if (dec <= 0) {
            return;
        }
        raw_len = (uint32_t)dec;
    } else {
        if (payload_len > VB_AUDIO_BUF_SIZE) {
            return;
        }
        memcpy(dst_pcm, payload, payload_len);
        raw_len = payload_len;
    }

    uint32_t frame_mask = (hdr->channels == 1) ? ~1U : ~3U;
    raw_len &= frame_mask;
    if (raw_len == 0) {
        return;
    }

    a->pcm_len[slot]  = raw_len;
    a->pcm_rate[slot] = sample_rate;
    a->pcm_ch[slot]   = hdr->channels;

    a->ring_tail = (a->ring_tail + 1) % VB_AUDIO_SLOTS;
    a->ring_count++;
    a->ring_unsubmitted++;

    submit_pending_to_hardware(a);
}

void audio_cleanup(VortexApp *app)
{
    AudioState *a = &app->audio;

    if (a->device_open) {
        /* Abort newest linked request first, then oldest */
        for (int k = a->hw_in_flight - 1; k >= 0; k--) {
            int hw_idx = (a->hw_oldest + k) & 1;
            if (a->io[hw_idx] && a->io_active[hw_idx]) {
                if (IExec->CheckIO((struct IORequest *)a->io[hw_idx]) == NULL) {
                    IExec->AbortIO((struct IORequest *)a->io[hw_idx]);
                }
                IExec->WaitIO((struct IORequest *)a->io[hw_idx]);
                a->io_active[hw_idx] = FALSE;
            }
        }
        a->hw_in_flight = 0;

        if (a->io[0] && a->io[0]->ahir_Std.io_Device) {
            IExec->CloseDevice((struct IORequest *)a->io[0]);
            a->io[0]->ahir_Std.io_Device = NULL;
        }
        a->device_open = FALSE;
    }

    if (a->io[1]) {
        IExec->FreeSysObject(ASOT_IOREQUEST, a->io[1]);
        a->io[1] = NULL;
    }
    if (a->io[0]) {
        IExec->FreeSysObject(ASOT_IOREQUEST, a->io[0]);
        a->io[0] = NULL;
    }

    for (int i = 0; i < VB_AUDIO_SLOTS; i++) {
        if (a->pcm_buf[i]) {
            IExec->FreeVec(a->pcm_buf[i]);
            a->pcm_buf[i] = NULL;
        }
    }

    if (a->ahi_mp) {
        IExec->FreeSysObject(ASOT_PORT, a->ahi_mp);
        a->ahi_mp = NULL;
    }
}
