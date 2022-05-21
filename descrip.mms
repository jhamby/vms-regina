!
! regina on OpenVMS
! MMS/MMK-makefile
!
!.IGNORE         ! ignore errors, continue processing, like "make -k"
!
.FIRST
        @ write sys$output f$fao("!/==!AS !%D==", -
                "Processing DESCRIP.MMS (Regina) begins at", 0)
.LAST
        @ write sys$output f$fao("!/==!AS !%D==", -
                "Processing DESCRIP.MMS (Regina) concludes at", 0)

.INCLUDE regina.ver

! TODO: test performance of REGINA_BITS=64 (vs. 32) and /PLUS_LIST_OPT.
! Note: REGINA_BITS=64 appears to be as fast as REGINA_BITS=32 on Alpha EV67.
! We can define "NDEBUG" for non-debug builds to remove the assertion checks.
COMMON_CFLAGS=/FLOAT=IEEE/IEEE=DENORM/MAIN=POSIX_EXIT/UNSIGNED_CHAR/PREFIX=ALL-
        /INCLUDE_DIRECTORY=[]/NAMES=(AS_IS,SHORT)/OBJECT=$(MMS$TARGET_NAME).OBJ-
        /DEFINE=(VMS,_LARGEFILE,_USE_STD_STAT,SOCKADDR_LEN,_POSIX_EXIT,__UNIX_PUTC,-
                 REGINA_VERSION_DATE=""$(VER_DATE)"",REGINA_VERSION_MAJOR="""$(VER_MAJOR)""",-
                 REGINA_VERSION_MINOR="""$(VER_MINOR)""",REGINA_VERSION_RELEASE="""$(VER_RELEASE)""",-
                 REGINA_VERSION_SUPP=""$(VER_SUPP)"",REGINA_BITS=64,NDEBUG)/STAND=C99

! Note: including these with /PLUS_LIST can make the command line too long for DCL.
WARN_CFLAGS=/WARN=(ENABLE=(DEFUNCT, OBSOLESCENT, QUESTCODE, UNUSEDTOP),-
                   DISABLE=(BOOLEXPRCONST, DUPEXTERN, INTCONCASTSGN, UNKNOWNMACRO))

! compiler optimization and debugging flags

.IFDEF DEBUGGING
CC=CC/DEBUG/LIST
CFLAGS=/NOOPTIMIZE$(COMMON_CFLAGS)
LINK=LINK
.ELSE
CC=CC/NODEBUG
.IFDEF MMSALPHA
! Note: /OPT=(LEV=5)/PLUS_LIST causes VSI C V7.4-002 to run out of memory.
ARCHFLAGS=/ARCH=EV56/OPT=(LEV=5,TUN=EV67)
ARCHFLAGSPLUS=/ARCH=EV56/OPT=(LEV=4,TUN=EV67)
.ELSE
ARCHFLAGS=/ARCH=HOST/OPT=(LEV=5)
ARCHFLAGSPLUS=/ARCH=HOST/OPT=(LEV=5)
.ENDIF
CFLAGS=$(ARCHFLAGS)$(COMMON_CFLAGS)$(WARN_CFLAGS)
CFLAGSPLUS=$(ARCHFLAGSPLUS)$(COMMON_CFLAGS)/PLUS_LIST/WARN=DIS=DUPEXTERN
LINK=LINK/NODEBUG
.ENDIF
LINKFLAGS=/MAP

OBJ1=builtin.obj,client.obj,cmath.obj,cmsfuncs.obj,convert.obj,
OBJ2=dbgfuncs.obj,debug.obj,envir.obj,error.obj,expr.obj,
OBJ3=files.obj,funcs.obj,mygetopt.obj,os_unx.obj,
OBJ4=mt_notmt.obj,rexxbif.obj,instore.obj,extstack.obj,os2funcs.obj,
OBJ5=interp.obj,interprt.obj,lexsrc.obj,library.obj,macros.obj,memory.obj,
OBJ6=misc.obj,options.obj,parsing.obj,rexxext.obj,rexxsaa.obj,shell.obj,
OBJ7=signals.obj,stack.obj,strengs.obj,strmath.obj,tracing.obj,unxfuncs.obj,
.IFDEF DOESNT_HAVE_UNAME
OBJ8=uname.obj,
.ELSE
OBJ8=
.ENDIF
OBJ9=vmsfuncs.obj,vmscmd.obj,variable.obj,wrappers.obj,yaccsrc.obj,arxfuncs.obj

OBJS=$(OBJ1)$(OBJ2)$(OBJ3)$(OBJ4)$(OBJ5)$(OBJ6)$(OBJ7)$(OBJ8)$(OBJ9),-
      vms_crtl_init.obj,vms_crtl_values.obj

SRCS=$(SUBST .obj,.c,$(OBJS)),vms_crtl_init.c,vms_crtl_values.c

