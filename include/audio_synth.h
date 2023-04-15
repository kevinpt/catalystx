#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

#include "cstone/debug.h"

typedef struct {
  int16_t value;
  int8_t  exponent;  // Base-2 exponent of value to set binary point
} FixedPoint;


typedef enum {
  OSC_NONE = 0,
  OSC_SINE,
  OSC_SQUARE,
  OSC_SAWTOOTH,
  OSC_TRIANGLE,
  OSC_NOISE
} OscKind;

typedef struct {
  uint32_t  count;
  uint32_t  increment; // 0 == disabled
} SynthDDFS;


typedef enum {
  ADSR_IDLE = 0,
  ADSR_ATTACK,
  ADSR_DECAY,
  ADSR_SUSTAIN,
  ADSR_RELEASE
} ADSRState;

// FIXME: Curve type is obsolete
typedef enum {
  CURVE_LINEAR = 0,
  CURVE_ULAW,
  CURVE_LOG,
  CURVE_SPLINE
} ADSRCurve;

typedef struct {
  uint16_t attack;
  uint16_t decay;
  int16_t sustain;
  uint16_t release;
  ADSRCurve curve;
  int16_t spline_weight;
} SynthADSRCfg;


typedef struct {
  // Setup
  SynthADSRCfg cfg;

  // Current state
  uint32_t state_start_time;
  ADSRState state;
  int16_t release_start_level;
  int16_t output;

  // Control
  uint16_t gate       : 1;
  uint16_t drone      : 1;

  // Control state
  uint16_t prev_gate  : 1;  // Allow edge detection of gate
  uint16_t drone_on   : 1;
  uint16_t reserved   : 12;
} SynthADSR;


typedef struct {
  uint32_t  frequency;
  OscKind   kind;

  SynthDDFS ddfs;
  int16_t   output;
  uint8_t   prev_quadrant : 2;
  uint8_t   quadrant      : 2;
  uint8_t   reserved      : 4;
} SynthOscillator;


typedef struct {
  SynthOscillator osc;
  SynthOscillator lfo;
  SynthADSR adsr;
  uint16_t lpf_cutoff_freq; // 0 == bypass LPF
  int16_t modulate_freq;    // 0 == disabled
  int16_t modulate_amp;     // 0 == disabled
  int16_t modulate_cutoff;  // 0 == disabled
  uint8_t key;
  uint8_t instrument;
} SynthVoice;

typedef struct  {
  uint32_t osc_freq;
  OscKind   osc_kind;

  uint32_t lfo_freq;
  OscKind   lfo_kind;

  SynthADSRCfg  adsr;

  uint16_t lpf_cutoff_freq; // 0 == bypass LPF
  int16_t modulate_freq;    // 0 == disabled
  int16_t modulate_amp;     // 0 == disabled
  int16_t modulate_cutoff;  // 0 == disabled
} SynthVoiceCfg;

#define SYNTH_MAX_VOICES  16
#define SYNTH_MAX_KEYS    128
#define SYNTH_MAX_INSTRUMENTS 4

typedef enum {
  VOICES_IDLE = 0,
  VOICES_ACTIVE,
  VOICES_RELEASE
} VoiceState;

typedef struct {
  uint32_t    sample_rate;
  int16_t     attenuation;

  SynthVoice  voices[SYNTH_MAX_VOICES];
  int8_t      next_voice;
  VoiceState  voice_state;

//  int8_t      key_voices[SYNTH_MAX_KEYS];
  SynthVoiceCfg instruments[SYNTH_MAX_INSTRUMENTS];

  IQueue_int16_t *queue;
  int16_t    *next_buf;
  uint32_t    timestamp;
  uint32_t    sample_count;
  bool        marker;
} SynthState;


#define MIDI_C   0
#define MIDI_Cs  1
#define MIDI_D   2
#define MIDI_Ds  3
#define MIDI_E   4
#define MIDI_F   5
#define MIDI_Fs  6
#define MIDI_G   7
#define MIDI_Gs  8
#define MIDI_A   9
#define MIDI_As  10
#define MIDI_B   11


#define MIDI_NOTE(note, octave)  (((octave)+1)*12 + (note))

