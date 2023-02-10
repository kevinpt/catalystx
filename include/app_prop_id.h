#ifndef APP_PROP_ID_H
#define APP_PROP_ID_H

#define PROP_LIST_APP(M) \
\
M(P2, AUDIO,     60) \
M(P2, KEY,       61) \
M(P2, VALVE,     70) \
M(P2, ZONE,      71) \
\
M(P3, INST0,  60) \
M(P3, USER,   61) \
\
M(P4, FREQ,     60) \
M(P4, WAVE,     61) \
M(P4, CURVE,    62)


enum PropElementsApp {
  PROP_LIST_APP(PROP_ENUM_ITEM)
};


#define P_RSRC_CON_LOCAL_TASK   (P1_RSRC | P2_CON | P3_LOCAL | P4_TASK)

#define P_APP_AUDIO_INFO_VALUE    (P1_APP | P2_AUDIO | P3_INFO | P4_VALUE)
#define P_APP_AUDIO_INST0_FREQ    (P1_APP | P2_AUDIO | P3_INST0 | P4_FREQ)
#define P_APP_AUDIO_INST0_WAVE    (P1_APP | P2_AUDIO | P3_INST0 | P4_WAVE)
#define P_APP_AUDIO_INST0_CURVE   (P1_APP | P2_AUDIO | P3_INST0 | P4_CURVE)

#define P_EVENT_KEY_n_PRESS       (P1_EVENT | P2_KEY | P2_ARR(0) | P4_PRESS)
#define P_EVENT_KEY_n_RELEASE     (P1_EVENT | P2_KEY | P2_ARR(0) | P4_RELEASE)

// Cron events
#define P_EVENT_VALVE_n_ON        (P1_EVENT | P2_VALVE | P2_ARR(0) | P4_ON)
#define P_EVENT_VALVE_n_OFF       (P1_EVENT | P2_VALVE | P2_ARR(0) | P4_OFF)
#define P_EVENT_ZONE_n_ON         (P1_EVENT | P2_ZONE | P2_ARR(0) | P4_ON)
#define P_EVENT_ZONE_n_OFF        (P1_EVENT | P2_ZONE | P2_ARR(0) | P4_OFF)


#define P_EVENT_BUTTON_USER_PRESS   (P1_EVENT | P2_BUTTON | P3_USER | P4_PRESS)
#define P_EVENT_BUTTON_USER_RELEASE (P1_EVENT | P2_BUTTON | P3_USER | P4_RELEASE)


#endif // APP_PROP_ID_H
