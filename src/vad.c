#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "pav_analysis.h"
#include "vad.h"

//const float FRAME_TIME = 10.0F; /* in ms. */
const float FRAME_TIME = 80.0F; /* in ms. */
const int NUM_WIND_INIT = 4; //Finestres que agafarà al inici per calcular els parametres del soroll
int num_w = 0;
float power_init_avg = 0;

/* 
 * As the output state is only ST_VOICE, ST_SILENCE, or ST_UNDEF,
 * only this labels are needed. You need to add all labels, in case
 * you want to print the internal state in string format
 */

const char *state_str[] = {
  "UNDEF", "S", "V", "INIT", "M"
};

const char *state2str(VAD_STATE st) {
  return state_str[st];
}

/* Define a datatype with interesting features */
typedef struct {
  float zcr;
  float p;
  float am;
} Features;

/* 
 * TODO: Delete and use your own features!
 * DONE
 */
Features compute_features(const float *x, int N) {
  /*
   * Input: x[i] : i=0 .... N-1 
   * Ouput: computed features
   */
  /* 
   * DELETE and include a call to your own functions
   *
   * For the moment, compute random value between 0 and 1 
   */
  Features feat;
  //feat.zcr = feat.p = feat.am = (float) rand()/RAND_MAX;
  feat.zcr = compute_zcr(x,N,16000);
  feat.am = compute_am(x,N);
  feat.p = compute_power(x,N);
  //printf("P - %f\nA - %f\nZ - %f\n",feat.p,feat.am,feat.zcr);
  return feat;
}

/* 
 * TODO: Init the values of vad_data
 * DONE
 */

VAD_DATA * vad_open(float rate, float alpha1) {
  VAD_DATA *vad_data = malloc(sizeof(VAD_DATA));
  vad_data->state = ST_INIT;
  vad_data->sampling_rate = rate;
  vad_data->frame_length = rate * FRAME_TIME * 1e-3;
  vad_data->alpha1 = alpha1;
  vad_data->alpha2 = alpha1; //despres canviem
  vad_data->pmax = -200;
  return vad_data;
}

VAD_STATE vad_close(VAD_DATA *vad_data) {
  /* 
   * TODO: decide what to do with the last undecided frames
   */
  VAD_STATE state = vad_data->state;

  free(vad_data);
  return state;
}

unsigned int vad_frame_size(VAD_DATA *vad_data) {
  return vad_data->frame_length;
}

/* 
 * TODO: Implement the Voice Activity Detection 
 * using a Finite State Automata
 */

VAD_STATE vad(VAD_DATA *vad_data, float *x) {

  /* 
   * TODO: You can change this, using your own features,
   * program finite state automaton, define conditions, etc.
   */

  Features f = compute_features(x, vad_data->frame_length);
  vad_data->last_feature = f.p; /* save feature, in case you want to show */

  if (f.p > vad_data->pmax) {
    vad_data->pmax = f.p;
    vad_data->p2 = vad_data->pmax - vad_data->alpha2;
  }

  switch (vad_data->state) {
  case ST_INIT:
    if (num_w < NUM_WIND_INIT) {
      power_init_avg = power_init_avg + f.p / NUM_WIND_INIT; 
      num_w = num_w + 1;
    } else {
      vad_data->state = ST_SILENCE;
      vad_data->p1 = power_init_avg + vad_data -> alpha1; //prova
    }
    break;

  case ST_SILENCE:
    if (f.p > vad_data->p2) {
      vad_data->state = ST_VOICE;
    } else if(f.p > vad_data->p1) {
      vad_data->state = ST_MAYBE;
    }
    break;

  case ST_VOICE:
    if (f.p < vad_data->p1) {
      vad_data->state = ST_SILENCE;
    } else if(f.p < vad_data->p2) {
      vad_data->state = ST_MAYBE;
    }
    break;

  case ST_UNDEF:
    break;

  case ST_MAYBE:
    if (f.p > vad_data->p2) { //condicion para ir de maybe voice a voice 
      vad_data->state = ST_VOICE;
    } else if (f.p < vad_data->p1) { //condicion para ir de maybe voice a silence
      vad_data->state = ST_SILENCE;
    } //sino se queda en Maybe silence
    break;

/*  case ST_MAYBE_S:
    if (f.p > vad_data->p1) { //condicion para ir de maybe silence a voice 
      vad_data->state = ST_VOICE;
    } else if (f.p > vad_data->p1) { //condicion para ir de maybe silence a silence
      vad_data->state = ST_SILENCE;
    } //sino se queda en Maybe silence
    break;
*/
  }
  if (vad_data->state == ST_SILENCE ||
      vad_data->state == ST_VOICE)
    return vad_data->state;
  else
    return ST_UNDEF;
}

void vad_show_state(const VAD_DATA *vad_data, FILE *out) {
  fprintf(out, "%d\t%f\n", vad_data->state, vad_data->last_feature);
}