#define NOTE_C0   MIDI_NOTE(MIDI_C,   0)
#define NOTE_Cs0  MIDI_NOTE(MIDI_Cs,  0)
#define NOTE_D0   MIDI_NOTE(MIDI_D,   0)
#define NOTE_Ds0  MIDI_NOTE(MIDI_Ds,  0)
#define NOTE_E0   MIDI_NOTE(MIDI_E,   0)
#define NOTE_F0   MIDI_NOTE(MIDI_F,   0)
#define NOTE_Fs0  MIDI_NOTE(MIDI_Fs,  0)
#define NOTE_G0   MIDI_NOTE(MIDI_G,   0)
#define NOTE_Gs0  MIDI_NOTE(MIDI_Gs,  0)
#define NOTE_A0   MIDI_NOTE(MIDI_A,   0)
#define NOTE_As0  MIDI_NOTE(MIDI_As,  0)
#define NOTE_B0   MIDI_NOTE(MIDI_B,   0)

#define NOTE_C1   MIDI_NOTE(MIDI_C,   1)
#define NOTE_Cs1  MIDI_NOTE(MIDI_Cs,  1)
#define NOTE_D1   MIDI_NOTE(MIDI_D,   1)
#define NOTE_Ds1  MIDI_NOTE(MIDI_Ds,  1)
#define NOTE_E1   MIDI_NOTE(MIDI_E,   1)
#define NOTE_F1   MIDI_NOTE(MIDI_F,   1)
#define NOTE_Fs1  MIDI_NOTE(MIDI_Fs,  1)
#define NOTE_G1   MIDI_NOTE(MIDI_G,   1)
#define NOTE_Gs1  MIDI_NOTE(MIDI_Gs,  1)
#define NOTE_A1   MIDI_NOTE(MIDI_A,   1)
#define NOTE_As1  MIDI_NOTE(MIDI_As,  1)
#define NOTE_B1   MIDI_NOTE(MIDI_B,   1)

#define NOTE_C2   MIDI_NOTE(MIDI_C,   2)
#define NOTE_Cs2  MIDI_NOTE(MIDI_Cs,  2)
#define NOTE_D2   MIDI_NOTE(MIDI_D,   2)
#define NOTE_Ds2  MIDI_NOTE(MIDI_Ds,  2)
#define NOTE_E2   MIDI_NOTE(MIDI_E,   2)
#define NOTE_F2   MIDI_NOTE(MIDI_F,   2)
#define NOTE_Fs2  MIDI_NOTE(MIDI_Fs,  2)
#define NOTE_G2   MIDI_NOTE(MIDI_G,   2)
#define NOTE_Gs2  MIDI_NOTE(MIDI_Gs,  2)
#define NOTE_A2   MIDI_NOTE(MIDI_A,   2)
#define NOTE_As2  MIDI_NOTE(MIDI_As,  2)
#define NOTE_B2   MIDI_NOTE(MIDI_B,   2)

#define NOTE_C3   MIDI_NOTE(MIDI_C,   3)
#define NOTE_Cs3  MIDI_NOTE(MIDI_Cs,  3)
#define NOTE_D3   MIDI_NOTE(MIDI_D,   3)
#define NOTE_Ds3  MIDI_NOTE(MIDI_Ds,  3)
#define NOTE_E3   MIDI_NOTE(MIDI_E,   3)
#define NOTE_F3   MIDI_NOTE(MIDI_F,   3)
#define NOTE_Fs3  MIDI_NOTE(MIDI_Fs,  3)
#define NOTE_G3   MIDI_NOTE(MIDI_G,   3)
#define NOTE_Gs3  MIDI_NOTE(MIDI_Gs,  3)
#define NOTE_A3   MIDI_NOTE(MIDI_A,   3)
#define NOTE_As3  MIDI_NOTE(MIDI_As,  3)
#define NOTE_B3   MIDI_NOTE(MIDI_B,   3)

