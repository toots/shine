/*
 * RISC OS specific functions.
 *
 * v0.00 11/03/01
 */

#include <kernel.h>
#include <swis.h>

#define READ_CATINFO 17  /* osfile command */
#define WRITE_CATINFO 18 /* osfile command */
#define STAMP_FILE 9     /* osfile command */
#define NO_FILE 0        /* osfile return status */
#define IS_FILE 1        /* osfile return status */
#define IS_DIR 2         /* osfile return status */
#define OBEY  0xfeb      /* Obey filetype */
#define AMPEG 0x1ad      /* Audio MPEG filetype */
#define DATA  0xffd      /* Data filetype */

void settype(char *name, int type)
{
  _kernel_swi_regs regs;

  regs.r[0] = WRITE_CATINFO;
  regs.r[1] = (int)name;
  regs.r[2] = type;
  _kernel_swi(OS_File,&regs,&regs);
}

int readtype(char *name)
{
  _kernel_swi_regs regs;
  
  regs.r[0] = READ_CATINFO;
  regs.r[1] = (int)name;
  _kernel_swi(OS_File,&regs,&regs);
  if(regs.r[0] != IS_FILE)
    return -1;

  return (regs.r[2]>>8) & 0xfff;
}
