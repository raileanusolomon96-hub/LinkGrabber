# Parameter Specification (Draft)

**Status:** Draft - Awaiting UI mockup for full specification
**Created:** 2026-11-07
**Source:** Quick capture during ideation

This is a lightweight specification to enable parallel DSP research.
Full specification will be generated from finalized UI mockup.

LinkGrabber does no audio processing. Audio passes through unmodified; the plugin only downloads and converts files. There are no audio parameters.

## Parameters

### format
- **Type:** Choice
- **Choices:** MP3 320 kbps, WAV 24-bit
- **Default:** MP3 320 kbps
- **DSP Purpose:** None (no audio processing). Selects the output format of the download/convert step.

## Next Steps

- [ ] Complete UI mockup workflow (/dream → option 3)
- [ ] Finalize design and generate full parameter-spec.md
- [ ] Validate consistency between draft and final spec
