#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sndfile.h>

#include "vad.h"
#include "vad_docopt.h"

#define DEBUG_VAD 0x1

int main(int argc, char *argv[]) {
  int verbose = 0; /* To show internal state of vad: verbose = DEBUG_VAD; */

  SNDFILE *sndfile_in, *sndfile_out = 0;
  SF_INFO sf_info;
  FILE *vadfile;
  int n_read = 0, n_write = 0, i, k, l;

  VAD_DATA *vad_data;
  VAD_STATE state, last_state, *tots = 0;

  float alpha1, alpha2, aux;
  float *buffer, *buffer_zeros;
  int num_maxs = 9;
  float pmax[] = {-208.0,-207.0,-206.0,-205.0,-204.0,-203.0,-202.0,-201.0,-200.0};
  int frame_size;         /* in samples */
  float frame_duration;   /* in seconds */
  unsigned int t, last_t; /* in frames */
  Features cond_ini;

  char	*input_wav, *output_vad, *output_wav;

  DocoptArgs args = docopt(argc, argv, /* help */ 1, /* version */ "2.0");

  verbose    = args.verbose ? DEBUG_VAD : 0;
  input_wav  = args.input_wav;
  output_vad = args.output_vad;
  output_wav = args.output_wav;
  alpha1 = atof(args.alpha1);
  alpha2 = atof(args.alpha2);

  if (input_wav == 0 || output_vad == 0) {
    fprintf(stderr, "%s\n", args.usage_pattern);
    return -1;
  }

  /* Open input sound file */
  if ((sndfile_in = sf_open(input_wav, SFM_READ, &sf_info)) == 0) {
    fprintf(stderr, "Error opening input file %s (%s)\n", input_wav, strerror(errno));
    return -1;
  }

  if (sf_info.channels != 1) {
    fprintf(stderr, "Error: the input file has to be mono: %s\n", input_wav);
    return -2;
  }

  /* Open vad file */
  if ((vadfile = fopen(output_vad, "wt")) == 0) {
    fprintf(stderr, "Error opening output vad file %s (%s)\n", output_vad, strerror(errno));
    return -1;
  }

  /* Open output sound file, with same format, channels, etc. than input */
  if (output_wav) {
    if ((sndfile_out = sf_open(output_wav, SFM_WRITE, &sf_info)) == 0) {
      fprintf(stderr, "Error opening output wav file %s (%s)\n", output_wav, strerror(errno));
      return -1;
    }
  }

  vad_data = vad_open(sf_info.samplerate, alpha1, alpha2);
  /* Allocate memory for buffers */
  frame_size   = vad_frame_size(vad_data);
  buffer       = (float *) malloc(frame_size * sizeof(float));
  buffer_zeros = (float *) malloc(frame_size * sizeof(float));
  for (i=0; i< frame_size; ++i) buffer_zeros[i] = 0.0F;

  frame_duration = (float) frame_size/ (float) sf_info.samplerate;
  last_state = ST_UNDEF;


  //Primera pasada, establir els valor minims i maxims per als dos llindars p1 i p2
  for (t = last_t = 0; ; t++) { /* For each frame ... */
    /* End loop when file has finished (or there is an error) */
    if  ((n_read = sf_read_float(sndfile_in, buffer, frame_size)) != frame_size) break;
    cond_ini = compute_features(buffer, frame_size);
    //printf("P - %f",cond_ini.p);
    //vad_data->pmax = cond_ini.p > vad_data->pmax ? cond_ini.p : vad_data->pmax; //quan era només una variable, no un array
    if (cond_ini.p > pmax[0]) {
      for (k = 1; k < num_maxs; k++){
        if (cond_ini.p < pmax[k]) break;
      } //la posició on s'ha de ficar el nou valor és k-1
      for (l = k - 1; l >= 0; l--) {
        aux = pmax[l];
        pmax[l] = cond_ini.p;
        cond_ini.p = aux;
      }
    }
    /* Chivato para ver que hemos recolectado bien las 5 muestras mayores en orden
    printf("\tPmax - [ ");
    for(k = 0; k < num_maxs; k++) printf("%f , ", pmax[k]);
    printf("]\n");
    */
  } //tambe podem fer mitjana noseke, TODO, DONE
  //hacer la media y ponerla en fmax dentro de vad_data
  for(k = 0; k < num_maxs; k++) vad_data->pmax = vad_data->pmax + pmax[k]/num_maxs;

  //tanquem l'audio i el tronem a obrir
  //podem substituir amb la funcio sf_seek() pero no ens sortia i voliem seguir provant
  sf_close(sndfile_in);
  if ((sndfile_in = sf_open(input_wav, SFM_READ, &sf_info)) == 0) {
    fprintf(stderr, "Error opening input file %s (%s)\n", input_wav, strerror(errno));
    return -1;
  }

  //Segona pasada per acabar de decidir els estats
  for (t = last_t = 0; ; t++) { /* For each frame ... */
    /* End loop when file has finished (or there is an error) */
    if  ((n_read = sf_read_float(sndfile_in, buffer, frame_size)) != frame_size) break;
    state = vad(vad_data, buffer);
    tots[t] = state;
    if (verbose & DEBUG_VAD) vad_show_state(vad_data, stdout);

    /* TODO: print only SILENCE and VOICE labels */
    /* As it is, it prints UNDEF segments but is should be merge to the proper value */
    if (state != last_state) {
      if (t != last_t && (state == ST_SILENCE || state == ST_VOICE))
        fprintf(vadfile, "%.5f\t%.5f\t%s\n", last_t * frame_duration, t * frame_duration, state2str(last_state));
      last_state = state;
      last_t = t;
    }

    if (sndfile_out != 0) {
      if (state == ST_VOICE) {
        if ((n_write = sf_write_float(sndfile_out, buffer, frame_size)) != frame_size) break;
      } else {
        if ((n_write = sf_write_float(sndfile_out, buffer_zeros, frame_size)) != frame_size) break;
      /* TODO: go back and write zeros in silence segments */
      }
    }
  }

  for (i = 0; i < t; i++){
    printf("%i",tots[t]);
  }



  state = vad_close(vad_data);
  /* TODO: what do you want to print, for last frames? */
  if (t != last_t)
    fprintf(vadfile, "%.5f\t%.5f\t%s\n", last_t * frame_duration, t * frame_duration + n_read / (float) sf_info.samplerate, state2str(state));

  /* clean up: free memory, close open files */
  free(buffer);
  free(buffer_zeros);
  sf_close(sndfile_in);
  fclose(vadfile);
  if (sndfile_out) sf_close(sndfile_out);
  return 0;
}