#define NOTE_C4   MIDI_NOTE(MIDI_C,   4)
#define NOTE_Cs4  MIDI_NOTE(MIDI_Cs,  4)
#define NOTE_D4   MIDI_NOTE(MIDI_D,   4)
#define NOTE_Ds4  MIDI_NOTE(MIDI_Ds,  4)
#define NOTE_E4   MIDI_NOTE(MIDI_E,   4)
#define NOTE_F4   MIDI_NOTE(MIDI_F,   4)
#define NOTE_Fs4  MIDI_NOTE(MIDI_Fs,  4)
#define NOTE_G4   MIDI_NOTE(MIDI_G,   4)
#define NOTE_Gs4  MIDI_NOTE(MIDI_Gs,  4)
#define NOTE_A4   MIDI_NOTE(MIDI_A,   4)
#define NOTE_As4  MIDI_NOTE(MIDI_As,  4)
#define NOTE_B4   MIDI_NOTE(MIDI_B,   4)

#define NOTE_C5   MIDI_NOTE(MIDI_C,   5)
#define NOTE_Cs5  MIDI_NOTE(MIDI_Cs,  5)
#define NOTE_D5   MIDI_NOTE(MIDI_D,   5)
#define NOTE_Ds5  MIDI_NOTE(MIDI_Ds,  5)
#define NOTE_E5   MIDI_NOTE(MIDI_E,   5)
#define NOTE_F5   MIDI_NOTE(MIDI_F,   5)
#define NOTE_Fs5  MIDI_NOTE(MIDI_Fs,  5)
#define NOTE_G5   MIDI_NOTE(MIDI_G,   5)
#define NOTE_Gs5  MIDI_NOTE(MIDI_Gs,  5)
#define NOTE_A5   MIDI_NOTE(MIDI_A,   5)
#define NOTE_As5  MIDI_NOTE(MIDI_As,  5)
#define NOTE_B5   MIDI_NOTE(MIDI_B,   5)

#define NOTE_C6   MIDI_NOTE(MIDI_C,   6)
#define NOTE_Cs6  MIDI_NOTE(MIDI_Cs,  6)
#define NOTE_D6   MIDI_NOTE(MIDI_D,   6)
#define NOTE_Ds6  MIDI_NOTE(MIDI_Ds,  6)
#define NOTE_E6   MIDI_NOTE(MIDI_E,   6)
#define NOTE_F6   MIDI_NOTE(MIDI_F,   6)
#define NOTE_Fs6  MIDI_NOTE(MIDI_Fs,  6)
#define NOTE_G6   MIDI_NOTE(MIDI_G,   6)
#define NOTE_Gs6  MIDI_NOTE(MIDI_Gs,  6)
#define NOTE_A6   MIDI_NOTE(MIDI_A,   6)
#define NOTE_As6  MIDI_NOTE(MIDI_As,  6)
#define NOTE_B6   MIDI_NOTE(MIDI_B,   6)

#define NOTE_C7   MIDI_NOTE(MIDI_C,   7)
#define NOTE_Cs7  MIDI_NOTE(MIDI_Cs,  7)
#define NOTE_D7   MIDI_NOTE(MIDI_D,   7)
#define NOTE_Ds7  MIDI_NOTE(MIDI_Ds,  7)
#define NOTE_E7   MIDI_NOTE(MIDI_E,   7)
#define NOTE_F7   MIDI_NOTE(MIDI_F,   7)
#define NOTE_Fs7  MIDI_NOTE(MIDI_Fs,  7)
#define NOTE_G7   MIDI_NOTE(MIDI_G,   7)
#define NOTE_Gs7  MIDI_NOTE(MIDI_Gs,  7)
#define NOTE_A7   MIDI_NOTE(MIDI_A,   7)
#define NOTE_As7  MIDI_NOTE(MIDI_As,  7)
#define NOTE_B7   MIDI_NOTE(MIDI_B,   7)

#define NOTE_C8   MIDI_NOTE(MIDI_C,   8)
#define NOTE_Cs8  MIDI_NOTE(MIDI_Cs,  8)
#define NOTE_D8   MIDI_NOTE(MIDI_D,   8)
#define NOTE_Ds8  MIDI_NOTE(MIDI_Ds,  8)
#define NOTE_E8   MIDI_NOTE(MIDI_E,   8)
#define NOTE_F8   MIDI_NOTE(MIDI_F,   8)
#define NOTE_Fs8  MIDI_NOTE(MIDI_Fs,  8)
#define NOTE_G8   MIDI_NOTE(MIDI_G,   8)
#define NOTE_Gs8  MIDI_NOTE(MIDI_Gs,  8)
#define NOTE_A8   MIDI_NOTE(MIDI_A,   8)
#define NOTE_As8  MIDI_NOTE(MIDI_As,  8)
#define NOTE_B8   MIDI_NOTE(MIDI_B,   8)






