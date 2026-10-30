# TTPlayer Rebuild, 2026-10-30: retain explicit core clocks separately from
# the spectral table family. Public FAAD and AddIn layouts remain unchanged.
faad_compat_replace("libfaad/structs.h"
  "    uint8_t latm_header_present;\n    uint8_t sf_index;"
  "    uint8_t latm_header_present;\n    uint32_t ttp_core_rate; /* exact ASC core clock, zero for raw transport */\n    uint8_t sf_index;")

faad_compat_replace("libfaad/mp4.c" [=[        if (get_sample_rate(get_sr_index(*frequency)) == *frequency)
            *index = get_sr_index(*frequency);
        else if (!sbr_output || (*frequency & 1) ||]=] [=[        if (!sbr_output || get_sample_rate(get_sr_index(*frequency)) == *frequency)
            *index = get_sr_index(*frequency);
        else if ((*frequency & 1) ||]=])
faad_compat_replace("libfaad/mp4.c"
  "    mp4ASC->samplingFrequency = get_sample_rate(mp4ASC->samplingFrequencyIndex);\n#ifdef SBR_DEC"
  "    /* Retain the explicit clock; sf_index only selects spectral tables. */\n#ifdef SBR_DEC")
faad_compat_replace("libfaad/mp4.c"
  "if (mp4ASC->samplingFrequency <= 24000 && mp4ASC->samplingFrequency != 7350)"
  "if (mp4ASC->samplingFrequency <= 24000 && mp4ASC->samplingFrequency != 7350 &&\n            mp4ASC->samplingFrequency == get_sample_rate(mp4ASC->samplingFrequencyIndex))")
faad_compat_replace("libfaad/mp4.c"
  "(ttp_core_rate == 7350 || mp4ASC->samplingFrequency == 7350 ||"
  "(ttp_core_rate != get_sample_rate(get_sr_index(ttp_core_rate)) ||\n         ttp_core_rate == 7350 || mp4ASC->samplingFrequency == 7350 ||")

faad_compat_replace("libfaad/decoder.c"
  "    hDecoder->sf_index = get_sr_index(hDecoder->config.defSampleRate);"
  "    hDecoder->ttp_core_rate = 0;\n    hDecoder->sf_index = get_sr_index(hDecoder->config.defSampleRate);")
faad_compat_replace("libfaad/decoder.c"
  "    hDecoder->channelConfiguration = mp4ASC.channelsConfiguration;"
  [=[    hDecoder->ttp_core_rate = mp4ASC.samplingFrequency;
#ifdef SBR_DEC
    if (((hDecoder->sbr_present_flag == 1) && !hDecoder->downSampledSBR) ||
        hDecoder->forceUpSampling == 1)
        hDecoder->ttp_core_rate /= 2;
#endif
    hDecoder->channelConfiguration = mp4ASC.channelsConfiguration;]=])
faad_compat_replace("libfaad/decoder.c"
  "    hInfo->samplerate = get_sample_rate(hDecoder->sf_index);"
  "    hInfo->samplerate = hDecoder->ttp_core_rate ? hDecoder->ttp_core_rate :\n        get_sample_rate(hDecoder->sf_index);")
faad_compat_replace("libfaad/syntax.c"
  "if (hDecoder->sf_index == 12)\n                return 24;"
  "if (hDecoder->sf_index == 12 ||\n                (hDecoder->ttp_core_rate && hDecoder->ttp_core_rate != get_sample_rate(hDecoder->sf_index)))\n                return 24;")
