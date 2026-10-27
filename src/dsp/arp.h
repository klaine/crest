/* Arpeggiator (plan/09): turns held keys into a clocked note stream for the voice engine. */
#ifndef CREST_ARP_H
#define CREST_ARP_H

#include <stdint.h>

#define ARP_HELD 16
#define ARP_SEQ 128

typedef struct {
    uint8_t held[ARP_HELD], held_vel[ARP_HELD];
    int nheld, phys_down, latched;
    uint8_t seq[ARP_SEQ], seq_vel[ARP_SEQ];
    int nseq, idx, dir;
    uint8_t sounding[ARP_HELD];
    int nsounding;
    int was_on, last_step, sig;
    int step_played;   /* the current step triggered notes */
    double pos;        /* free-running position in beats */
    uint32_t rng;
} arp_t;

struct crest_inst;

/* All take the instance; notes go to voice_note_on/off. */
int arp_enabled(const struct crest_inst *I);
void arp_note_on(struct crest_inst *I, int note, int vel);
void arp_note_off(struct crest_inst *I, int note);
void arp_all_off(struct crest_inst *I);     /* release what the arp is playing and forget held keys */
void arp_tick(struct crest_inst *I, int n); /* once per sub-block of n samples */

#endif
