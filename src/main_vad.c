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
  VAD_STATE state, last_state;
  int tots[200], *estats, *estats_filtrats;

  float alpha1, alpha2, aux;
  float *buffer, *buffer_zeros;
  int num_maxs = 9;
  float pmax[] = {-208.0,-207.0,-206.0,-205.0,-204.0,-203.0,-202.0,-201.0,-200.0};
  int frame_size;         /* in samples */
  float frame_duration;   /* in seconds */
  unsigned int t, last_t; /* in frames */
  Features cond_ini;

  int error_median = 3, j, mitjana=0;
  int buffer_median[2*error_median+1];


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

  estats            = (int *) malloc(t * sizeof(int));
  estats_filtrats   = (int *) malloc(t * sizeof(int));
  //t_final = t * 


  for(k = 0; k < num_maxs; k++) vad_data->pmax = vad_data->pmax + pmax[k]/num_maxs;
  //printf("PMAX - %f\n", vad_data->pmax);

  //tanquem l'audio i el tronem a obrir
  //podem substituir amb la funcio sf_seek() pero no ens sortia i voliem seguir provant
  sf_close(sndfile_in);
  if ((sndfile_in = sf_open(input_wav, SFM_READ, &sf_info)) == 0) {
    fprintf(stderr, "Error opening input file %s (%s)\n", input_wav, strerror(errno));
    return -1;
  }

  //Segona pasada per acabar de decidir els estats
  for (t = last_t = 0; ; t++) { /* For each frame ... */
    //printf(" - Trama %i\n", t);
    /* End loop when file has finished (or there is an error) */
    if  ((n_read = sf_read_float(sndfile_in, buffer, frame_size)) != frame_size) break;

    state = vad(vad_data, buffer);
    //printf("Fins aqui\n");
    //tots[t] = (state == ST_VOICE) ? 1 : 0;
    estats[t] = (state == ST_VOICE) ? 1 : 0;  
    //printf("%i", tots[t]);
    //tots[trama] = state;
    if (verbose & DEBUG_VAD) vad_show_state(vad_data, stdout);

    /* TODO: print only SILENCE and VOICE labels */
    /* As it is, it prints UNDEF segments but is should be merge to the proper value */
    /*
    if (state != last_state) {
      if (t != last_t && (state == ST_SILENCE || state == ST_VOICE))
        fprintf(vadfile, "%.5f\t%.5f\t%s\n", last_t * frame_duration, t * frame_duration, state2str(last_state));
      last_state = state;
      last_t = t;
    }
  */
    if (sndfile_out != 0) {
      if (state == ST_VOICE) {
        if ((n_write = sf_write_float(sndfile_out, buffer, frame_size)) != frame_size) break;
      } else {
        if ((n_write = sf_write_float(sndfile_out, buffer_zeros, frame_size)) != frame_size) break;
      /* TODO: go back and write zeros in silence segments */
      }
    }
  }
  /*
  printf("En principi ho ha fet tot\n");
  for (i = 0; i < t; i++)
    printf("%i",estats[i]);
*/

  //filtro de mediana
  for (i = 0; i < error_median; i++){
    estats_filtrats[i] = estats[i];
  }
  for (k = 0; k < 2 * error_median + 1; k++) {
    buffer_median[k] = estats[k];
  }
  j = 0;
  for(i = error_median; i < t-1-error_median; i++) {
    for (k = 0; k < 2 * error_median + 1; k++) {
      mitjana = mitjana + buffer_median[k];
      //printf("%i ", buffer_median[k]);
    }
    estats_filtrats[i - error_median - 1] = (mitjana <= error_median) ? 0 : 1;
    mitjana = 0;
    //printf(" - %i\n",estats_filtrats[i]);
    j = (j+1) % (2*error_median+1);
    buffer_median[j] = estats[i];
  }

/* Codi a mitges per fer un filtre de mediana real, no un apaño només per a zeros i uns, no cal acabar, tampoc funciona bé
    for (i = 0; i < 2*error_median + 1; i++)
      printf("%i ",tots[i]);
    printf("\n");

  for(i = error_median; i < 200-1-error_median; i++) {
    for(k = 0; k < 2 * error_median + 1; k++) {
      nou = tots[i - error_median + k];
      if (k==0){
        buff_median[k] = nou;
      } else {
        for(l=0; l < k; l++){
          if(nou < buff_median[l]) break; //s'ha de ficar a la posició l
        }
        for (j = l ; j <= k; l++) {
          a = buff_median[l];
          buff_median[l] = nou;
          nou = a;
        }
      }
    }
  
    for (i = 0; i < 2*error_median + 1; i++)
      printf("%i ",tots[i]);
    printf("\n");
    tots[i] = buffer_median[error_median];
  }
  */

  int canvis[50],cont=0;
  for(i = 0; i < t-1; i++){
    if (estats_filtrats[i] + estats_filtrats[i+1] == 1) {
      canvis[cont] = i + 1;
      cont++;
    }
  }
  float t_final = t * frame_duration + n_read / (float) sf_info.samplerate;
  //printf("Duració audio: %f", t_final );

  /*
  printf("\n\nTots filtrat\n");
  for (i = 0; i < t; i++)
    printf("%i",estats_filtrats[i]);
  printf("\nCanvis\n");
  for (i = 0; i < cont; i++)
    printf("%i-",canvis[i]);
  */
  state = vad_close(vad_data);

  /* TODO: what do you want to print, for last frames? */
  
  fprintf(vadfile, "0.00000\t%.5f\t%s\n", canvis[0] * frame_duration, state2str(1));
  for(i = 0; i < cont - 1; i++){
    fprintf(vadfile, "%.5f\t%.5f\t%s\n", canvis[i] * frame_duration, canvis[i+1] * frame_duration, state2str(estats_filtrats[canvis[i]]+1));    
  }
  fprintf(vadfile, "%.5f\t%.5f\t%s\n", canvis[i] * frame_duration, t_final, state2str(estats_filtrats[canvis[i]]+1));
  /*
  if (t != last_t)
    fprintf(vadfile, "%.5f\t%.5f\t%s\n", last_t * frame_duration, t * frame_duration + n_read / (float) sf_info.samplerate, state2str(state));
  */
  /* clean up: free memory, close open files */
  free(buffer);
  free(buffer_zeros);
  sf_close(sndfile_in);
  fclose(vadfile);
  if (sndfile_out) sf_close(sndfile_out);
  return 0;
}
