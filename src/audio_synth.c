#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cstone/debug.h"
#include "cstone/iqueue_int16_t.h"
#include "audio_synth.h"
#include "util/random.h"
#include "util/intmath.h"

//#define PROFILE_AUDIO
#ifdef PROFILE_AUDIO
#  include "cstone/profile.h"
static uint32_t s_prof_id;
#endif


static RandomState s_audio_prng;


// Scale is fixed point in Q0.15 format covering range [-1.0, +1.0)
static uint16_t octave_scale(int16_t n, int16_t scale) {
#define SCALE_FP_EXP     15
#define POW2_TABLE_BITS  5
#define POW2_MASK        ((1u << POW2_TABLE_BITS) - 1)
#define POW2_0P5         (1 << (SCALE_FP_EXP-1))
  // 2^(n/32) * (2^15)  for n = [0, 1]
  static const uint16_t pow2_octave_table[] = {
  // 0.0 0.03125
  32768, 33486, 34219, 34968, 35734, 36516, 37316, 38133,
  38968, 39821, 40693, 41584, 42495, 43425, 44376, 45348,
  46341, 47356, 48393, 49452, 50535, 51642, 52773, 53928,
  55109, 56316, 57549, 58809, 60097, 61413, 62757, 64132,
  // 1.0 (-1 to fit into uint16_t)
  65535
  };

  // 2^(n/32) * (2^15)  for n = [0, 0.03125]
  static const uint16_t pow2_fine_table[] = {
  // 0.0
  32768, 32790, 32812, 32835, 32857, 32879, 32901, 32924,
  32946, 32968, 32991, 33013, 33035, 33058, 33080, 33102,
  33125, 33147, 33170, 33192, 33215, 33237, 33260, 33282,
  33305, 33327, 33350, 33372, 33395, 33418, 33440, 33463,
  // 0.03125
  33486
  };

  uint16_t scale_abs = scale < 0 ? -(scale+1) : scale;
  uint16_t ix = scale_abs >> (SCALE_FP_EXP - POW2_TABLE_BITS); // Take upper 5 bits
  int32_t pow2 = pow2_octave_table[ix];
  int32_t pow2_b;

  if(ix == 0) { // Improve resolution between scale of 0.0 and 0.03
    ix = (scale_abs >> (SCALE_FP_EXP - POW2_TABLE_BITS - POW2_TABLE_BITS)) & POW2_MASK; // Take next 5 bits

    pow2 = pow2_fine_table[ix];
    pow2_b = pow2_fine_table[ix+1];
    scale_abs <<= POW2_TABLE_BITS;  // Adjust for interpolation

  } else {  // Coarser resolution for rest of octave
    pow2_b = pow2_octave_table[ix+1];
  }

  // Interpolate between points
  pow2 = (((pow2_b - pow2) * scale_abs) >> SCALE_FP_EXP) + pow2;

  if(scale >= 0)   // n * 2^|scale| + 0.5
    return (((int32_t)n * pow2) + POW2_0P5) >> SCALE_FP_EXP;
  else  // (n+0.5) / 2^|scale|
    return ((((int32_t)n << SCALE_FP_EXP) + POW2_0P5) / pow2);
}


// Compute scale argument for :c:func:`octave_scale`
// Result in Q0.15 format
int16_t frequency_scale_factor(uint16_t ref_freq, uint16_t peak_freq) {
  return log2_fixed(peak_freq, 0) - log2_fixed(ref_freq, 0);
}


uint32_t ddfs_increment(uint32_t sample_rate, uint32_t target_freq, uint32_t target_scale) {
  return ((uint64_t)target_freq * (1ull << 32)) / (sample_rate * target_scale);
}


void synth_init(SynthState *synth, uint32_t sample_rate, size_t queue_size) {
  memset(synth, 0, sizeof *synth);
  synth->sample_rate = sample_rate;
  synth->queue = iqueue_alloc__int16_t(queue_size, /*overwrite*/false);

  uint32_t seed = random_from_system();
  random_init(&s_audio_prng, seed);

  synth->attenuation = (int32_t)INT16_MAX * 1 / 3;

  SynthVoiceCfg voice_cfg = {
    .osc_freq = 0,
    .osc_kind = OSC_SINE,

    .lfo_freq = 0,
    .lfo_kind = OSC_SINE,

    .adsr.attack = 200,
    .adsr.decay = 700,
    .adsr.sustain = 20000,
    .adsr.release = 300,
    .adsr.curve = CURVE_ULAW,

    .lpf_cutoff_freq  = 1000,
    .modulate_freq    = 0,
    .modulate_amp     = 0,
    .modulate_cutoff  = 0
  };

  for(int i = 0; i < SYNTH_MAX_INSTRUMENTS; i++) {
    synth_instrument_init(synth, i, &voice_cfg);
  }

  for(int i = 0; i < SYNTH_MAX_KEYS; i++) {
    synth->key_voices[i] = -1;
  }

#ifdef PROFILE_AUDIO
  s_prof_id = profile_add(0, "synth spline");
#endif
}


