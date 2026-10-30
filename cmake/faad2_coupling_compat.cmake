# TTPlayer FAAD2 Main/LC/LTP coupling, 2026-10-30. GPL-2.0-or-later.
# Patch only the build-tree copy, after the ordinary FAAD compatibility patches.
function(faad_cce_replace file before after)
  set(path "${FAAD_PATCHED_DIR}/${file}")
  file(READ "${path}" content)
  string(FIND "${content}" "${before}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "FAAD2 ${FAAD_VERSION} CCE patch no longer matches ${file}")
  endif()
  string(REPLACE "${before}" "${after}" content "${content}")
  file(WRITE "${path}" "${content}")
endfunction()
file(COPY "${CMAKE_CURRENT_LIST_DIR}/../src/faad_coupling/ttp_coupling.h"
          "${CMAKE_CURRENT_LIST_DIR}/../src/faad_coupling/ttp_coupling.c"
          "${CMAKE_CURRENT_LIST_DIR}/../src/faad_coupling/ttp_coupling_parse.inc"
     DESTINATION "${FAAD_PATCHED_DIR}/libfaad")

faad_cce_replace("libfaad/structs.h" [=[	const unsigned char *cmes;]=] [=[	const unsigned char *cmes;
    /* Private per-reader CCE state, never exposed through the plugin ABI. */
    struct ttp_cce_context *ttp_coupling;
    struct ttp_cce_frame *ttp_coupling_source;]=])

foreach(file IN ITEMS "libfaad/syntax.c" "libfaad/specrec.c" "libfaad/decoder.c")
  faad_cce_replace("${file}" "#include \"structs.h\""
    "#include \"structs.h\"\n#include \"ttp_coupling.h\"\n#include <float.h>")
endforeach()

# The parser is in syntax.c so it can use the existing checked ICS/Huffman code.
faad_cce_replace("libfaad/syntax.c" "/* Table 4.4.1 */"
  "#include \"ttp_coupling_parse.inc\"\n\n/* Table 4.4.1 */")
faad_cce_replace("libfaad/syntax.c" [=[    hDecoder->fr_channels = 0;
    hDecoder->fr_ch_ele = 0;]=] [=[    hInfo->error = ttp_cce_begin(hDecoder);
    if (hInfo->error) return;
    hDecoder->fr_channels = 0;
    hDecoder->fr_ch_ele = 0;]=])
faad_cce_replace("libfaad/syntax.c" [=[#ifdef COUPLING_DEC
                hInfo->error = coupling_channel_element(hDecoder, ld);
#else
                hInfo->error = 6;
#endif]=] [=[                hInfo->error = ttp_parse_cce(hDecoder, ld);]=])
faad_cce_replace("libfaad/syntax.c"
  "case ID_CCE: /* not implemented yet, but skip the bits */"
  "case ID_CCE: /* parse and stage the coupling source for frame synthesis */")
faad_cce_replace("libfaad/syntax.c"
  "retval = reconstruct_single_channel(hDecoder, ics, &sce, spec_data);"
  "retval = ttp_cce_queue_single(hDecoder, &sce, spec_data);")
faad_cce_replace("libfaad/syntax.c" [=[reconstruct_channel_pair(hDecoder, ics1, ics2, &cpe,
        spec_data1, spec_data2)]=] [=[ttp_cce_queue_pair(hDecoder, &cpe, spec_data1, spec_data2)]=])
faad_cce_replace("libfaad/syntax.c" [=[        faad_byte_align(ld);
    }

    return;
}

/* Table 4.4.4 and */]=] [=[        faad_byte_align(ld);
    }
    if (ld->error || (uint64_t)faad_get_processed_bits(ld) > (uint64_t)ld->buffer_size * 8) {
        hInfo->error = 14;
        return;
    }
    hInfo->error = ttp_cce_finish(hDecoder);
}

/* Table 4.4.4 and */]=])

faad_cce_replace("libfaad/specrec.c" [=[    /* tns decoding */
    tns_decode_frame(ics,]=] [=[    /* Apply dependent CCE at its declared spectral coupling point. */
    retval = ttp_cce_spectral(hDecoder, ics, sce->channel, 0, spec_coef);
    if (retval) return retval;
    /* tns decoding */
    tns_decode_frame(ics,]=])
faad_cce_replace("libfaad/specrec.c" [=[        spec_coef, hDecoder->frameLength);

    /* drc decoding */]=] [=[        spec_coef, hDecoder->frameLength);
    if (ttp_cce_capture(hDecoder, spec_coef)) return 0;
    retval = ttp_cce_spectral(hDecoder, ics, sce->channel, 1, spec_coef);
    if (retval) return retval;

    /* drc decoding */]=])
