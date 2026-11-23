/* shineenc's command line: malformed ones print the usage and exit with 1. */

#include "helpers.h"

#include <stdio.h>

static int expect_usage(const char *arguments) {
  int status = run_shineenc(arguments, arguments);

  if (status < 0)
    return 0;
  if (status != 1) {
    printf("FAIL %s: exited with %d, expected 1\n", arguments, status);
    return 0;
  }
  return 1;
}

int main(void) {
  /* Only options: parsing must not read past the arguments. */
  static const char *malformed[] = {"-q -v > /dev/null", "-q -b > /dev/null",
                                    "-q -m -b > /dev/null"};
  const int count = sizeof(malformed) / sizeof(*malformed);
  int passed = 0;

  for (int i = 0; i < count; i++)
    passed += expect_usage(malformed[i]);

  printf("%d/%d cases passed\n", passed, count);
  return passed == count ? 0 : 1;
}