void synth_set_marker(SynthState *synth, bool enable) {
  synth->marker = enable;
}


void synth_set_freq(SynthState *synth, int voice, uint32_t frequency) {
  SynthVoice *vox = &synth->voices[voice];

  if(frequency == 0) {
    vox->osc.ddfs.increment = 0;
  } else {
    vox->osc.ddfs.increment = ddfs_increment(synth->sample_rate, frequency, 4);
//    DPRINT("CH%d incr = %" PRIu32 "\n", channel, chan->ddfs.increment);
  }

  vox->osc.frequency = frequency / 4;
}

void synth_set_waveform(SynthState *synth, int voice, OscKind kind) {
  SynthVoiceCfg *vox = &synth->instruments[voice]; //&synth->voices[voice];
  vox->osc_kind = kind;
}

void synth_set_adsr_curve(SynthState *synth, int voice, ADSRCurve curve) {
  SynthVoiceCfg *vox = &synth->instruments[voice]; //&synth->voices[voice];
  vox->adsr.curve = curve;

}


void synth_oscillator_init(SynthState *synth, SynthOscillator *osc, uint32_t frequency, OscKind kind) {

  if(frequency == 0) {
    osc->ddfs.increment = 0;
  } else {
    osc->ddfs.increment = ddfs_increment(synth->sample_rate, frequency, 1);
//    DPRINT("CH%d incr = %" PRIu32 "\n", channel, chan->ddfs.increment);
  }

  osc->kind = kind;
  osc->frequency = frequency;
}


void synth_voice_init(SynthState *synth, int voice, SynthVoiceCfg *cfg) {
  SynthVoice *vox = &synth->voices[voice];

  synth_oscillator_init(synth, &vox->osc, cfg->osc_freq, cfg->osc_kind);
  synth_oscillator_init(synth, &vox->lfo, cfg->lfo_freq, cfg->lfo_kind);

  memcpy(&vox->adsr.cfg, &cfg->adsr, sizeof cfg->adsr);

  vox->lpf_cutoff_freq = cfg->lpf_cutoff_freq;
  vox->modulate_freq = cfg->modulate_freq;
  vox->modulate_amp = cfg->modulate_amp;
  vox->modulate_cutoff = cfg->modulate_cutoff;
}




#define SINE_TABLE_BITS   8   // 256-entry table

const int16_t s_sine_table[256] = {
  0, 201, 402, 603, 804, 1005, 1206, 1406,
  1607, 1808, 2009, 2209, 2410, 2610, 2811, 3011,
  3211, 3411, 3611, 3811, 4011, 4210, 4409, 4608,
  4807, 5006, 5205, 5403, 5601, 5799, 5997, 6195,
  6392, 6589, 6786, 6982, 7179, 7375, 7571, 7766,
  7961, 8156, 8351, 8545, 8739, 8932, 9126, 9319,
  9511, 9703, 9895, 10087, 10278, 10469, 10659, 10849,
  11038, 11227, 11416, 11604, 11792, 11980, 12166, 12353,
  12539, 12724, 12909, 13094, 13278, 13462, 13645, 13827,
  14009, 14191, 14372, 14552, 14732, 14911, 15090, 15268,
  15446, 15623, 15799, 15975, 16150, 16325, 16499, 16672,
  16845, 17017, 17189, 17360, 17530, 17699, 17868, 18036,
  18204, 18371, 18537, 18702, 18867, 19031, 19194, 19357,
  19519, 19680, 19840, 20000, 20159, 20317, 20474, 20631,
  20787, 20942, 21096, 21249, 21402, 21554, 21705, 21855,
  22004, 22153, 22301, 22448, 22594, 22739, 22883, 23027,
  23169, 23311, 23452, 23592, 23731, 23869, 24006, 24143,
  24278, 24413, 24546, 24679, 24811, 24942, 25072, 25201,
  25329, 25456, 25582, 25707, 25831, 25954, 26077, 26198,
  26318, 26437, 26556, 26673, 26789, 26905, 27019, 27132,
  27244, 27355, 27466, 27575, 27683, 27790, 27896, 28001,
  28105, 28208, 28309, 28410, 28510, 28608, 28706, 28802,
  28897, 28992, 29085, 29177, 29268, 29358, 29446, 29534,
  29621, 29706, 29790, 29873, 29955, 30036, 30116, 30195,
  30272, 30349, 30424, 30498, 30571, 30643, 30713, 30783,
  30851, 30918, 30984, 31049, 31113, 31175, 31236, 31297,
  31356, 31413, 31470, 31525, 31580, 31633, 31684, 31735,
  31785, 31833, 31880, 31926, 31970, 32014, 32056, 32097,
  32137, 32176, 32213, 32249, 32284, 32318, 32350, 32382,
  32412, 32441, 32468, 32495, 32520, 32544, 32567, 32588,
  32609, 32628, 32646, 32662, 32678, 32692, 32705, 32717,
  32727, 32736, 32744, 32751, 32757, 32761, 32764, 32766
};


