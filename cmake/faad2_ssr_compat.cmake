# TTPlayer Rebuild, 2026-10-30: complete four-band AAC SSR synthesis.
# Apply after rate/LD/coupling adaptations. Private FAAD source copies only.
faad_compat_replace("libfaad/common.h" "//#define SSR_DEC" "#define SSR_DEC")
foreach(pair IN ITEMS "ssr.c|faad2_ssr_core.c" "ssr_ipqf.c|faad2_ssr_ipqf.c" "ssr_fb.c|faad2_ssr_filterbank.c")
  string(REPLACE "|" ";" files "${pair}")
  list(GET files 0 destination)
  list(GET files 1 source)
  configure_file("${CMAKE_CURRENT_LIST_DIR}/${source}" "${FAAD_PATCHED_DIR}/libfaad/${destination}" COPYONLY)
endforeach()
faad_compat_replace("libfaad/ssr.h" "#define SSR_BANDS 4"
  "#define SSR_BANDS 4\nuint8_t ttp_ssr_validate(const ssr_info *ssr, uint8_t sequence);")
faad_compat_replace("libfaad/decoder.c" [=[#include "ssr.h"]=] [=[#include "ssr.h"
#include "ssr_fb.h"]=])
faad_compat_replace("libfaad/syntax.c" [=[#include "structs.h"]=] [=[#include "structs.h"
#ifdef SSR_DEC
#include "ssr.h"
#endif]=])
faad_compat_replace("libfaad/syntax.c" [=[            else
                gain_control_data(ld, ics);]=] [=[            else {
                gain_control_data(ld, ics);
                if (ttp_ssr_validate(&ics->ssr, ics->window_sequence)) return 30;
            }]=])

# ISO/IEC 14496-3:2009 section 4.5.1.1 requires frameLengthFlag=0 for
# SSR (256/32 lines per band). Reject the prohibited 960 flag before setup.
faad_compat_replace("libfaad/decoder.c"
  "    /* must be done before frameLength is divided by 2 for LD */"
  [=[    if (hDecoder->object_type == SSR &&
        (hDecoder->frameLength != 1024 || hDecoder->sbr_present_flag == 1))
        return -1;

    /* must be done before frameLength is divided by 2 for LD */]=])
# No SBR profile uses an SSR core. Preserve the old heuristic for other AOTs.
faad_compat_replace("libfaad/mp4.c"
  "mp4ASC->sbr_present_flag == (char)-1"
  "mp4ASC->sbr_present_flag == (char)-1 && mp4ASC->objectTypeIndex != SSR")
faad_compat_replace("libfaad/decoder.c" "if (*samplerate <= 24000"
  "if (hDecoder->object_type != SSR && *samplerate <= 24000")

# Restore dormant CPE allocator scope and initialize PFMD to unity.
faad_compat_replace("libfaad/specrec.c" [=[#ifdef SSR_DEC
    if (hDecoder->object_type == SSR)
    {
        if (hDecoder->ssr_overlap[cpe->channel] == NULL)
        {
            hDecoder->ssr_overlap[cpe->channel] = (real_t*)faad_malloc(2*hDecoder->frameLength*sizeof(real_t));
            memset(hDecoder->ssr_overlap[cpe->channel], 0, 2*hDecoder->frameLength*sizeof(real_t));
        }
        if (hDecoder->ssr_overlap[cpe->paired_channel] == NULL)
        {
            hDecoder->ssr_overlap[cpe->paired_channel] = (real_t*)faad_malloc(2*hDecoder->frameLength*sizeof(real_t));
            memset(hDecoder->ssr_overlap[cpe->paired_channel], 0, 2*hDecoder->frameLength*sizeof(real_t));
        }
        if (hDecoder->prev_fmd[cpe->channel] == NULL)
        {
            uint16_t k;
            hDecoder->prev_fmd[cpe->channel] = (real_t*)faad_malloc(2*hDecoder->frameLength*sizeof(real_t));
            for (k = 0; k < 2*hDecoder->frameLength; k++)
                hDecoder->prev_fmd[cpe->channel][k] = REAL_CONST(-1);
        }
        if (hDecoder->prev_fmd[cpe->paired_channel] == NULL)
        {
            uint16_t k;
            hDecoder->prev_fmd[cpe->paired_channel] = (real_t*)faad_malloc(2*hDecoder->frameLength*sizeof(real_t));
            for (k = 0; k < 2*hDecoder->frameLength; k++)
                hDecoder->prev_fmd[cpe->paired_channel][k] = REAL_CONST(-1);
        }
    }
#endif]=] [=[#ifdef SSR_DEC
    if (hDecoder->object_type == SSR)
    {
        if (hDecoder->ssr_overlap[channel] == NULL)
        {
            hDecoder->ssr_overlap[channel] = (real_t*)faad_malloc(2*hDecoder->frameLength*sizeof(real_t));
            memset(hDecoder->ssr_overlap[channel], 0, 2*hDecoder->frameLength*sizeof(real_t));
        }
        if (hDecoder->ssr_overlap[paired_channel] == NULL)
        {
            hDecoder->ssr_overlap[paired_channel] = (real_t*)faad_malloc(2*hDecoder->frameLength*sizeof(real_t));
            memset(hDecoder->ssr_overlap[paired_channel], 0, 2*hDecoder->frameLength*sizeof(real_t));
        }
        if (hDecoder->prev_fmd[channel] == NULL)
        {
            uint16_t k;
            hDecoder->prev_fmd[channel] = (real_t*)faad_malloc(2*hDecoder->frameLength*sizeof(real_t));
            for (k = 0; k < 2*hDecoder->frameLength; k++)
                hDecoder->prev_fmd[channel][k] = REAL_CONST(1);
        }
        if (hDecoder->prev_fmd[paired_channel] == NULL)
        {
            uint16_t k;
            hDecoder->prev_fmd[paired_channel] = (real_t*)faad_malloc(2*hDecoder->frameLength*sizeof(real_t));
            for (k = 0; k < 2*hDecoder->frameLength; k++)
                hDecoder->prev_fmd[paired_channel][k] = REAL_CONST(1);
        }
    }
#endif]=])
faad_compat_replace("libfaad/specrec.c" "hDecoder->prev_fmd[channel][k] = REAL_CONST(-1);" "hDecoder->prev_fmd[channel][k] = REAL_CONST(1);")

# Rewind must reset every SSR history, including the per-channel IPQF. State
# is owned by the decoder; no globals, init races or newer Windows APIs.
faad_compat_replace("libfaad/decoder.c" "        hDecoder->postSeekResetFlag = 1;"
  [=[        hDecoder->postSeekResetFlag = 1;
#ifdef SSR_DEC
        if (hDecoder->object_type == SSR) {
            unsigned c, j;
            memset(hDecoder->ipqf_buffer, 0, sizeof(hDecoder->ipqf_buffer));
            for (c=0;c<MAX_CHANNELS;++c) {
                hDecoder->window_shape_prev[c]=0;
                if(hDecoder->ssr_overlap[c])
                    memset(hDecoder->ssr_overlap[c],0,2*hDecoder->frameLength*sizeof(real_t));
                if(hDecoder->prev_fmd[c])
                    for(j=0;j<2*hDecoder->frameLength;++j)hDecoder->prev_fmd[c][j]=REAL_CONST(1);
            }
        }
#endif]=])

# Reject an SSR+SBR sync extension before upstream rewrites its AOT to HE.
faad_compat_replace("libfaad/mp4.c" [=[                if (mp4ASC->sbr_present_flag)
                {
                    uint8_t tmp;]=] [=[                if (mp4ASC->sbr_present_flag)
                {
                    uint8_t tmp;
                    if (mp4ASC->objectTypeIndex == SSR) return -2;]=])

faad_compat_replace("libfaad/syntax.c"
  "            /* TTPlayer: do not approximate unimplemented SBR at this rate. */"
  [=[            /* SSR has no SBR tool; do not run LC/SBR processing on its filter bank. */
            if (hDecoder->object_type == SSR) return 24;
            /* TTPlayer: do not approximate unimplemented SBR at this rate. */]=])

# Upstream's disabled SSR guard accidentally tests AOT 4 (LTP). Enabling SSR
# must enforce the SSR-only restriction without rejecting legal LTP960.
faad_compat_replace("libfaad/mp4.c"
  "if ((mp4ASC->objectTypeIndex == 4) && mp4ASC->frameLengthFlag)"
  "if ((mp4ASC->objectTypeIndex == SSR) && mp4ASC->frameLengthFlag)")
