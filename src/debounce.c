#include <stdint.h>
#include <stdbool.h>

#include "debounce.h"

void debouncer_init(Debouncer *db, unsigned sample_ms, unsigned filter_ms, bool init_filter) {
  db->Sample_ms = sample_ms;
  db->Filter_ms = filter_ms;

  // Number of periodic samples required to consider an input stable
  db->Stable_periods = (db->Filter_ms + db->Sample_ms-1) / db->Sample_ms;

  db->m_unstable_count = db->Stable_periods;
  db->m_filtered = init_filter;
  db->m_prev = init_filter;
};



bool debouncer_filter_sample(Debouncer *db, bool noisy_in) {
  db->m_prev = db->m_filtered;

  if(noisy_in == db->m_filtered) { // No change; Keep counter reset
    db->m_unstable_count = db->Stable_periods;
    return false;
  }

  // Count down until stable duration reached
  if(db->m_unstable_count > 0) {
    if(--db->m_unstable_count == 0) {
      db->m_filtered = noisy_in;
      db->m_unstable_count = db->Stable_periods;
      return true;
    }
  }

  return false;
};

bool debouncer_filtered(Debouncer *db) {
  return db->m_filtered;
};

bool debouncer_rising_edge(Debouncer *db) {
  return db->m_filtered != db->m_prev && db->m_filtered;
};

bool debouncer_falling_edge(Debouncer *db) {
  return db->m_filtered != db->m_prev && !db->m_filtered;
};


#if 0
// FIXME: Convert to C
/*

3-bit ripple carry adder with a == 0x01, count in b:
s  = a + b
carry out = c2

s0 = a0 ^ b0                      --> ~b0
c0 = a0 & b0                      --> b0

s1 = a1 ^ b1 ^ c0                 --> b1 ^ c0 --> b1 ^ b0'
c1 = (b1 & a1) | (c0 & (a1^b1))   --> c0 & b1 --> b0' & b1

s2 = a2 ^ b2 ^ c1                 --> b2 ^ c1
c2 = (b2 & a2) | (c1 & (a2^b2))   --> c1 & b2

*/

// Vertical debouncer for sizeof(T)*8 parallel inputs
// Requires input to be stable for 8 cycles on both transitions before filter
// output changes.
//
// Noisy     _____--_--_-_-----------_--__-___________
//
// Filtered  _____________________-----------------___
//                                ^               ^
//                                |               |
//                Delayed rise >--'               `--< Delayed fall
//                in output

template <class T>
class VerticalDebouncer {
private:
  T m_s[3]; // 3-bit vertical count
  T m_filtered;

public:
  VerticalDebouncer(T init_filter) : m_s{0}, m_filtered(init_filter) {};

  T filter_samples(T noisy_in) {
    T c1, c2;

    // Advance vertical counters
    m_s[0] = ~m_s[0];

    c1 = m_s[0] & m_s[1];
    m_s[1] = m_s[0] ^ m_s[1];

    m_s[2] = c1 ^ m_s[2];

    // Reset counts for unchanged inputs and those still bouncing
    T unstable = noisy_in ^ m_filtered;
    m_s[0] &= unstable;
    m_s[1] &= unstable;
    m_s[2] &= unstable;

    // Carry out indicates count overflow == stable in new state for 8 samples
    c2 = m_s[2] & c1;

    m_filtered ^= c2;

    return m_filtered;
  }

  T filtered(void) { return m_filtered; };
};

// Vertical debouncer for sizeof(T)*8 parallel inputs with fast response.
// Fast filter response for transition from '0' to '1'.
// Requires input to be stable for 8 cycles on transition from '1' to '0'.
// This eliminates delay during initial switch bounce. Switches that short
// to ground on press should have their logic inverted before they're
// passed into filter_samples().
//
// Noisy     _____--_--_-_-----------_--__-___________
//
// Filtered  _____---------------------------------___
//                ^                               ^
//                |                               |
//                `--< Immediate rise             `--< Delayed fall
//                     in output

template <class T>
class VerticalDebouncerFastRise {
private:
  T m_s[3]; // 3-bit vertical count
  T m_filtered;

public:
  VerticalDebouncerFastRise(T init_filter) : m_s{0}, m_filtered(init_filter) {};

  T filter_samples(T noisy_in) {
    T c1, c2;

    // Advance vertical counters
    m_s[0] = ~m_s[0];

    c1 = m_s[0] & m_s[1];
    m_s[1] = m_s[0] ^ m_s[1];

    m_s[2] = c1 ^ m_s[2];

    // Reset counts for unchanged inputs and those still bouncing
    T unstable = noisy_in ^ m_filtered;

    m_s[0] &= unstable;
    m_s[1] &= unstable;
    m_s[2] &= unstable;

    // Carry out indicates count overflow == stable in new state for 8 samples
    c2 = m_s[2] & c1;

    // Pressed buttons force immediate change on filter output
    T pressed = unstable & noisy_in; // Pressed = 1
    m_filtered |= pressed;

    // Toggle back to released after counter overflow
    m_filtered ^= c2;

    return m_filtered;
  }

  T filtered(void) { return m_filtered; };
};

#endif