static int16_t saturate16(int32_t v) {
  if(v > INT16_MAX)       v = INT16_MAX;
  else if(v < INT16_MIN)  v = INT16_MIN;

  return v;
}


static int32_t scale_cv_unipolar(int16_t cv, int16_t cv_min, int16_t cv_max) {
  int32_t level = cv;
  int16_t range = cv_max - cv_min;

//  level = (level - INT16_MIN) / 2; // Scale = [0, +1)
  level = (((level-INT16_MIN) * (int32_t)range) >> (15+1)) + cv_min;

  return level;
}


int16_t oscillator_step_output(SynthOscillator *osc, uint32_t increment) {
  int16_t sample = 0;
  uint8_t quadrant = 0;

  if(increment == 0)
    increment = osc->ddfs.increment;

  osc->ddfs.count += increment;
  quadrant = osc->ddfs.count >> (32 - 2); // Upper 2 bits are quadrant

  switch(osc->kind) {
  case OSC_SINE:
    {
      /*      __          __                         _
        +    /  \        /  \                       /
        ___ / __ \  __  / __ \ ___             ___ / ___
                  \    /
        -   |  |   \__/                       s_sine_table[]
            |  |  |
            |  |  |  |
            Q0 Q1 Q2 Q3
      */
      unsigned table_ix = osc->ddfs.count >> (32 - SINE_TABLE_BITS - 2);
//      quadrant = table_ix >> SINE_TABLE_BITS;  // Upper 2 bits are quadrant
      table_ix &= (1ul << SINE_TABLE_BITS) - 1; // Remove quadrant

      if(!(quadrant & 0x01)) // Q0 & Q2
        sample = s_sine_table[table_ix];
      else  // Reverse index for Q1 & Q3
        sample = s_sine_table[(1ul << SINE_TABLE_BITS) - 1 - table_ix];

      //if(quadrant >= 2)
      if(quadrant & 0x02) // Flip sign in Q2 & Q3
        sample = -sample;
    }
    break;

  case OSC_TRIANGLE:
    {
      // Extract 15-bits of ramp plus 2-bits of quadrant from DDFS counter
      unsigned ramp = osc->ddfs.count >> (32 - 15 - 2);
//      quadrant = ramp >> 15;  // Upper 2 bits are quadrant
      ramp &= (1ul << 15) - 1; // Remove quadrant

      if(!(quadrant & 0x01)) // Q0 & Q2
        sample = ramp;
      else  // Down slope for Q1 & Q3
        sample = INT16_MAX - ramp;

      if(quadrant & 0x02) // Flip sign in Q2 & Q3
        sample = -sample;
    }
    break;

  case OSC_SAWTOOTH:
    sample = (int16_t)(osc->ddfs.count >> (32 - 16));
    break;

  case OSC_SQUARE:
    sample = (osc->ddfs.count < UINT32_MAX/2) ? INT16_MAX : INT16_MIN;
    break;

  case OSC_NOISE:
    //sample = osc->output + (int16_t)random_range32(&s_audio_prng, INT16_MIN, INT16_MAX);
    sample = saturate16((int32_t)osc->output + random_range32(&s_audio_prng, INT16_MIN, INT16_MAX));
    break;
  }

  //osc->zero_crossing = ((osc->output ^ sample) & 0x1000) != 0;
  osc->prev_quadrant  = osc->quadrant;
  osc->quadrant       = quadrant;
  osc->output         = sample;

  return sample;
}


static inline bool oscillator__zero_cross_rise(SynthOscillator *osc) {
  return osc->quadrant == 0 && osc->prev_quadrant == 3;
}


static inline bool oscillator__zero_cross_fall(SynthOscillator *osc) {
  return osc->quadrant == 2 && osc->prev_quadrant == 1;
}


static inline bool voice_is_active(SynthVoice *vox) {
  return vox->adsr.state != ADSR_IDLE;
}


