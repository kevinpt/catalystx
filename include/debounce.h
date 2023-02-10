#ifndef DEBOUNCE_H
#define DEBOUNCE_H

typedef struct {
  // Config
  unsigned  sample_ms;
  unsigned  filter_ms;

  // State
  unsigned  stable_periods;
  unsigned  unstable_count;
  bool      filtered;
  bool      prev;
} Debouncer;


typedef unsigned  VDebounceWord;

typedef struct {
  VDebounceWord vcount[3]; // 3-bit vertical count
  VDebounceWord filtered;
} VerticalDebouncer;


#ifdef __cplusplus
extern "C" {
#endif

void debouncer_init(Debouncer *db, unsigned sample_ms, unsigned filter_ms, bool init_filter);
bool debouncer_filter_sample(Debouncer *db, bool noisy_in);
bool debouncer_filtered(Debouncer *db);
bool debouncer_rising_edge(Debouncer *db);
bool debouncer_falling_edge(Debouncer *db);


void vdebouncer_init(VerticalDebouncer *db, VDebounceWord init_filter);
VDebounceWord vdebouncer_filter_samples(VerticalDebouncer *db, VDebounceWord noisy_in);
VDebounceWord vdebouncer_filtered(VerticalDebouncer *db);

VDebounceWord vdebouncer_fast_filter_samples(VerticalDebouncer *db, VDebounceWord noisy_in);

#ifdef __cplusplus
}
#endif

#endif // DEBOUNCE_H
