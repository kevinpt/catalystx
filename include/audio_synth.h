#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

typedef struct {
  int16_t value;
  int8_t  exponent;  // Base-2 exponent of value to set binary point
} FixedPoint;


typedef enum {
  OSC_SINE = 0,
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

typedef enum {
  CURVE_LINEAR = 0,
  CURVE_ULAW,
  CURVE_LOG
} ADSRCurve;

typedef struct {
  uint16_t attack;
  uint16_t decay;
  int16_t sustain;
  uint16_t release;
  ADSRCurve curve;
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
#define SYNTH_MAX_INSTRUMENTS 2
typedef struct {
  uint32_t sample_rate;
  int16_t attenuation;

  SynthVoice voices[SYNTH_MAX_VOICES];
  int8_t next_voice;

  int8_t key_voices[SYNTH_MAX_KEYS];
  SynthVoiceCfg instruments[SYNTH_MAX_INSTRUMENTS];


  IQueue_int16_t *queue;
  int16_t *next_buf;
  uint32_t timestamp;
  uint32_t sample_count;
} SynthState;


#ifdef __cplusplus
extern "C" {
#endif


int16_t frequency_scale_factor(uint16_t ref_freq, uint16_t peak_freq);
uint32_t ddfs_increment(uint32_t sample_rate, uint32_t target_freq, uint32_t target_scale);

void synth_init(SynthState *synth, uint32_t sample_rate, size_t queue_size);
void synth_set_freq(SynthState *synth, int voice, uint32_t frequency);
void synth_set_waveform(SynthState *synth, int voice, OscKind kind);
void synth_set_adsr_curve(SynthState *synth, int voice, ADSRCurve curve);

void synth_oscillator_init(SynthState *synth, SynthOscillator *osc, uint32_t frequency, OscKind kind);
void synth_voice_init(SynthState *synth, int voice, SynthVoiceCfg *cfg);

int16_t oscillator_step_output(SynthOscillator *osc, uint32_t increment);
void synth_gen_samples(SynthState *synth, size_t gen_count);

void synth_instrument_init(SynthState *synth, int instrument, SynthVoiceCfg *cfg);
SynthVoice *synth_add_voice(SynthState *synth, uint8_t key, int instrument);
//void synth_end_voice(SynthState *synth, uint8_t key);
void synth_press_key(SynthState *synth, uint8_t key, int instrument);
void synth_release_key(SynthState *synth, uint8_t key);

void adsr_init(SynthADSR *adsr);
int16_t adsr_step_output(SynthADSR *adsr, uint32_t now);


#ifdef __cplusplus
}
#endif

#endif // AUDIO_SYNTH_H