static int32_t voice__step_output(SynthVoice *vox, uint32_t sample_rate, bool *update_marker) {
  bool lfo_active = vox->modulate_freq > 0 || vox->modulate_amp > 0;

  int32_t lfo_sample = oscillator_step_output(&vox->lfo, 0);

  uint32_t osc_increment = 0;


  if(vox->modulate_freq > 0) {
    if(oscillator__zero_cross_rise(&vox->lfo))
      *update_marker = true;
#if 0

    uint32_t min_freq = vox->osc.frequency - vox->modulate_freq;

    uint32_t lfo_level = ((uint32_t)lfo_sample - INT16_MIN) / 2; // Scale = [0, +1)
    uint32_t target_freq = ((lfo_level * (2*vox->modulate_freq)) >> 15) + min_freq;
#else

    int16_t scale = ((int32_t)vox->modulate_freq * lfo_sample) >> 15;
    uint32_t target_freq = octave_scale(vox->osc.frequency, scale);
#endif
    osc_increment = ddfs_increment(sample_rate, target_freq, 1);
  }

  int32_t osc_sample = oscillator_step_output(&vox->osc, osc_increment);

  // Generate marker from voice oscillator if LFO is inactive
  if(!lfo_active && oscillator__zero_cross_rise(&vox->osc))
    *update_marker = true;


  // Apply envelope
  int32_t osc_w_env = (osc_sample * (int32_t)vox->adsr.output) >> 15;
#if 0
  static int ocount = 0;
  if(ocount++ == 311) {
    ocount = 0;
    printf("## OSC: %ld * %ld --> %ld\n", osc_sample, (int32_t)vox->adsr.output, osc_w_env);
  }
#endif
  osc_sample = osc_w_env;

  // Apply VCA modulation
  if(vox->modulate_amp > 0) {
    if(vox->modulate_freq == 0 && oscillator__zero_cross_rise(&vox->lfo))
      *update_marker = true;

#if 0
    static int pcount = 0;
    if(pcount++ == 100) {
      pcount = 0;
      printf("## LFO: %ld \t@ %lu\n", lfo_sample, synth->timestamp);
    }
#endif
    // Convert LFO from [-1,+1) to [modulate_amp, +1)
//        lfo_sample = (lfo_sample - INT16_MIN) / 2;  // Scale = [0, +1)
//        lfo_sample = ((lfo_sample * vox->modulate_amp) >> 15) + (INT16_MAX - vox->modulate_amp);
    lfo_sample = scale_cv_unipolar(lfo_sample, vox->modulate_amp, INT16_MAX);
    int32_t osc_new = (osc_sample * lfo_sample) >> 15;

#if 0
    static int pcount = 0;
    if(pcount++ == 311) {
      pcount = 0;
      printf("## LFO: %ld * %ld\t--> %ld\t@ %lu\n", lfo_sample, osc_sample, osc_new, synth->timestamp);
    }
#endif
    osc_sample = osc_new;
  }

  return osc_sample;
}


static int32_t compress_audio(int32_t sample, int16_t threshold) {
  int32_t abs_sample = sample < 0 ? -sample : sample;

  if(abs_sample <= threshold)
    return sample;

  abs_sample = (abs_sample - threshold) / 2 + threshold;

  return sample < 0 ? -abs_sample : abs_sample;
}


size_t synth_gen_samples(SynthState *synth, size_t gen_count) {
  size_t q_count = iqueue_count__int16_t(synth->queue);
  if(q_count >= gen_count)  // Nothing to do
    return q_count;

  // We don't need to keep generating samples when no voices are active
  int active_voices = 0;
  int release_voices = 0;
  for(int voice = 0; voice < SYNTH_MAX_VOICES; voice++) {
    SynthADSR *adsr = &synth->voices[voice].adsr;

    if(adsr->state == ADSR_RELEASE)
      release_voices++;
    else if(adsr->state != ADSR_IDLE || adsr->drone_on || adsr->gate)
      active_voices++;
  }

  if(active_voices == 0) {
    if(release_voices == 0)
      synth->voice_state = VOICES_IDLE;
    else
      synth->voice_state = VOICES_RELEASE;
  } else {
    synth->voice_state = VOICES_ACTIVE;
  }

  if(synth->voice_state == VOICES_IDLE) { // Let queue drain remaining samples
    DPRINT("0 voices, q_count: %u", (unsigned)q_count);
    return q_count;
  }


  gen_count -= q_count;
  int32_t mixed_samples;
  int16_t sample;

  uint32_t samples_per_ms = synth->sample_rate / 1000;

  //for(size_t j = 0; j < gen_count; j++) {
  while(gen_count--) {
    if(synth->sample_count == 0) {  // Update all ADSR envelopes
      for(int voice = 0; voice < SYNTH_MAX_VOICES; voice++) {
        SynthADSR *adsr = &synth->voices[voice].adsr;
        ADSRState prev_state = adsr->state;
        adsr_step_output(adsr, synth->timestamp);

        if(adsr->state == ADSR_IDLE && prev_state != ADSR_IDLE) {
          DPRINT("ADSR end: voice=%d", voice);
        }
      }
    }

    // Generate samples for all active voices
    mixed_samples = 0;
    bool first_voice = true;
    bool apply_marker = false;
    for(int voice = 0; voice < SYNTH_MAX_VOICES; voice++) {
      SynthVoice *vox = &synth->voices[voice];
      if(!voice_is_active(vox)) continue;

      bool update_marker = false;
      int32_t osc_sample = voice__step_output(vox, synth->sample_rate, &update_marker);
      mixed_samples += osc_sample;

      if(synth->marker && first_voice) {
        if(update_marker)
          apply_marker = true;
        first_voice = false;
      }
    }

    if(!apply_marker) {
      mixed_samples = (mixed_samples * synth->attenuation) >> 15;

      mixed_samples = compress_audio(mixed_samples, synth->attenuation);
      mixed_samples = compress_audio(mixed_samples, INT16_MAX-5000);
      mixed_samples = compress_audio(mixed_samples, INT16_MAX-2500);
      sample = saturate16(mixed_samples);
    } else {
      sample = INT16_MIN;
    }

    if(iqueue_push_one__int16_t(synth->queue, &sample) < 1) // Full queue
      break;

    // Update timing for next period
    synth->sample_count++;
    if(synth->sample_count >= samples_per_ms) {
      synth->timestamp++;
      synth->sample_count = 0;
    }
  }

  return iqueue_count__int16_t(synth->queue);
}



