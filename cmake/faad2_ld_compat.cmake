# TTPlayer Rebuild, 2026-10-30: real AAC LD 480/512 frames and rates.
# Applied after the core-frequency patch; affect build-tree copies only.
# AAC LD does not use the ordinary AAC-LC implicit-SBR rate guess.
faad_compat_replace("libfaad/mp4.c"
  "if (mp4ASC->sbr_present_flag == (char)-1)"
  "if (mp4ASC->sbr_present_flag == (char)-1 && mp4ASC->objectTypeIndex != LD)")

# ISO/IEC 14496-3:2009 table 4.82: when a rate-dependent table is absent,
# use the nearest defined table. LD defines the 22.05/24, 32 and 44.1/48 kHz
# families. Keep the actual core clock; only the local table index is mapped.
# Index 12 (7350) uses the same LD family as a valid 8 kHz core; only its
# presentation clock changes. It does not enter the AAC-LC SBR heuristic.
faad_compat_replace("libfaad/specrec.c" [=[    if (sf_index >= 13 || (sf_index == 12 && hDecoder->object_type == LD))
        return 32;

    switch (ics->window_sequence)]=] [=[    if (sf_index >= 13)
        return 32;

    if (hDecoder->object_type == LD)
    {
        if (sf_index < 3) sf_index = 3;
        if (sf_index > 7) sf_index = 7;
    }

    switch (ics->window_sequence)]=])

faad_compat_replace("libfaad/common.c" [=[        if (sr_index < 3 || sr_index > 7 ||
            (frame_len != 480 && frame_len != 512))
            return 0;
        if (frame_len == 480 && sr_index >= 6)
            return 30;
        return ld_512[sr_index - 3];]=] [=[        const uint8_t ld_index = sr_index < 3 ? 3 : (sr_index > 7 ? 7 : sr_index);
        if (sr_index >= 13 || (frame_len != 480 && frame_len != 512))
            return 0;
        /* Match LD's nearest available spectral family, not its PCM clock. */
        if (frame_len == 480 && ld_index >= 6)
            return 30;
        return ld_512[ld_index - 3];]=])

# LD480 and ordinary LTP960 use a radix-5 forward FFT. The butterfly's
# c4/c5 combinations are independent of transform direction; output signs
# below them already select forward/backward. Swapping ti11/ti12 here made
# the forward transform disagree with the DFT (inverse AAC decoding did not).
# Keep both ido branches consistent, including the otherwise unused branch.
faad_compat_replace("libfaad/cfft.c" [=[                ComplexMult(&RE(c4), &RE(c5),
                    ti12, ti11, RE(t5), RE(t4));
                ComplexMult(&IM(c4), &IM(c5),
                    ti12, ti11, IM(t5), IM(t4));]=] [=[                ComplexMult(&RE(c5), &RE(c4),
                    ti11, ti12, RE(t5), RE(t4));
                ComplexMult(&IM(c5), &IM(c4),
                    ti11, ti12, IM(t5), IM(t4));]=])
faad_compat_replace("libfaad/cfft.c" [=[                    ComplexMult(&RE(c4), &RE(c5),
                        ti12, ti11, RE(t5), RE(t4));
                    ComplexMult(&IM(c4), &IM(c5),
                        ti12, ti11, IM(t5), IM(t4));]=] [=[                    ComplexMult(&RE(c5), &RE(c4),
                        ti11, ti12, RE(t5), RE(t4));
                    ComplexMult(&IM(c5), &IM(c4),
                        ti11, ti12, IM(t5), IM(t4));]=])
