# TTPlayer Rebuild FAAD2 compatibility fixes, 2026-10-29.
# Apply only to the copied build-tree sources; all upstream notices remain.
function(faad_compat_replace file before after)
  set(path "${FAAD_PATCHED_DIR}/${file}")
  file(READ "${path}" content)
  string(FIND "${content}" "${before}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "FAAD2 ${FAAD_VERSION} compatibility patch no longer matches ${file}")
  endif()
  string(REPLACE "${before}" "${after}" content "${content}")
  file(WRITE "${path}" "${content}")
endfunction()

# er_raw_data_block has a fixed channel-element sequence, not the ordinary
# raw_data_block ID_FIL element. Looking ahead after its final LFE mistakes
# padding for an SBR fill and consumes bytes beyond the access-unit boundary.
faad_compat_replace("libfaad/syntax.c"
  "if (faad_showbits(ld, LEN_SE_ID) == ID_FIL)"
  "/* TTPlayer 2026-10-29: ID_FIL belongs to non-ER raw_data_block. */\n    if (hDecoder->object_type < ER_OBJECT_START &&\n        faad_showbits(ld, LEN_SE_ID) == ID_FIL)")

# ISO/IEC 14496-3:2009 4.6.17.2.5, tables 4.172 and 4.173: LD has
# different TNS bandwidth limits from the 1024-sample LC/Main family.
# Pass the existing transform length through the private helper so 480 and
# 512 samples also differ correctly at 22.05/24 kHz. No public ABI changes.
foreach(file IN ITEMS "libfaad/common.h" "libfaad/common.c")
  faad_compat_replace("${file}" "const uint8_t is_short)"
    "const uint8_t is_short, const uint16_t frame_len)")
endforeach()
faad_compat_replace("libfaad/common.c" [=[    uint8_t i = 0;

    if (is_short) i++;]=] [=[    uint8_t i = 0;

    /* TTPlayer 2026-10-29: LD TNS limits, not the LC long-window table. */
    if (object_type == LD)
    {
        static const uint8_t ld_512[] = {31, 32, 37, 31, 31};
        if (sr_index < 3 || sr_index > 7 ||
            (frame_len != 480 && frame_len != 512))
            return 0;
        if (frame_len == 480 && sr_index >= 6)
            return 30;
        return ld_512[sr_index - 3];
    }

    if (is_short) i++;]=])
faad_compat_replace("libfaad/tns.c"
  "max_tns_sfb(sr_index, object_type, (ics->window_sequence == EIGHT_SHORT_SEQUENCE))"
  "max_tns_sfb(sr_index, object_type, (ics->window_sequence == EIGHT_SHORT_SEQUENCE), frame_len)")

# FAAD's synthesis tables and reported PCM rate use the frequency index. An
# explicit core frequency equal to a table rate is exactly equivalent to that
# index. SBR also permits twice a supported core rate (e.g. 96 -> 192 kHz).
# Arbitrary rates must not be silently rounded to a table rate.
faad_compat_replace("libfaad/mp4.c" "/* Table 1.6.1 */" [=[
/* TTPlayer 2026-10-29: retain and validate explicit ASC frequencies. */
static int8_t ttp_read_asc_frequency(bitfile *ld, uint8_t *index, uint32_t *frequency,
                                   uint8_t sbr_output)
{
    if (*index == 15)
    {
        const uint32_t used = faad_get_processed_bits(ld);
        const uint64_t available = (uint64_t)ld->buffer_size * 8;
        if (used > available || available - used < 24)
            return -2;
        *frequency = faad_getbits(ld, 24);
        if (!*frequency)
            return -2;
        if (get_sample_rate(get_sr_index(*frequency)) == *frequency)
            *index = get_sr_index(*frequency);
        else if (!sbr_output || (*frequency & 1) ||
                 get_sample_rate(get_sr_index(*frequency / 2)) != *frequency / 2)
            return -2;
        /* Keep index 15 for a doubled output rate outside the table. Init2
           derives the supported AAC core index from half the SBR frequency. */
    } else {
        *frequency = get_sample_rate(*index);
    }
    return *frequency ? 0 : -2;
}

/* Table 1.6.1 */]=])

faad_compat_replace("libfaad/mp4.c"
  "\tif(mp4ASC->samplingFrequencyIndex==0x0f)\n\t\tfaad_getbits(ld, 24);"
  [=[    if (ttp_read_asc_frequency(ld, &mp4ASC->samplingFrequencyIndex,
                               &mp4ASC->samplingFrequency, 0) != 0)
        return -2;]=])

# A sync extension's object type alone is not its complete SBR declaration.
# Do not synthesize a missing present bit / rate index from zero-filled bits.
faad_compat_replace("libfaad/mp4.c" [=[            if (tmp_OTi == 5 || tmp_OTi == 29)
            {
                mp4ASC->sbr_present_flag]=] [=[            if (tmp_OTi == 5 || tmp_OTi == 29)
            {
                if ((uint64_t)faad_get_processed_bits(ld) + 1 > (uint64_t)ld->buffer_size * 8)
                    return -2;
                mp4ASC->sbr_present_flag]=])
faad_compat_replace("libfaad/mp4.c" [=[                    tmp = (uint8_t)faad_getbits(ld, 4
                        DEBUGVAR(1,12,]=] [=[                    if ((uint64_t)faad_get_processed_bits(ld) + 4 > (uint64_t)ld->buffer_size * 8)
                        return -2;
                    tmp = (uint8_t)faad_getbits(ld, 4
                        DEBUGVAR(1,12,]=])

faad_compat_replace("libfaad/mp4.c" [=[        /* check for downsampled SBR */
        if (tmp == mp4ASC->samplingFrequencyIndex)
            mp4ASC->downSampledSBR = 1;
        mp4ASC->samplingFrequencyIndex = tmp;
        if (mp4ASC->samplingFrequencyIndex == 15)
        {
            mp4ASC->samplingFrequency = (uint32_t)faad_getbits(ld, 24
                DEBUGVAR(1,6,"parse_audio_decoder_specific_info(): extensionSamplingFrequencyIndex"));
        } else {
            mp4ASC->samplingFrequency = get_sample_rate(mp4ASC->samplingFrequencyIndex);
        }]=] [=[        /* Normalize before comparing the core and SBR frequencies. */
        if (ttp_read_asc_frequency(ld, &tmp, &mp4ASC->samplingFrequency, 1) != 0)
            return -2;
        if (tmp == mp4ASC->samplingFrequencyIndex)
            mp4ASC->downSampledSBR = 1;
        mp4ASC->samplingFrequencyIndex = tmp;]=])

faad_compat_replace("libfaad/mp4.c" [=[                    /* check for downsampled SBR */
                    if (tmp == mp4ASC->samplingFrequencyIndex)
                        mp4ASC->downSampledSBR = 1;
                    mp4ASC->samplingFrequencyIndex = tmp;

                    if (mp4ASC->samplingFrequencyIndex == 15)
                    {
                        mp4ASC->samplingFrequency = (uint32_t)faad_getbits(ld, 24
                            DEBUGVAR(1,13,"parse_audio_decoder_specific_info(): extensionSamplingFrequencyIndex"));
                    } else {
                        mp4ASC->samplingFrequency = get_sample_rate(mp4ASC->samplingFrequencyIndex);
                    }]=] [=[                    if (ttp_read_asc_frequency(ld, &tmp, &mp4ASC->samplingFrequency, 1) != 0)
                        return -2;
                    if (tmp == mp4ASC->samplingFrequencyIndex)
                        mp4ASC->downSampledSBR = 1;
                    mp4ASC->samplingFrequencyIndex = tmp;]=])