void synth_instrument_init(SynthState *synth, int instrument, SynthVoiceCfg *cfg) {
  memcpy(&synth->instruments[instrument], cfg, sizeof *cfg);
}


// MIDI note frequencies scaled by 4
static uint16_t s_midi_notes[] = {
33,     35,     37,     39,     41,     44,     46,     49,     52,     55,
58,     62,     65,     69,     73,     78,     82,     87,     93,     98,
104,    110,    117,    123,    131,    139,    147,    156,    165,    175,
185,    196,    208,    220,    233,    247,    262,    277,    294,    311,
330,    349,    370,    392,    415,    440,    466,    494,    523,    554,
587,    622,    659,    698,    740,    784,    831,    880,    932,    988,
1047,   1109,   1175,   1245,   1319,   1397,   1480,   1568,   1661,   1760,
1865,   1976,   2093,   2217,   2349,   2489,   2637,   2794,   2960,   3136,
3322,   3520,   3729,   3951,   4186,   4435,   4699,   4978,   5274,   5588,
5920,   6272,   6645,   7040,   7459,   7902,   8372,   8870,   9397,   9956,
10548,  11175,  11840,  12544,  13290,  14080,  14917,  15804,  16744,  17740,
18795,  19912,  21096,  22351,  23680,  25088,  26580,  28160,  29834,  31609,
33488,  35479,  37589,  39824,  42192,  44701,  47359,  50175
};


static int8_t synth__find_free_voice(SynthState *synth) {
  uint8_t vi = synth->next_voice;

  uint8_t remaining = SYNTH_MAX_VOICES;
  while(voice_is_active(&synth->voices[vi]) && remaining > 0) {
    if(++vi >= SYNTH_MAX_VOICES)
      vi = 0;
    remaining--;
  }

  // Either this is a free voice or we've exhausted the list and this is the next to takeover
  SynthVoice *vox = &synth->voices[vi];
  synth->key_voices[vox->key] = -1;

  return vi;
}


static SynthVoice *synth__find_voice(SynthState *synth, uint8_t key, uint8_t instrument) {
  for(int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if(synth->voices[i].key == key && synth->voices[i].instrument == instrument)
      return &synth->voices[i];
  }

  return NULL;
}


void synth_press_key(SynthState *synth, uint8_t key, int instrument) {
  DPRINT("PRESS: %d %d", key, instrument);
  // Check if voice is already playing
  SynthVoice *vox = synth__find_voice(synth, key, instrument);

  if(!vox)
    synth_add_voice(synth, key, instrument);
  else
    vox->adsr.gate = true;
}

void synth_release_key(SynthState *synth, uint8_t key) {
  DPRINT("RELEASE: %d", key);
  int8_t vi = synth->key_voices[key];
  if(vi < 0)
    return;

  SynthVoice *vox = &synth->voices[vi];
  vox->adsr.gate = false;
}




