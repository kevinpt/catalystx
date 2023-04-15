/*

** Buzzer generated on PC-8500:
           4kHz                              4kHz
____-_-_-_-_-_-_-_-_-_________________-_-_-_-_-_-_-_-_-____________________________

    |      60ms      |      60ms      |      60ms      |      60ms      |


    |<-- pin interrupt                |<-- pin interrupt

    |<-- Start timer; Disable pin interrupt
    |        65ms        |<-- Reenable pin interrupt
                                      |<-- Restart timer; Disable int.
                                      |         65ms         |<-- Reenable pin interrupt
                                      |                125ms               |<-- Counter timeout; End timer

** Event generation:

    | Beep 1
                                      | Beep 2
                                                                           | Beep end


The PC-8500 will beep at 4kHz for 1, 2, 4, or 5+ times. 1 is for normal UI feedback.
2, 4, and 5+ are for increasingly severe warnings/errors.

We need to track beeps as they arrive and generate events so that acoustic sound
can be generated with minimal delay.

Wait for rising edge pin interrupt triggered by buzzer start. When the pin triggers,
disable the pin interrupt and start the timer counting with a 125ms timeout. An
output compare is set for 65ms to renable the pin interrupt after the initial 4kHz
pulse train. If another edge happens before the 125ms count expires, we reinitialize
the counter and disable the pin interrupt for 65ms as before. If the 125ms count
expires, the buzzer has ended.

*/



