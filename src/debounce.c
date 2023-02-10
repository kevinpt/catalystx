#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "debounce.h"

void debouncer_init(Debouncer *db, unsigned sample_ms, unsigned filter_ms, bool init_filter) {
  db->sample_ms = sample_ms;
  db->filter_ms = filter_ms;

  // Number of periodic samples required to consider an input stable
  db->stable_periods = (db->filter_ms + db->sample_ms-1) / db->sample_ms;

  db->unstable_count = db->stable_periods;
  db->filtered = init_filter;
  db->prev = init_filter;
};



bool debouncer_filter_sample(Debouncer *db, bool noisy_in) {
  db->prev = db->filtered;

  if(noisy_in == db->filtered) { // No change; Keep counter reset
    db->unstable_count = db->stable_periods;
    return false;
  }

  // Count down until stable duration reached
  if(db->unstable_count > 0) {
    if(--db->unstable_count == 0) {
      db->filtered = noisy_in;
      db->unstable_count = db->stable_periods;
      return true;
    }
  }

  return false;
};

bool debouncer_filtered(Debouncer *db) {
  return db->filtered;
};

bool debouncer_rising_edge(Debouncer *db) {
  return db->filtered != db->prev && db->filtered;
};

bool debouncer_falling_edge(Debouncer *db) {
  return db->filtered != db->prev && !db->filtered;
};



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

// Vertical debouncer for sizeof(VDebounceWord)*8 parallel inputs
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


void vdebouncer_init(VerticalDebouncer *db, VDebounceWord init_filter) {
  memset(&db->vcount, 0, sizeof db->vcount);
  db->filtered = init_filter;
}

//  VerticalDebouncer(T init_filter) : vcount{0}, filtered(init_filter) {};

VDebounceWord vdebouncer_filter_samples(VerticalDebouncer *db, VDebounceWord noisy_in) {
  VDebounceWord c1, c2;

  // Advance vertical counters
  db->vcount[0] = ~db->vcount[0];

  c1 = db->vcount[0] & db->vcount[1];
  db->vcount[1] = db->vcount[0] ^ db->vcount[1];

  db->vcount[2] = c1 ^ db->vcount[2];

  // Reset counts for unchanged inputs and those still bouncing
  VDebounceWord unstable = noisy_in ^ db->filtered;
  db->vcount[0] &= unstable;
  db->vcount[1] &= unstable;
  db->vcount[2] &= unstable;

  // Carry out indicates count overflow == stable in new state for 8 samples
  c2 = db->vcount[2] & c1;

  db->filtered ^= c2;

  return db->filtered;
}

VDebounceWord vdebouncer_filtered(VerticalDebouncer *db) {
  return db->filtered;
};


// Vertical debouncer for sizeof(VDebounceWord)*8 parallel inputs with fast response.
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

VDebounceWord vdebouncer_fast_filter_samples(VerticalDebouncer *db, VDebounceWord noisy_in) {
  VDebounceWord c1, c2;

  // Advance vertical counters
  db->vcount[0] = ~db->vcount[0];

  c1 = db->vcount[0] & db->vcount[1];
  db->vcount[1] = db->vcount[0] ^ db->vcount[1];

  db->vcount[2] = c1 ^ db->vcount[2];

  // Reset counts for unchanged inputs and those still bouncing
  VDebounceWord unstable = noisy_in ^ db->filtered;

  db->vcount[0] &= unstable;
  db->vcount[1] &= unstable;
  db->vcount[2] &= unstable;

  // Carry out indicates count overflow == stable in new state for 8 samples
  c2 = db->vcount[2] & c1;

  // Pressed buttons force immediate change on filter output
  VDebounceWord pressed = unstable & noisy_in; // Pressed = 1
  db->filtered |= pressed;

  // Toggle back to released after counter overflow
  db->filtered ^= c2;

  return db->filtered;
}