SynthVoice *synth_add_voice(SynthState *synth, uint8_t key, int instrument) {
  uint8_t vi = synth__find_free_voice(synth);
  SynthVoice *vox = &synth->voices[vi];

  synth_voice_init(synth, vi, &synth->instruments[instrument]);
  vox->key = key;
  vox->instrument = instrument;

  // Configure oscillator frequency to match key
  synth->key_voices[key] = vi;
  DPRINT("Add voice: key=%d voice=%d freq=%d\n", key, vi, s_midi_notes[key] / 4);
  synth_set_freq(synth, vi, s_midi_notes[key]);

//  // Scale LFO frequency modulation
//  vox->modulate_freq = (int32_t)vox->modulate_freq * (int32_t)vox->osc.frequency / 440;

  // Set oscillators to random phase
  vox->osc.ddfs.count = random_next32(&s_audio_prng);
  vox->lfo.ddfs.count = random_next32(&s_audio_prng);

  // Start envelope
  vox->adsr.gate = true;

  synth->next_voice = vi + 1;
  if(synth->next_voice >= SYNTH_MAX_VOICES)
    synth->next_voice = 0;

  return vox;
}


/*void synth_end_voice(SynthState *synth, uint8_t key) {*/
/*  if(key >= SYNTH_MAX_KEYS) // Out of range*/
/*    return;*/

/*  int8_t voice = synth->key_voices[key];*/
/*//  DPRINT("End voice: key=%d voice=%d\n", key, voice);*/
/*  if(voice >= 0)*/
/*    synth->voices[voice].adsr.gate = false;*/

/*  synth->key_voices[key] = -1;*/
/*}*/


void adsr_init(SynthADSR *adsr) {
  memset(adsr, 0, sizeof *adsr);
}

const int16_t s_ulaw_table[256] = { // u = 100
  0, 2341, 4098, 5506, 6680, 7688, 8570, 9354,
  10061, 10703, 11292, 11836, 12341, 12813, 13255, 13671,
  14064, 14437, 14791, 15128, 15450, 15758, 16053, 16337,
  16609, 16871, 17125, 17369, 17605, 17834, 18055, 18270,
  18478, 18681, 18878, 19069, 19256, 19438, 19615, 19788,
  19957, 20122, 20283, 20440, 20594, 20745, 20893, 21038,
  21180, 21319, 21455, 21589, 21720, 21849, 21976, 22100,
  22223, 22343, 22461, 22577, 22692, 22804, 22915, 23024,
  23132, 23238, 23342, 23445, 23546, 23646, 23745, 23842,
  23938, 24032, 24126, 24218, 24309, 24398, 24487, 24575,
  24661, 24747, 24831, 24915, 24997, 25079, 25159, 25239,
  25318, 25396, 25473, 25549, 25625, 25699, 25773, 25846,
  25919, 25991, 26062, 26132, 26201, 26270, 26338, 26406,
  26473, 26539, 26605, 26670, 26735, 26799, 26862, 26925,
  26987, 27049, 27110, 27170, 27231, 27290, 27349, 27408,
  27466, 27524, 27581, 27638, 27694, 27750, 27806, 27861,
  27915, 27969, 28023, 28076, 28129, 28182, 28234, 28286,
  28337, 28388, 28439, 28489, 28539, 28589, 28638, 28687,
  28736, 28784, 28832, 28880, 28927, 28974, 29021, 29067,
  29113, 29159, 29205, 29250, 29295, 29339, 29384, 29428,
  29472, 29515, 29558, 29601, 29644, 29687, 29729, 29771,
  29813, 29854, 29895, 29936, 29977, 30018, 30058, 30098,
  30138, 30178, 30217, 30256, 30295, 30334, 30373, 30411,
  30449, 30487, 30525, 30563, 30600, 30637, 30674, 30711,
  30747, 30784, 30820, 30856, 30892, 30928, 30963, 30998,
  31033, 31068, 31103, 31138, 31172, 31207, 31241, 31275,
  31308, 31342, 31376, 31409, 31442, 31475, 31508, 31541,
  31573, 31606, 31638, 31670, 31702, 31734, 31766, 31797,
  31828, 31860, 31891, 31922, 31953, 31983, 32014, 32045,
  32075, 32105, 32135, 32165, 32195, 32225, 32254, 32284,
  32313, 32342, 32371, 32400, 32429, 32458, 32486, 32515,
  32543, 32572, 32600, 32628, 32656, 32684, 32711, 32739,
};


static int16_t ulaw_response(int16_t input) {
  return s_ulaw_table[((uint16_t)input >> 7) & 0xFF];
}


