# TTPlayer Rebuild, 2026-10-29: native AAC-LC 7350 Hz (frequency index 12).
# Included after faad2_compat.cmake; all replacements affect build copies only.
# The 7350 Hz frequency is distinct, while its standard core spectral-band
# tables are the same as the 8 kHz family. Do not round arbitrary frequencies.
faad_compat_replace("libfaad/common.c" [=[uint8_t get_sr_index(const uint32_t samplerate)
{]=] [=[uint8_t get_sr_index(const uint32_t samplerate)
{
    /* TTPlayer: preserve the exact 7350 Hz entry; other ranges are unchanged. */
    if (samplerate == 7350) return 12;]=])
faad_compat_replace("libfaad/common.c"
  "24000, 22050, 16000, 12000, 11025, 8000\n"
  "24000, 22050, 16000, 12000, 11025, 8000, 7350\n")
faad_compat_replace("libfaad/common.c"
  "33, 33, 38, 40, 40, 40, 41, 41, 37, 37, 37, 34\n"
  "33, 33, 38, 40, 40, 40, 41, 41, 37, 37, 37, 34, 34\n")
faad_compat_replace("libfaad/common.c" "if (sr_index < 12)" "if (sr_index < 13)")

faad_compat_replace("libfaad/specrec.c"
  "0, 0, 0, 36, 36, 37, 31, 31, 0, 0, 0, 0\n"
  "0, 0, 0, 36, 36, 37, 31, 31, 0, 0, 0, 0, 0\n")
faad_compat_replace("libfaad/specrec.c"
  "0, 0, 0, 35, 35, 37, 30, 30, 0, 0, 0, 0\n"
  "0, 0, 0, 35, 35, 37, 30, 30, 0, 0, 0, 0, 0\n")
faad_compat_replace("libfaad/specrec.c"
  "40, 40, 45, 49, 49, 49, 46, 46, 42, 42, 42, 40\n"
  "40, 40, 45, 49, 49, 49, 46, 46, 42, 42, 42, 40, 40\n")
faad_compat_replace("libfaad/specrec.c"
  "41, 41, 47, 49, 49, 51, 47, 47, 43, 43, 43, 40\n"
  "41, 41, 47, 49, 49, 51, 47, 47, 43, 43, 43, 40, 40\n")
faad_compat_replace("libfaad/specrec.c"
  "12, 12, 12, 14, 14, 14, 15, 15, 15, 15, 15, 15\n"
  "12, 12, 12, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15\n")
faad_compat_replace("libfaad/specrec.c"
  "swb_offset_1024_8        /* 8000  */"
  "swb_offset_1024_8,       /* 8000  */\n    swb_offset_1024_8        /* 7350, standard 8 kHz band geometry */")
faad_compat_replace("libfaad/specrec.c"
  "swb_offset_128_8         /* 8000  */"
  "swb_offset_128_8,        /* 8000  */\n    swb_offset_128_8         /* 7350, standard 8 kHz band geometry */")
faad_compat_replace("libfaad/specrec.c"
  "0                        /* 8000  */"
  "0,                       /* 8000  */\n    0                        /* 7350: LD band geometry unsupported */")
faad_compat_replace("libfaad/specrec.c" "if (sf_index >= 12)"
  "/* TTPlayer: index 12 is valid for the core long/short windows, not LD. */\n    if (sf_index >= 13 || (sf_index == 12 && hDecoder->object_type == LD))")

# New LC7350 support must produce native 7350 Hz, not silently enter the old
# low-rate implicit-SBR upsampler. Keep every previously supported rate intact.
faad_compat_replace("libfaad/decoder.c"
  "if (*samplerate <= 24000 && (hDecoder->config.dontUpSampleImplicitSBR == 0))"
  "if (*samplerate <= 24000 && *samplerate != 7350 && (hDecoder->config.dontUpSampleImplicitSBR == 0))")
faad_compat_replace("libfaad/mp4.c"
  "if (mp4ASC->samplingFrequency <= 24000)"
  "if (mp4ASC->samplingFrequency <= 24000 && mp4ASC->samplingFrequency != 7350)")

# 7350/14700 SBR frequency tables have not been restored. Reject explicit SBR
# and in-band SBR for this core, rather than indexing old 12-entry SBR tables
# or silently decoding a high-frequency stream using 8 kHz table substitutes.
faad_compat_replace("libfaad/mp4.c"
  "uint32_t startpos = faad_get_processed_bits(ld);"
  "uint32_t startpos = faad_get_processed_bits(ld);\n    uint32_t ttp_core_rate = 0;")
faad_compat_replace("libfaad/mp4.c"
  "mp4ASC->samplingFrequency = get_sample_rate(mp4ASC->samplingFrequencyIndex);"
  "mp4ASC->samplingFrequency = get_sample_rate(mp4ASC->samplingFrequencyIndex);\n#ifdef SBR_DEC\n    ttp_core_rate = mp4ASC->samplingFrequency;\n#endif")
faad_compat_replace("libfaad/mp4.c"
  "    /* no SBR signalled, this could mean either implicit signalling or no SBR in this file */"
  [=[    /* TTPlayer: only native core decoding is supported at 7350 Hz. */
    if (mp4ASC->sbr_present_flag == 1 &&
        (ttp_core_rate == 7350 || mp4ASC->samplingFrequency == 7350 ||
         mp4ASC->samplingFrequency == 14700))
        return -2;

    /* no SBR signalled, this could mean either implicit signalling or no SBR in this file */]=])
faad_compat_replace("libfaad/syntax.c" [=[        if ((bs_extension_type == EXT_SBR_DATA) ||
            (bs_extension_type == EXT_SBR_DATA_CRC))
        {]=] [=[        if ((bs_extension_type == EXT_SBR_DATA) ||
            (bs_extension_type == EXT_SBR_DATA_CRC))
        {
            /* TTPlayer: do not approximate unimplemented SBR at this rate. */
            if (hDecoder->sf_index == 12)
                return 24;]=])
faad_compat_replace("libfaad/sbr_syntax.c" [=[    /* calculate the Master Frequency Table */
    sbr->k0]=] [=[    /* TTPlayer: guard the unimplemented 7350/14700 SBR frequency tables. */
    if (sbr->sample_rate == 7350 || sbr->sample_rate == 14700)
        return 1;

    /* calculate the Master Frequency Table */
    sbr->k0]=])