faad_cce_replace("libfaad/specrec.c" [=[    /* tns decoding */
    tns_decode_frame(ics1,]=] [=[    retval = ttp_cce_spectral(hDecoder, ics1, cpe->channel, 0, spec_coef1);
    if (retval) return retval;
    retval = ttp_cce_spectral(hDecoder, ics2, cpe->paired_channel, 0, spec_coef2);
    if (retval) return retval;
    /* tns decoding */
    tns_decode_frame(ics1,]=])
faad_cce_replace("libfaad/specrec.c" [=[        spec_coef2, hDecoder->frameLength);

    /* drc decoding */]=] [=[        spec_coef2, hDecoder->frameLength);
    retval = ttp_cce_spectral(hDecoder, ics1, cpe->channel, 1, spec_coef1);
    if (retval) return retval;
    retval = ttp_cce_spectral(hDecoder, ics2, cpe->paired_channel, 1, spec_coef2);
    if (retval) return retval;

    /* drc decoding */]=])
# Independent coupling must not feed another channel into the LTP predictor.
# Capture the ordinary target's synthesized history first, then add the CCE.
faad_cce_replace("libfaad/specrec.c" [=[            hDecoder->fb_intermed[sce->channel], hDecoder->frameLength, hDecoder->object_type);
    }
#endif]=] [=[            hDecoder->fb_intermed[sce->channel], hDecoder->frameLength, hDecoder->object_type);
    }
#endif
    retval = ttp_cce_time(hDecoder, sce->channel, hDecoder->time_out[sce->channel]);
    if (retval) return retval;]=])
faad_cce_replace("libfaad/specrec.c" [=[            hDecoder->fb_intermed[cpe->paired_channel], hDecoder->frameLength, hDecoder->object_type);
    }
#endif]=] [=[            hDecoder->fb_intermed[cpe->paired_channel], hDecoder->frameLength, hDecoder->object_type);
    }
#endif
    retval = ttp_cce_time(hDecoder, cpe->channel, hDecoder->time_out[cpe->channel]);
    if (retval) return retval;
    retval = ttp_cce_time(hDecoder, cpe->paired_channel, hDecoder->time_out[cpe->paired_channel]);
    if (retval) return retval;]=])
faad_cce_replace("libfaad/decoder.c" [=[    if (hDecoder == NULL)
        return;

#ifdef PROFILE]=] [=[    if (hDecoder == NULL)
        return;
    ttp_cce_clear(hDecoder);

#ifdef PROFILE]=])
faad_cce_replace("libfaad/decoder.c" "hDecoder->postSeekResetFlag = 1;"
  "ttp_cce_clear(hDecoder);\n        hDecoder->postSeekResetFlag = 1;")

# Main prediction reconstructs the independent channel spectra before intensity
# stereo derives the right channel. The upstream reversed order corrupts the
# rear channels of ISO am05_44 when intensity stereo starts at frame 91.
file(READ "${FAAD_PATCHED_DIR}/libfaad/specrec.c" cce_specrec)
string(FIND "${cce_specrec}" "uint8_t reconstruct_channel_pair" cce_pair)
string(SUBSTRING "${cce_specrec}" ${cce_pair} -1 cce_pair_body)
string(FIND "${cce_pair_body}" "#ifdef MAIN_DEC" cce_main_start)
string(FIND "${cce_pair_body}" "#ifdef LTP_DEC" cce_main_end)
if(cce_pair LESS 0 OR cce_main_start LESS 0 OR cce_main_end LESS cce_main_start)
  message(FATAL_ERROR "FAAD Main prediction ordering anchor changed")
endif()
math(EXPR cce_main_size "${cce_main_end}-${cce_main_start}")
string(SUBSTRING "${cce_pair_body}" ${cce_main_start} ${cce_main_size} cce_main_block)
string(REPLACE "${cce_main_block}" "" cce_pair_body "${cce_pair_body}")
string(REPLACE "    /* intensity stereo decoding */"
  "${cce_main_block}    /* intensity stereo decoding */" cce_pair_body "${cce_pair_body}")
string(SUBSTRING "${cce_specrec}" 0 ${cce_pair} cce_prefix)
file(WRITE "${FAAD_PATCHED_DIR}/libfaad/specrec.c" "${cce_prefix}${cce_pair_body}")