static int16_t log_response(int16_t input) {
#if 0
  float in = (float)input / INT16_MAX;

//  const float rp_inv = 1.0f / 0.13f;
  float rp = 0.13f;

  float ra = 1.0f - in;
  float parallel = 1.0f / ((1.0f / rp) + (1.0f / in));
  float out = parallel / (parallel + ra);

  return out * INT16_MAX;

#else
/*  Simulation of a linear pot in parallel with a resistor to get a pseudo-log response

           RA       Input (RA+Input = Pot resistance split by wiper)
           ___       ___
  1V  o---[___]--+--[___]---.
                 |   ___     |
                 +--[___]----+
                 |   RP      |
                 |           \/
                 |
                 '-----------------o Output = 1 * (Input||RP) / (Input||RP + RA)

    Resistances are scaled to fixed point in range [0.0 - 1.0)
*/
#  define ONE_17Q15   32768
#  define ONE_DIV_N(N) ((ONE_17Q15 << 15) / (N))

  if(input == 0)
    return 0;

  const int32_t rp_inv = ONE_DIV_N(4260); // 1 / 0.13 Ohm
  int32_t ra = ONE_17Q15 - 1 - input;
  int32_t parallel = rp_inv + ONE_DIV_N(input);
  parallel = ONE_DIV_N(parallel);

  int32_t out = ((parallel << 15) / (parallel + ra));

  if(out > INT16_MAX)
    out = INT16_MAX;
  if(out < 0)
    out = 0;

  return out;
#endif
}


static int16_t spline_response(uint16_t x, int16_t weight) {
  static Point16 p0 = {0,0};
  static Point16 p2 = {INT16_MAX,INT16_MAX};
/*
  Quadratic spline with weight parameter:

       W = -1               W = 0               W = 1

   1|       [] p2        |       [] p2     p1 []   .--[] p2
    |       /            |      /             |  .'
    |      /             |   [] p1            | / 
    |   _.'              |  /                 |/ 
p0 0[]--____[]__ p1   p0 []__________      p0 []__________
    0        1
*/


  // Quadratic solver suffers precision loss when weight is too close to 0.
  // We just force it to 0 to use the linear special case.
#define WEIGHT_LIMIT  (INT16_MAX / 500)
  if(weight < WEIGHT_LIMIT && weight > -WEIGHT_LIMIT)
    weight = 0;

  if(weight == 0) // Linear response: y (Q1.15) == x (Q0.16)
    return x >> 1;

  int16_t weight01 = weight/2 + INT16_MAX/2;  // Rescale [-1,1) to [0,1)
  if(weight01 < 0)  weight01 = 0;
  Point16 p1 = {INT16_MAX-weight01, weight01};

  // Find Bezier t-parameter for the current x

#ifdef PROFILE_AUDIO
  profile_start(s_prof_id);
#endif
  // Profiling shows bezier_solve_t() averages 12us while bezier_search_t() averages 16us
  // on STM32F401 @ 84MHz.
  //uint16_t t = bezier_search_t(p0,p1,p2, x >> 1);
  uint16_t t = bezier_solve_t(p0.x,p1.x,p2.x, x >> 1);
#ifdef PROFILE_AUDIO
  profile_stop(s_prof_id);
#endif

  // Use Bezier t-parameter (not time) to compute y for the current x (time)
  int16_t y = quadratic_eval(p0.y, p1.y, p2.y, t);


  // Pure quadratic Bezier doesn't afford for deep curvature.
  // We add extra Y scaling using the original weight factor to
  // pull the curve down or up more at the -1, +1 extremes.

  // W > 0  --> scale = W*t
  int32_t scale_y = ((int32_t)weight * (int32_t)(t>>1)) >> 15;

  if(weight < 0) { // Remap Y values to be lower    
    // scale = (W+1) - W*t
    // y' = y * scale;
    scale_y = ((int32_t)weight + INT16_MAX) - scale_y;
    y = (scale_y * (int32_t)y) >> 15;
  } else {  // Remap Y values to be higher
    // y' = 1 - (1-y)*(1-scale)
    y = INT16_MAX - ((((int32_t)INT16_MAX - y) * ((int32_t)INT16_MAX - scale_y)) >> 15);
  }

  return y;
}


