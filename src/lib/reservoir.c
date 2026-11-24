/* reservoir.c
 * Layer3 bit reservoir: Described in C.1.5.4.2.2 of the IS
 */

#include "reservoir.h"
#include "bitstream.h"
#include "huffman.h"
#include "l3bitstream.h"
#include "l3loop.h"
#include "layer3.h"
#include "types.h"

/*
 * shine_max_reservoir_bits:
 * ------------
 * Called at the beginning of each granule to get the max bit
 * allowance for the current granule.
 */
int shine_max_reservoir_bits(shine_global_config *config) {
  /* ponytail: the reservoir is disabled (ResvMax is 0), so every granule gets
   * the mean. Allocation from perceptual entropy comes with the
   * psychoacoustic model. */
  int max_bits = config->mean_bits / config->wave.channels;

  return max_bits > 4095 ? 4095 : max_bits;
}

/*
 * shine_ResvAdjust:
 * -----------
 * Called after a granule's bit allocation. Readjusts the size of
 * the reservoir to reflect the granule's usage.
 */
void shine_ResvAdjust(gr_info *gi, shine_global_config *config) {
  config->ResvSize +=
      (config->mean_bits / config->wave.channels) - gi->part2_3_length;
}

/*
 * shine_ResvFrameEnd:
 * -------------
 * Called after all granules in a frame have been allocated. Makes sure
 * that the reservoir size is within limits, possibly by adding stuffing
 * bits. Note that stuffing bits are added by increasing a granule's
 * part2_3_length. The bitstream formatter will detect this and write the
 * appropriate stuffing bits to the bitstream.
 */
void shine_ResvFrameEnd(shine_global_config *config) {
  int over_bits;

  /* just in case mean_bits is odd, this is necessary... */
  if ((config->wave.channels == 2) && (config->mean_bits & 1))
    config->ResvSize += 1;

  over_bits = config->ResvSize - config->ResvMax;
  if (over_bits < 0)
    over_bits = 0;
  config->ResvSize -= over_bits;
  /* The reservoir is whole bytes. */
  over_bits += config->ResvSize % 8;
  config->ResvSize -= config->ResvSize % 8;

  /* doc/psy/reservoir.md §2: stuffing is ancillary data after the last
   * granule. Inside a part2_3_length a decoder would read it as
   * coefficients. */
  config->side_info.resvDrain = over_bits;
}
