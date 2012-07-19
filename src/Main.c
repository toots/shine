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

#include "g_includes.h"
#include "wave.h"
#include "layer3.h"


/* RISC OS specifics */
#define WAVE  0xfb1      /* Wave filetype */

/* The routine we tell libshine-fxp to use to read PCM data */
extern int wave_get(short buffer[2][samp_per_frame], void *config_in);

/* Routine we tell libshine-fxp to call to write out the MP3 file */
int write_mp3(long bytes, void *buffer, void *config_in) {
    config_t *config=(config_t *)config_in;
    return fwrite(buffer, sizeof(unsigned char), bytes, config->mpeg.file);
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
  printf("\n");
}

/*
 * set_defaults:
 * -------------
 */
static void set_defaults(config_t *config)
{
  L3_set_config_mpeg_defaults(&config->mpeg);
  /* Could set overrides here, if any - see Layer3.c */
}

/*
 * parse_command line arguments
 * --------------
 */
static bool parse_command(int argc, char** argv, config_t *config)
{
  int i = 0;

  if(argc<3) return false;

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
        config->quiet = 1;
        break;

      case 'h':
      default :
        return false;
      }

  if (argc - i != 2) return false;
  config->infile = argv[i++];
  config->outfile = argv[i];
  return true;
}


/*
 * check_config: Print some info about what we're going to encode
 * -------------
 */
static void check_config(config_t *config)
{
  static char *mode_names[4]    = { "stereo", "j-stereo", "dual-ch", "mono" };
  static char *layer_names[3]   = { "I", "II", "III" };
  static char *version_names[2] = { "MPEG-II (LSF)", "MPEG-I" };
  static char *psy_names[3]     = { "", "MUSICAM", "Shine" };
  static char *demp_names[4]    = { "none", "50/15us", "", "CITT" };

  printf("%s layer %s, %s  Psychoacoustic Model: %s\n",
         version_names[config->mpeg.type],
         layer_names[config->mpeg.layr],
         mode_names[config->mpeg.mode],
         psy_names[config->mpeg.psyc]);
  printf("Bitrate=%d kbps  ",config->mpeg.bitr );
  printf("De-emphasis: %s   %s %s\n",
         demp_names[config->mpeg.emph],
         ((config->mpeg.original)?"Original":""),
         ((config->mpeg.copyright)?"(C)":""));

  printf("Encoding \"%s\" to \"%s\"\n", config->infile, config->outfile);
}

/*
 * main:
 * -----
 */
int main(int argc, char **argv)
{
  config_t config;
  time_t end_time;

  config.quiet = 0;
  time(&config.start_time);

  /* Set the default MPEG encoding paramters - basically init the struct */
  set_defaults(&config);

  if (!parse_command(argc, argv, &config))
    {
      print_usage();
      exit(1);
    }
  config.quiet = config.quiet || !strcmp(config.outfile, "-");

  if (!config.quiet) print_name();

  /* Open the input file and fill the config wave_t header */
  wave_open(&config);

  /* Set the MP3 sample rate index plus see if it's valid */
  config.mpeg.samplerate_index = L3_find_samplerate_index(config.wave.samplerate);
  if (config.mpeg.samplerate_index < 0) error("invalid samplerate");

  /* Set the MP3 bit rate index plus see if it's valid */
  config.mpeg.bitrate_index = L3_find_bitrate_index(config.mpeg.bitr);
  if (config.mpeg.bitrate_index < 0) error("invalid bitrate");

  /* open the output file */
  if (!strcmp(config.outfile, "-"))
    config.mpeg.file = stdout;
  else
    config.mpeg.file = fopen(config.outfile, "wb");
  if (!config.mpeg.file)
    {
      fprintf(stderr, "Could not create \"%s\".\n", config.outfile);
      exit(1);
    }

  /* Print some info about the file about to be created (optional) */
  if (!config.quiet) check_config(&config);

  /* set up the read PCM stream and write MP3 stream functions */
  config.get_pcm = &wave_get;
  config.write_mp3 = &write_mp3;

  /* All the magic happens here */
  L3_compress(&config);

  /* Close the wave file (using the wav reader) */
  wave_close(&config);

  /* Close the MP3 file */
  fclose(config.mpeg.file);

  time(&end_time);
  end_time -= config.start_time;
  if (!config.quiet)
    printf(" Finished in %2ld:%2ld:%2ld\n", end_time/3600, (end_time/60)%60, end_time%60);
  exit(0);
}