int16_t adsr_step_output(SynthADSR *adsr, uint32_t now) {
  int32_t envelope = adsr->output;
  ADSRState next_state = adsr->state;
  int32_t elapsed = now - adsr->state_start_time;
  int32_t overtime = 0;

  // Handle normal and drone gate mode
  bool gate_on;
  if(!adsr->drone) {  // Normal mode: gate level controls envelope
    gate_on = adsr->gate;
    adsr->prev_gate = adsr->gate;

  } else {  // Drone mode: Rising edge of gate toggles envelope on/off
    if(adsr->gate && !adsr->prev_gate) { // Rising edge
      adsr->drone_on = !adsr->drone_on;
    }

    adsr->prev_gate = adsr->gate;
    adsr->gate = false;

    gate_on = adsr->drone_on;
  }
  

  // Determine next state of FSM
  switch(adsr->state) {
  case ADSR_IDLE:
    if(gate_on) {
      next_state = ADSR_ATTACK;
      printf("A %" PRIu32 "\t%d\n", now, adsr->output);
    }
    break;

  case ADSR_ATTACK:
    if(!gate_on) {
      next_state = ADSR_RELEASE;
      adsr->release_start_level = adsr->output;

    } else if(elapsed >= (int32_t)adsr->cfg.attack) {
      overtime = elapsed - (int32_t)adsr->cfg.attack;

      // Skip decay if it should already be finished
      next_state = (overtime >= (int32_t)adsr->cfg.decay) ? ADSR_SUSTAIN : ADSR_DECAY;

      if(next_state == ADSR_SUSTAIN)
        printf("S* %" PRIu32 "\t%d\n", now, adsr->output);
      else
        printf("D %" PRIu32 "\t%d\n", now, adsr->output);
    }
    break;

  case ADSR_DECAY:
    if(!gate_on) {
      next_state = ADSR_RELEASE;
      adsr->release_start_level = adsr->output;

    } else if(elapsed >= (int32_t)adsr->cfg.decay) {
      overtime = elapsed - (int32_t)adsr->cfg.decay;
      next_state = ADSR_SUSTAIN;
      printf("S %" PRIu32 "\t%d\n", now, adsr->output);
    }
    break;

  case ADSR_SUSTAIN:
    if(!gate_on) {
      next_state = ADSR_RELEASE;
      adsr->release_start_level = adsr->cfg.sustain;
      printf("R %" PRIu32 "\t%d\n", now, adsr->output);
    }
    break;

  case ADSR_RELEASE:
    if(elapsed >= (int32_t)adsr->cfg.release) {
      next_state = ADSR_IDLE;
      printf("I %" PRIu32 "\t%d\n", now, adsr->output);
    }
    break;

  }

  if(next_state != adsr->state) { // New state
    adsr->state_start_time = now - (uint32_t)overtime;
    elapsed = overtime;
  }


  uint16_t x;

  switch(next_state) {
  case ADSR_IDLE:     envelope = 0; break;
  case ADSR_ATTACK:
    x = (uint32_t)elapsed * (uint32_t)UINT16_MAX / (uint32_t)adsr->cfg.attack;
    envelope = (int32_t)(x >> 1);
    //envelope = elapsed * (int32_t)INT16_MAX / (int32_t)adsr->cfg.attack;
    break;
  case ADSR_DECAY:
    x = UINT16_MAX - (uint16_t)((uint32_t)elapsed * (uint32_t)UINT16_MAX / (uint32_t)adsr->cfg.decay);
    envelope = (((uint32_t)x * (uint32_t)(INT16_MAX - adsr->cfg.sustain)) >> 16) + adsr->cfg.sustain;
    //envelope = (int32_t)INT16_MAX - (elapsed * (int32_t)(INT16_MAX - adsr->cfg.sustain) / (int32_t)adsr->cfg.decay);
    break;
  case ADSR_SUSTAIN:  envelope = adsr->cfg.sustain; break;
  case ADSR_RELEASE:
    x = UINT16_MAX - (uint16_t)((uint32_t)elapsed * (uint32_t)UINT16_MAX / (uint32_t)adsr->cfg.release);
    envelope = ((uint32_t)x * (uint32_t)(adsr->release_start_level)) >> 16;
    //envelope = (int32_t)adsr->release_start_level - (elapsed * (int32_t)adsr->release_start_level / (int32_t)adsr->cfg.release);
//    if(envelope < 0)
//      printf("## %" PRId32 "  (%" PRId32 " %d %d)\n", envelope, elapsed, adsr->cfg.sustain, adsr->cfg.release);
    break;
  }

  if(envelope < 0)
    envelope = 0;

  adsr->state = next_state;

  if(envelope > 0) {
    // Force decay and release spline weight to always be negative
    int16_t neg_weight = adsr->cfg.spline_weight;
    if(neg_weight > 0)
      neg_weight = -neg_weight;

    switch(adsr->cfg.curve) {
    case CURVE_ULAW:
      envelope = ulaw_response(envelope);
      break;
    case CURVE_LOG:
      envelope = log_response(envelope);
      break;
    case CURVE_SPLINE:
      switch(next_state) {
        case ADSR_ATTACK:
          envelope = spline_response(x, adsr->cfg.spline_weight);
          break;
        case ADSR_DECAY:
          envelope = spline_response(x, neg_weight);
          envelope = (((uint32_t)envelope * (uint32_t)(INT16_MAX - adsr->cfg.sustain)) >> 15) + adsr->cfg.sustain;
          break;
        case ADSR_RELEASE:
          envelope = spline_response(x, neg_weight);
          envelope = ((uint32_t)envelope * (uint32_t)(adsr->release_start_level)) >> 15;
          break;

        default:
          break;
      }

      break;
    case CURVE_LINEAR:
    default:
      break;
    }
  }


  adsr->output = envelope;

  return envelope;
}