#ifdef __cplusplus
extern "C" {
#endif


uint16_t midi_note_freq(uint16_t note_ix);
int16_t frequency_scale_factor(uint16_t ref_freq, uint16_t peak_freq);
uint32_t ddfs_increment(uint32_t sample_rate, uint32_t target_freq, uint32_t target_scale);

void synth_init(SynthState *synth, uint32_t sample_rate, size_t queue_size);
void synth_set_marker(SynthState *synth, bool enable);
void synth_set_freq(SynthState *synth, int inst, uint32_t frequency);
void synth_set_waveform(SynthState *synth, int inst, OscKind kind);
void synth_set_adsr_curve(SynthState *synth, int inst, ADSRCurve curve);

void synth_oscillator_init(SynthState *synth, SynthOscillator *osc, uint32_t frequency, OscKind kind);
void synth_voice_init(SynthState *synth, int voice, SynthVoiceCfg *cfg);

int16_t oscillator_step_output(SynthOscillator *osc, uint32_t increment);
size_t synth_gen_samples(SynthState *synth, size_t gen_count);

void synth_instrument_init(SynthState *synth, int inst, SynthVoiceCfg *cfg);
int synth_instrument_add(SynthState *synth, SynthVoiceCfg *cfg);

SynthVoice *synth_add_voice(SynthState *synth, uint8_t key, int inst);
//void synth_end_voice(SynthState *synth, uint8_t key);
void synth_press_key(SynthState *synth, uint8_t key, int inst);
void synth_release_key(SynthState *synth, uint8_t key, int inst);

void adsr_init(SynthADSR *adsr);
int16_t adsr_step_output(SynthADSR *adsr, uint32_t now);

#if 1
bool update_sample_dev_state(SampleDevice *sdev, SynthState *audio_synth);
#else
static inline bool update_sample_dev_state(SampleDevice *sdev, SynthState *audio_synth) {
  /*
  The synth has no active voices so we can disable the DMA to save on processor load.
  We need to fill the DMA buffer with 0-samples before disabling DMA. This will ensure
  we don't get garbage playback or speaker pops when something is sequenced wrong. At
  this point all voices are idle so this invocation will generate a half buffer of 0's.
  We need to keep DMA running for one more cycle to generate the second half. Then we
  can invoke the SDEV_OP_SHUTDOWN_END command to terminate DMA after our first half
  buffer of 0's has been partially sent.

  Note that when the DMA is disabled by SDEV_OP_SHUTDOWN_END it will trigger a TC
  interrupt one last time to indicate end of transfer. That will cause a third
  invocation of this function.
  */

  if(audio_synth->voice_state == VOICES_IDLE) {
    DPRINT("IDLE VOICES %p state=%d", sdev, sdev->state);
    switch(sdev->state) {
    case SDEV_ACTIVE:
      DPRINT("ACTIVE-> SHUTDOWN");
      sdev_ctl(sdev, SDEV_OP_DEACTIVATE, NULL, 0);
      // Continue to 0-fill first half of buffer
      break;
    case SDEV_SHUTDOWN:
      DPRINT("SHUTDOWN -> END");
      sdev_ctl(sdev, SDEV_OP_SHUTDOWN_END, NULL, 0);
      // Continue to 0-fill second half of buffer
      break;
    case SDEV_INACTIVE:
      // Third invocation caused by DMA end of transfer
      DPRINT("INACTIVE");
      return false; // Abort output function; nothing to do
      break;
    default:
      DPRINT("INVALID !!!");
      sdev->state = SDEV_ACTIVE;
      sdev_ctl(sdev, SDEV_OP_DEACTIVATE, NULL, 0);
      break;
    }
  }

  return true;
}
#endif

#ifdef __cplusplus
}
#endif

#endif // AUDIO_SYNTH_H