! Is there a way to replace "," with "+" or vice versa?
SRCSPLUS=builtin.c+client.c+cmath.c+cmsfuncs.c+convert.c+-
   dbgfuncs.c+debug.c+envir.c+error.c+expr.c+-
   files.c+funcs.c+mygetopt.c+os_unx.c+-
   mt_notmt.c+rexxbif.c+instore.c+extstack.c+os2funcs.c+-
   interp.c+interprt.c+lexsrc.c+library.c+macros.c+memory.c+-
   misc.c+options.c+parsing.c+rexxext.c+rexxsaa.c+shell.c+-
   signals.c+stack.c+strengs.c+strmath.c+tracing.c+unxfuncs.c+-
   vmsfuncs.c+vmscmd.c+variable.c+wrappers.c+yaccsrc.c+arxfuncs.c+-
   vms_crtl_init.c+vms_crtl_values.c

LIB=LIBRARY
LIBFLAGS=/CREATE regina.olb

all : rexx.exe, regina.exe, execiser.exe

.IFDEF BUILD_LTO
rexx.exe :      rexx.c, $(SRCS)
        @ write sys$output "Compiling and linking $(MMS$TARGET) "
        $(CC) $(CFLAGSPLUS) rexx.c+$(SRCSPLUS)
        $(LINK) $(LINKFLAGS) rexx.obj
        @ write sys$output "Done (linking)."

.ELSE
rexx.exe :      rexx.obj, -
        $(OBJ1)$(OBJ2)$(OBJ3)$(OBJ4)$(OBJ5)$(OBJ6)$(OBJ7)$(OBJ8)$(OBJ9),-
            vms_crtl_init.obj,vms_crtl_values.obj
        @ write sys$output "Linking $(MMS$TARGET) "
        $(LINK) $(LINKFLAGS) $(MMS$SOURCE_LIST)
        @ write sys$output "Done (linking)."
.ENDIF

regina.exe :    regina.obj,regina.olb,vms_crtl_init.obj,vms_crtl_values.obj
        @ write sys$output "Linking $(MMS$TARGET) "
        $(LINK) $(LINKFLAGS) regina.obj,vms_crtl_init.obj,vms_crtl_values.obj,-
            regina.olb/LIBRARY
        @ write sys$output "Done (linking)."
!
execiser.exe :  execiser.obj,regina.olb,vms_crtl_init.obj,vms_crtl_values.obj
        @ write sys$output ""
        @ write sys$output "Linking $(MMS$TARGET) "
        $(LINK) $(LINKFLAGS) execiser.obj, vms_crtl_init.obj,-
            vms_crtl_values.obj, regina.olb/LIBRARY
        @ write sys$output "Done (linking)."
!
regina.olb :    drexx.obj,rexxsaa.obj,client.obj -
        $(OBJ1)$(OBJ2)$(OBJ3)$(OBJ4)$(OBJ5)$(OBJ6)$(OBJ7)$(OBJ8)$(OBJ9)
        @ write sys$output "Creating library $(MMS$TARGET) "
        $(LIB) $(LIBFLAGS) $(MMS$SOURCE_LIST)
        @ write sys$output "Done (library)."
!
clean :
        @ delete/nolog  rexx.exe;*,regina.exe;*,execiser.exe;*,-
                        *.obj;*,*.map;*,*.olb;*,*.lis;*
        @ write sys$output "Done (cleaning)."
!
alloca.obj : alloca.c
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
builtin.obj :   builtin.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
client.obj :    client.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
cmath.obj :     cmath.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
cmsfuncs.obj :  cmsfuncs.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
convert.obj :   convert.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
dbgfuncs.obj :  dbgfuncs.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
debug.obj :     debug.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
envir.obj :     envir.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
error.obj :     error.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
execiser.obj :  execiser.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
expr.obj :      expr.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
extlib.obj :    extlib.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
extstack.obj :    extstack.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
files.obj :     files.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
funcs.obj :     funcs.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
instore.obj :    instore.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
interp.obj :    interp.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
interprt.obj :  interprt.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
lexsrc.obj :    lexsrc.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
library.obj :   library.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
macros.obj :    macros.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
memory.obj :    memory.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
misc.obj :      misc.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
mt_notmt.obj :  mt_notmt.c, rexx.h, mt.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
mygetopt.obj :  mygetopt.c, rexx.h, mygetopt.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
options.obj :   options.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
os2funcs.obj :   os2funcs.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
os_unx.obj :   os_unx.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
parsing.obj :   parsing.c, rexx.h, strengs.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
r2perl.obj :    r2perl.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
regina.obj :    regina.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
rexx.obj :      rexx.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
drexx.obj :     drexx.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
rexxbif.obj :   rexxbif.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
rexxext.obj :   rexxext.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
rexxsaa.obj :   rexxsaa.c, configur.h, defs.h, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
shell.obj :     shell.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
signals.obj :   signals.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
stack.obj :     stack.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
strengs.obj :   strengs.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
strmath.obj :   strmath.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
tracing.obj :   tracing.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
unxfuncs.obj :  unxfuncs.c, utsname.h, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
variable.obj :  variable.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
vmscmd.obj :    vmscmd.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
vmsfuncs.obj :  vmsfuncs.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
vms_crtl_init.obj :  vms_crtl_init.c
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
vms_crtl_values.obj :  vms_crtl_values.c
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
wrappers.obj :  wrappers.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
yaccsrc.obj :   yaccsrc.c, rexx.h
        @ write sys$output ""
        @ write sys$output "Compiling $(MMS$SOURCE) "
        $(CC) $(CFLAGS) $(MMS$SOURCE)
        @ write sys$output "Done (compiling)."
! -- end of descrip.mms
