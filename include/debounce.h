#ifndef DEBOUNCE_H
#define DEBOUNCE_H

typedef struct {
  unsigned  Sample_ms;
  unsigned  Filter_ms;
  unsigned  Stable_periods;
  unsigned  m_unstable_count;
  bool      m_filtered;
  bool      m_prev;
} Debouncer;


#ifdef __cplusplus
extern "C" {
#endif

void debouncer_init(Debouncer *db, unsigned sample_ms, unsigned filter_ms, bool init_filter);
bool debouncer_filter_sample(Debouncer *db, bool noisy_in);
bool debouncer_filtered(Debouncer *db);
bool debouncer_rising_edge(Debouncer *db);
bool debouncer_falling_edge(Debouncer *db);

#ifdef __cplusplus
}
#endif

#endif // DEBOUNCE_H
