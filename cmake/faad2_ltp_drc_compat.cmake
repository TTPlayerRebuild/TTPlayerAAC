# TTPlayer Rebuild 2026-10-30: keep DRC output gain out of LTP history.
# Apply after coupling/SSR patches; all edits affect only the FAAD build copy.
faad_compat_replace("libfaad/structs.h"
  "    int16_t *lt_pred_stat[MAX_CHANNELS];"
  "    int16_t *lt_pred_stat[MAX_CHANNELS];\n    real_t *ttp_ltp_raw_overlap[MAX_CHANNELS];")
faad_compat_replace("libfaad/decoder.c"
  "        hDecoder->lt_pred_stat[i] = NULL;"
  "        hDecoder->lt_pred_stat[i] = NULL;\n        hDecoder->ttp_ltp_raw_overlap[i] = NULL;")
faad_compat_replace("libfaad/decoder.c"
  "        if (hDecoder->lt_pred_stat[i]) faad_free(hDecoder->lt_pred_stat[i]);"
  "        if (hDecoder->lt_pred_stat[i]) faad_free(hDecoder->lt_pred_stat[i]);\n        if (hDecoder->ttp_ltp_raw_overlap[i]) faad_free(hDecoder->ttp_ltp_raw_overlap[i]);")
foreach(channel IN ITEMS channel paired_channel)
  faad_compat_replace("libfaad/specrec.c"
    "    if (hDecoder->fb_intermed[${channel}] != NULL)"
    "#ifdef LTP_DEC\n    if (hDecoder->ttp_ltp_raw_overlap[${channel}])\n    {\n        faad_free(hDecoder->ttp_ltp_raw_overlap[${channel}]);\n        hDecoder->ttp_ltp_raw_overlap[${channel}] = NULL;\n    }\n#endif\n    if (hDecoder->fb_intermed[${channel}] != NULL)")
endforeach()

# A separate unscaled overlap is needed once DRC first affects a channel:
# merely moving lt_update_state cannot undo spectral DRC or the previous
# frame's scaled overlap. time_out is scratch here and is overwritten by
# the normal output filter bank. No extra IMDCT/allocation for ordinary LTP.
faad_compat_replace("libfaad/specrec.c"
  "uint8_t reconstruct_single_channel(NeAACDecStruct *hDecoder, ic_stream *ics,"
  [=[static uint8_t ttp_ltp_raw_history(NeAACDecStruct *hDecoder, ic_stream *ics,
                                    uint8_t channel, real_t *spectrum)
{
#ifdef LTP_DEC
    real_t *overlap;
    size_t bytes;
    if (!is_ltp_ot(hDecoder->object_type)) return 0;
    overlap = hDecoder->ttp_ltp_raw_overlap[channel];
    if (!overlap)
    {
        if (!hDecoder->drc->present ||
            (hDecoder->drc->excluded_chns_present &&
             hDecoder->drc->exclude_mask[channel])) return 0;
        bytes = hDecoder->frameLength * sizeof(real_t);
        overlap = (real_t*)faad_malloc(bytes);
        if (!overlap) return 34;
        /* Until DRC first applies, the existing overlap is still unscaled. */
        memcpy(overlap, hDecoder->fb_intermed[channel], bytes);
        hDecoder->ttp_ltp_raw_overlap[channel] = overlap;
    }
    /* Preserve this branch across later gain/exclusion changes. */
    ifilter_bank(hDecoder->fb, ics->window_sequence, ics->window_shape,
        hDecoder->window_shape_prev[channel], spectrum,
        hDecoder->time_out[channel], overlap,
        hDecoder->object_type, hDecoder->frameLength);
    lt_update_state(hDecoder->lt_pred_stat[channel], hDecoder->time_out[channel],
        overlap, hDecoder->frameLength, hDecoder->object_type);
#endif
    return 0;
}

uint8_t reconstruct_single_channel(NeAACDecStruct *hDecoder, ic_stream *ics,]=])

faad_compat_replace("libfaad/specrec.c" [=[    /* drc decoding */
#ifdef APPLY_DRC]=] [=[    retval = ttp_ltp_raw_history(hDecoder, ics, sce->channel, spec_coef);
    if (retval) return retval;
    /* drc decoding */
#ifdef APPLY_DRC]=])
faad_compat_replace("libfaad/specrec.c" [=[    /* drc decoding */
#if APPLY_DRC]=] [=[    retval = ttp_ltp_raw_history(hDecoder, ics1, cpe->channel, spec_coef1);
    if (retval) return retval;
    retval = ttp_ltp_raw_history(hDecoder, ics2, cpe->paired_channel, spec_coef2);
    if (retval) return retval;
    /* drc decoding */
#if APPLY_DRC]=])
foreach(channel IN ITEMS "sce->channel" "cpe->channel" "cpe->paired_channel")
  faad_compat_replace("libfaad/specrec.c"
    "        lt_update_state(hDecoder->lt_pred_stat[${channel}], hDecoder->time_out[${channel}],"
    "        if (!hDecoder->ttp_ltp_raw_overlap[${channel}])\n        lt_update_state(hDecoder->lt_pred_stat[${channel}], hDecoder->time_out[${channel}],")
endforeach()
