/*
 *  The Regina Rexx Interpreter
 *  Copyright (C) 1992  Anders Christensen <anders@pvv.unit.no>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define __NEW_STARLET 1         /* enable VMS function prototypes */

#include "rexx.h"

#include <unixio.h>
#include <descrip.h>
#include <clidef.h>
#include <ssdef.h>
#include <lib$routines.h>

/* defined in vmsfuncs.c */
void vms_error( const tsd_t *TSD, const int err ) ;

/* init_vms initializes the module.
 * Currently, we have nothing to initialize.
 * The function returns 1 on success, 0 if memory is short.
 */
int init_vms( tsd_t *TSD )
{
   return(1);
}

/* Helper for Unx_fork_exec() to run DCL commands on OpenVMS using
 * lib$spawn(). Returns 0 on error to fall back on vfork()/execvp().
 */
int vms_do_command( tsd_t *TSD, const char *cmdline, environment *env )
{
   struct dsc$descriptor_s name, input, output ;
   struct dsc$descriptor_s *input_ptr=NULL, *output_ptr=NULL ;
   char input_name_buf[256], output_name_buf[256] ;
   int rc, tmpfd ;
   unsigned int pid=0, flags=CLI$M_NOWAIT ;

   name.dsc$w_length = strlen( cmdline ) ;
   name.dsc$b_dtype = DSC$K_DTYPE_T ;
   name.dsc$b_class = DSC$K_CLASS_S ;
   name.dsc$a_pointer = (char *) cmdline ;

   tmpfd = env->input.hdls[0] ;
   if (tmpfd != -1 && tmpfd != 0) {
      /* get the mailbox name to use for SYS$INPUT in VMS format. */
      /* Note: the write end of the pipe has a shorter name. */
      if (getname( env->input.hdls[1], input_name_buf, 1 ) == 0) {
         fprintf(stderr, "VMS getname() failed\n");
         return 0 ;
      }

      input.dsc$w_length = strlen(input_name_buf) ;
      input.dsc$b_dtype = DSC$K_DTYPE_T ;
      input.dsc$b_class = DSC$K_CLASS_S ;
      input.dsc$a_pointer = input_name_buf ;
      input_ptr = &input ;
   }

   tmpfd = env->output.hdls[1] ;
   if (tmpfd != -1 && tmpfd != 1) {
      /* get the mailbox name for SYS$OUTPUT and SYS$ERROR. */
      if (getname( env->output.hdls[1], output_name_buf, 1 ) == 0) {
         fprintf(stderr, "VMS getname() failed\n");
         return 0 ;
      }
 
      output.dsc$w_length = strlen(output_name_buf) ;
      output.dsc$b_dtype = DSC$K_DTYPE_T ;
      output.dsc$b_class = DSC$K_CLASS_S ;
      output.dsc$a_pointer = output_name_buf ;
      output_ptr = &output ;
   }

   rc = lib$spawn( &name, input_ptr, output_ptr, &flags, NULL, &pid ) ;

   if (rc != SS$_NORMAL) {
      vms_error( TSD, rc ) ;
      return 0 ;
   }

   return (int) pid;
}
