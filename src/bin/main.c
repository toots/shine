/* main.c
 * Command line interface.
 *
 * This fixed point version of shine is based on Gabriel Bouvigne's original
 * source, version 0.1.2
 * It was converted for use on Acorn computers running RISC OS and will require
 * the assembler multiply file to be replaced for other platforms.
 * 09/02/01 P.Everett
 *
 * Converted to a no-globals library-based system primarily for ARM-LINUX-GCC
 * but also works on x86 and x86_64 with the Makefile-generic, however
 * quality is worse right now on non-ARM as the noarch file uses bad math.
 * Jan 2, 2006 P.Roberts
 *
 */

/* Global headers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Required headers from libshine. */
#include "layer3.h"

/* Local header */
#include "wave.h"

/* RISC OS specifics */
#define WAVE  0xfb1      /* Wave filetype */

/* Some global vars. */
char *infname, *outfname;
FILE *infile, *outfile;
int quiet = 0;

/* Routine we tell libshine-fxp to call to write out the MP3 file */
int write_mp3(long bytes, void *buffer, void *config) {
    return fwrite(buffer, sizeof(unsigned char), bytes, outfile);
}


/*
 * error:
 * ------
 */
void error(char *s)
{
  fprintf(stderr, "[ERROR] %s\n",s);
  exit(1);
}

static void print_name()
{
  printf("shineenc (Liquidsoap version)\n");
}

/*
 * print_usage:
 * ------------
 */
static void print_usage()
{
  printf("Usage: shineenc [options] <infile> <outfile>\n\n");
  printf("Options:\n");
  printf(" -h            this help message\n");
  printf(" -b <bitrate>  set the bitrate [32-320], default 128kbit\n");
  printf(" -c            set copyright flag, default off\n");
  printf(" -q            quiet mode\n");
}

/*
 * set_defaults:
 * -------------
 */
static void set_defaults(shine_config_t *config)
{
  L3_set_config_mpeg_defaults(&config->mpeg);
  /* Could set overrides here, if any - see Layer3.c */
}

/*
 * parse_command line arguments
 * --------------
 */
static int parse_command(int argc, char** argv, shine_config_t *config)
{
  int i = 0;

  if(argc<3) return 0;

  while (argv[++i][0] == '-' && argv[i][1] != '\000' && argv[i][1] != ' ')
    switch (argv[i][1])
      {
      case 'b':
        config->mpeg.bitr = atoi(argv[++i]);
        break;

      case 'c':
        config->mpeg.copyright = 1;
        break;

      case 'q':
        quiet = 1;
        break;

      case 'h':
      default :
        return 0;
      }

  if (argc - i != 2) return 0;
  infname = argv[i++];
  outfname = argv[i];
  return 1;
}

/*
 * check_config: Print some info about what we're going to encode
 * -------------
 */
static void check_config(shine_config_t *config)
{
  static char *mode_names[4]    = { "stereo", "j-stereo", "dual-ch", "mono" };
  static char *demp_names[4]    = { "none", "50/15us", "", "CITT" };

  printf("MPEG-I layer III, %s  Psychoacoustic Model: Shine\n",
         mode_names[config->mpeg.mode]);
  printf("Bitrate=%d kbps  ",config->mpeg.bitr );
  printf("De-emphasis: %s   %s %s\n",
         demp_names[config->mpeg.emph],
         ((config->mpeg.original)?"Original":""),
         ((config->mpeg.copyright)?"(C)":""));

  printf("Encoding \"%s\" to \"%s\"\n", infname, outfname);
}

/*
 * main:
 * -----
 */
int main(int argc, char **argv)
{
  time_t         start_time, end_time;
  int16_t        buffer[2][samp_per_frame];
  shine_config_t       config;
  shine_t       s;
  long           written;
  unsigned char *data;

  time(&start_time);

  /* Set the default MPEG encoding paramters - basically init the struct */
  set_defaults(&config);

  if (!parse_command(argc, argv, &config))
    {
      print_usage();
      exit(1);
    }
  quiet = quiet || !strcmp(outfname, "-");

  if (!quiet) print_name();

  /* Open the input file and fill the config shine_wave_t header */
  infile = wave_open(infname, &config, quiet);

  /* See if samplerate is valid */
  if (L3_find_samplerate_index(config.wave.samplerate) < 0) error("invalid samplerate");

  /* See if bitrate is valid */
  if (L3_find_bitrate_index(config.mpeg.bitr) < 0) error("invalid bitrate");

  /* open the output file */
  if (!strcmp(outfname, "-"))
    outfile = stdout;
  else
    outfile = fopen(outfname, "wb");
  if (!outfile)
    {
      fprintf(stderr, "Could not create \"%s\".\n", outfname);
      exit(1);
    }

  /* Set to stereo mode if wave data is stereo, mono otherwise. */
  if (config.wave.channels > 1)
    config.mpeg.mode = STEREO;
  else
    config.mpeg.mode = MONO;

  /* Print some info about the file about to be created (optional) */
  if (!quiet) check_config(&config);

  /* Initiate encoder */
  s = L3_initialise(&config);

  /* All the magic happens here */
  while (wave_get(buffer, infile, &config)) {
    data = L3_encode_frame(s,buffer,&written);
    write_mp3(written, data, &config);  
  }

  /* Close encoder. */
  L3_close(s);

  /* Close the wave file (using the wav reader) */
  wave_close(infile);

  /* Close the MP3 file */
  fclose(outfile);

  time(&end_time);
  end_time -= start_time;
  if (!quiet)
    printf(" Finished in %2ld:%2ld:%2ld\n", end_time/3600, (end_time/60)%60, end_time%60);
  exit(0);
}
