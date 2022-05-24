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
/* huups, have to add one to length in everyting given to Str_ncatstr */

#include "rexx.h"

#include <assert.h>
#include <ctype.h>
#include <strings.h>            /* for strncasecmp() */
#include <unistd.h>

#define __NEW_STARLET 1         /* enable VMS function prototypes */

#include <starlet.h>
#include <descrip.h>
#include <rmsdef.h>
#include <ssdef.h>
#include <dvidef.h>
#include <jpidef.h>
#include <quidef.h>
#include <syidef.h>
#include <uicdef.h>
#include <libdtdef.h>
#include <lnmdef.h>
#include <psldef.h>
#include <libdef.h>
#include <efndef.h>
#include <iledef.h>
#include <iosbdef.h>
#include <lib$routines.h>
#include <gen64def.h>

#include <fabdef.h>
#include <namdef.h>
#include <xab.h>


#define ADD_CHAR(a,b) (a)->value[(a)->len++] = (b)


struct fabptr {
   struct fabptr *next ;
   int num ;
   streng *name ;
   struct FAB *box ;
} ;

/* f$cvsi()      */
/* f$cvtime()    */
/* f$cvui()      */


typedef struct { /* vms_tsf: static variables of this module (thread-safe) */
   char *                  error_buffer;
   struct dsc$descriptor_s error_descr;
   int                     pid;
   struct fabptr *         fabptrs[16];
} vmf_tsd_t; /* thread-specific but only needed by this module. see
              * init_vmf
              */

/* Helper function defined later. */
static streng *boolean( const tsd_t *TSD, const int param ) ;

/* init_vmf initializes the module.
 * Currently, we set up the thread specific data.
 * The function returns 1 on success, 0 if memory is short.
 */
int init_vmf( tsd_t *TSD )
{
   vmf_tsd_t *vt;

   if (TSD->vmf_tsd != NULL)
      return(1);

   if ((vt = TSD->vmf_tsd = MallocTSD(sizeof(vmf_tsd_t))) == NULL)
      return(0);
   memset(vt,0,sizeof(vmf_tsd_t));  /* correct for all values */
   return(1);
}


static const char *all_privs[] = {
   "CMKRNL",   "CMEXEC",  "SYSNAM",   "GRPNAM",    "ALLSPOOL", "IMPERSONATE",
   "DIAGNOSE", "LOG_IO",  "GROUP",    "NOACNT",    "PRMCEB",   "PRMMBX",
   "PSWAPM",   "SETPRI",  "SETPRV",   "TMPMBX",    "WORLD",    "MOUNT",
   "OPER",     "EXQUOTA", "NETMBX",   "VOLPRO",    "PHY_IO",   "BUGCHK",
   "PRMGBL",   "SYSGBL",  "PFNMAP",   "SHMEM",     "SYSPRV",   "BYPASS",
   "SYSLCK",   "SHARE",   "UPGRADE",  "DOWNGRADE", "GRPPRV",   "READALL",
   "IMPORT",   "AUDIT",   "SECURITY"
} ;

#define NUM_PRIVS ((sizeof(all_privs)/sizeof(char*)))


void vms_error( const tsd_t *TSD, const int err )
{
   unsigned short length ;
   unsigned int rc ;
   vmf_tsd_t *vt;

   vt = TSD->vmf_tsd;
   if (!vt->error_buffer)
   {
      vt->error_descr.dsc$a_pointer = vt->error_buffer = MallocTSD( 256 ) ;
      vt->error_descr.dsc$w_length = 256 ;
      vt->error_descr.dsc$b_dtype = DSC$K_DTYPE_T ;
      vt->error_descr.dsc$b_class = DSC$K_CLASS_S ;
   }

   rc=sys$getmsg( err, &length, &vt->error_descr, 0, NULL ) ;
   if (rc != SS$_NORMAL)
      exiterror( ERR_SYSTEM_FAILURE , 0 ) ;

   vt->error_buffer[length] = 0x00 ;
   printf( "\n" ) ;
   fprintf( stderr, "%s\n", vt->error_buffer ) ;
}


static streng *internal_id( const tsd_t *TSD, const unsigned short *id )
{
   streng *result ;

   result = Str_makeTSD( 20 ) ;
   snprintf( result->value, 20, "(%u,%u,%u)", id[0], id[1], id[2] ) ;
   result->len = strlen( result->value ) ;
   return( result ) ;
}

static int name_to_num( const tsd_t *TSD, const streng *name )
{
   unsigned int id, rc ;
   struct dsc$descriptor_s descr = {
      name->len, DSC$K_DTYPE_T, DSC$K_CLASS_S, (char *)name->value
   } ;
   rc = sys$asctoid( &descr, &id, NULL ) ;
   if (rc == SS$_NOSUCHID || rc == SS$_IVIDENT)
      return 0 ;
   if (rc != SS$_NORMAL)
      vms_error( TSD, rc ) ;

   return (id) ;
}


static streng *num_to_name( const tsd_t *TSD, const int num )
{
   char user[256], group[256] ;
   $DESCRIPTOR( udescr, user ) ;
   $DESCRIPTOR( gdescr, group ) ;
   streng *result ;
   unsigned short length, glength ;
   int rc, xnum ;
   int success = TRUE;

   if (num == 0)
      return NULL ;

   if (!(num & 0x80000000))
   {
      xnum = num | 0x0000ffff ;
      rc = sys$idtoasc( xnum, &glength, &gdescr, NULL, NULL, NULL) ;
      if (rc == SS$_NOSUCHID)
         success = FALSE;
      else if (rc != SS$_NORMAL)
      {
         vms_error( TSD, rc ) ;
         success = FALSE;
      }
   }
   else
      success = FALSE;

   rc = sys$idtoasc( num, &length, &udescr, NULL, NULL, NULL ) ;

   if (rc == SS$_NOSUCHID)
      return NULL ;
   if (rc != SS$_NORMAL)
   {
      vms_error( TSD, rc ) ;
      length = 0 ;
   }

   if (success)
   {
      result = Str_makeTSD( glength + 1 + length ) ;
      Str_ncatstrTSD( result, group, glength ) ;
      result->value[result->len++] = ',' ;
   }
   else
      result = Str_makeTSD( length ) ;

   Str_ncatstrTSD( result, user, length ) ;
   return result ;
}


static streng *get_prot( const tsd_t *TSD, int prot )
{
   char *names[] = { "SYSTEM", "OWNER", "GROUP", "WORLD" } ;
   int i ;
   streng *result ;

   result = Str_makeTSD( 50 ) ;
   for (i=0; i<4; i++)
   {
      Str_catstrTSD( result, names[i] ) ;
      if ((prot & 0x0f) != 0x0f)
      {
         /* DCL-bug: says RWED, should say RWLP */
         ADD_CHAR(result, '=') ;
         if (!(prot & 0x01)) ADD_CHAR(result, 'R') ;
         if (!(prot & 0x02)) ADD_CHAR(result, 'W') ;
         if (!(prot & 0x04)) ADD_CHAR(result, 'E') ; /* actually L */
         if (!(prot & 0x08)) ADD_CHAR(result, 'D') ; /* actually P */
      }
      ADD_CHAR( result, ',' ) ;
      ADD_CHAR( result, ' ' ) ;
      prot = prot >> 4 ;
   }
   result->len -= 2 ;
   return result ;
}

static streng *get_uic( const tsd_t *TSD, const UICDEF *uic )
{
   streng *name ;
   streng *result ;

   result = Str_makeTSD( 14 ) ;
   name = num_to_name( TSD, uic->uic$l_uic ) ;
   if (name)
   {
      ADD_CHAR( result, '[' ) ;
      Str_catTSD( result, name ) ;
      ADD_CHAR( result, ']' ) ;
   }
   else
   {
      snprintf(result->value, 14, "[%o,%o]", uic->uic$v_group, uic->uic$v_member) ;
      result->len = strlen( result->value ) ;
   }
   return result ;
}


struct dvi_items_type {
   const char *name ;   /* Parameter that identifies a particular item */
   const unsigned int item_code ;      /* Item identifier to give to lib$getdvi */
} ;

#define ARRAY_LEN(s) (sizeof(s) / sizeof(struct dvi_items_type))


#define HEXDIG(x) ((isdigit(x))?((x)-'0'):(toupper(x)-'A'+10))

static unsigned int read_pid( const streng *hexpid )
{
   int i ;
   unsigned int sum=0 ;

   for (i=0; i<hexpid->len; i++)
      if (isxdigit(hexpid->value[i]))
         sum = sum*16 + HEXDIG( hexpid->value[i] ) ;

   return sum ;
}


streng *vms_f_directory( tsd_t *TSD, cparamboxptr parms )
{
   char buffer[ NAML$C_MAXRSS ] ;
   int rc ;
   $DESCRIPTOR( dir, buffer ) ;

   checkparam( parms, 0, 0, "VMS_F_DIRECTORY" ) ;

   rc = sys$setddir( NULL, &(dir.dsc$w_length), &dir ) ;
   if (rc != RMS$_NORMAL)
      vms_error( TSD, rc ) ;

   return Str_ncreTSD( dir.dsc$a_pointer, dir.dsc$w_length ) ;
}


/* f$edit() */
/* f$element() */
/* f$environment()   --- not sure how to handle this */
/* f$extract() */
/* f$fao() */

/*
streng *vms_f_file_attributes( tsd_t *TSD, cparamboxptr parms )
{
   checkparam( parms, 2, 2, "VMS_F_FILE_ATTRIBUTES" ) ;
}
*/

static const struct dvi_items_type dvi_items[] =
{
   { "ACCESSTIMES_RECORDED",     DVI$_ACCESSTIMES_RECORDED     },
   { "ACPPID",                   DVI$_ACPPID                   },
   { "ACPTYPE",                  DVI$_ACPTYPE                  },
   { "ADAPTER_IDENT",            DVI$_ADAPTER_IDENT            },
   { "ALL",                      DVI$_ALL                      },
   { "ALLDEVNAM",                DVI$_ALLDEVNAM                },
   { "ALLOCLASS",                DVI$_ALLOCLASS                },
   { "ALT_HOST_AVAIL",           DVI$_ALT_HOST_AVAIL           },
   { "ALT_HOST_NAME",            DVI$_ALT_HOST_NAME            },
   { "ALT_HOST_TYPE",            DVI$_ALT_HOST_TYPE            },
   { "AVAILABLE_PATH_COUNT",     DVI$_AVAILABLE_PATH_COUNT     },
   { "AVL",                      DVI$_AVL                      },
   { "CCL",                      DVI$_CCL                      },
   { "CLUSTER",                  DVI$_CLUSTER                  },
   { "CONCEALED",                DVI$_CONCEALED                }, /* undoc'ed */
   { "CYLINDERS",                DVI$_CYLINDERS                },
   { "DEVBUFSIZ",                DVI$_DEVBUFSIZ                },
   { "DEVCHAR",                  DVI$_DEVCHAR                  },
   { "DEVCHAR2",                 DVI$_DEVCHAR2                 },
   { "DEVCLASS",                 DVI$_DEVCLASS                 },
   { "DEVDEPEND",                DVI$_DEVDEPEND                },
   { "DEVDEPEND2",               DVI$_DEVDEPEND2               },
   { "DEVICE_MAX_IO_SIZE",       DVI$_DEVICE_MAX_IO_SIZE       },
   { "DEVICE_TYPE_NAME",         DVI$_DEVICE_TYPE_NAME         },
   { "DEVLOCKNAM",               DVI$_DEVLOCKNAM               },
   { "DEVNAM",                   DVI$_DEVNAM                   },
   { "DEVSTS",                   DVI$_DEVSTS                   },
   { "DEVTYPE",                  DVI$_DEVTYPE                  },
   { "DFS_ACCESS",               DVI$_DFS_ACCESS               },
   { "DIR",                      DVI$_DIR                      },
 /*  DVI$_DISPLAY_DEVNAM refered to in SS, not in LexFuncs */
   { "DMT",                      DVI$_DMT                      },
   { "DUA",                      DVI$_DUA                      },
   { "ELG",                      DVI$_ELG                      },
   { "ERASE_ON_DELETE",          DVI$_ERASE_ON_DELETE          },
   { "ERRCNT",                   DVI$_ERRCNT                   },
   { "ERROR_RESET_TIME",         DVI$_ERROR_RESET_TIME         },
   { "EXISTS",                   DVI$_DIR                      },
   { "EXPSIZE",                  DVI$_EXPSIZE                  },
   { "FC_HBA_FIRMWARE_REV",      DVI$_FC_HBA_FIRMWARE_REV      },
   { "FC_NODE_NAME",             DVI$_FC_NODE_NAME             },
   { "FC_PORT_NAME",             DVI$_FC_PORT_NAME             },
   { "FOD",                      DVI$_FOD                      },
   { "FOR",                      DVI$_FOR                      },
   { "FREEBLOCKS",               DVI$_FREEBLOCKS               },
   { "FULLDEVNAM",               DVI$_FULLDEVNAM               },
   { "GEN",                      DVI$_GEN                      },
   { "HARDLINKS_SUPPORTED",      DVI$_HARDLINKS_SUPPORTED      },
   { "HOST_AVAIL",               DVI$_HOST_AVAIL               },
   { "HOST_COUNT",               DVI$_HOST_COUNT               },
   { "HOST_NAME",                DVI$_HOST_NAME                },
   { "HOST_TYPE",                DVI$_HOST_TYPE                },
   { "IDV",                      DVI$_IDV                      },
   { "LAN_ALL_MULTICAST_MODE",   DVI$_LAN_ALL_MULTICAST_MODE   },
   { "LAN_AUTONEG_ENABLED",      DVI$_LAN_AUTONEG_ENABLED      },
   { "LAN_DEFAULT_MAC_ADDRESS",  DVI$_LAN_DEFAULT_MAC_ADDRESS  },
   { "LAN_FULL_DUPLEX",          DVI$_LAN_FULL_DUPLEX          },
   { "LAN_JUMBO_FRAMES_ENABLED", DVI$_LAN_JUMBO_FRAMES_ENABLED },
   { "LAN_LINK_STATE_VALID",     DVI$_LAN_LINK_STATE_VALID     },
   { "LAN_LINK_UP",              DVI$_LAN_LINK_UP              },
   { "LAN_MAC_ADDRESS",          DVI$_LAN_MAC_ADDRESS          },
   { "LAN_PROMISCUOUS_MODE",     DVI$_LAN_PROMISCUOUS_MODE     },
   { "LAN_PROTOCOL_NAME",        DVI$_LAN_PROTOCOL_NAME        },
   { "LAN_PROTOCOL_TYPE",        DVI$_LAN_PROTOCOL_TYPE        },
   { "LAN_SPEED",                DVI$_LAN_SPEED                },
   { "LOCKID",                   DVI$_LOCKID                   },
   { "LOGVOLNAM",                DVI$_LOGVOLNAM                },
   { "MAILBOX_BUFFER_QUOTA",     DVI$_MAILBOX_BUFFER_QUOTA     },
   { "MAILBOX_INITIAL_QUOTA",    DVI$_MAILBOX_INITIAL_QUOTA    },
   { "MAXBLOCK",                 DVI$_MAXBLOCK                 },
   { "MAXFILES",                 DVI$_MAXFILES                 },
   { "MBX",                      DVI$_MBX                      },
   { "MEDIA_ID",                 DVI$_MEDIA_ID                 },
   { "MEDIA_NAME",               DVI$_MEDIA_NAME               },
   { "MEDIA_TYPE",               DVI$_MEDIA_TYPE               },
   { "MNT",                      DVI$_MNT                      },
   { "MOUNTCNT",                 DVI$_MOUNTCNT                 },
   { "MOUNTCNT_CLUSTER",         DVI$_MOUNTCNT_CLUSTER         },
   { "MOUNTVER_ELIGIBLE",        DVI$_MOUNTVER_ELIGIBLE        },
   { "MOUNT_TIME",               DVI$_MOUNT_TIME               },
   { "MPDEV_AUTO_PATH_SW_CNT",   DVI$_MPDEV_AUTO_PATH_SW_CNT   },
   { "MPDEV_CURRENT_PATH",       DVI$_MPDEV_CURRENT_PATH       },
   { "MPDEV_MAN_PATH_SW_CNT",    DVI$_MPDEV_MAN_PATH_SW_CNT    },
   { "MT3_DENSITY",              DVI$_MT3_DENSITY              },
   { "MT3_SUPPORTED",            DVI$_MT3_SUPPORTED            },
   { "MULTIPATH",                DVI$_MULTIPATH                },
   { "MVSUPMSG",                 DVI$_MVSUPMSG                 },
   { "NET",                      DVI$_NET                      },
   { "NEXTDEVNAM",               DVI$_NEXTDEVNAM               },
   { "NOCACHE_ON_VOLUME",        DVI$_NOCACHE_ON_VOLUME        },
   { "NOHIGHWATER",              DVI$_NOHIGHWATER              },
   { "NOSHARE_MOUNTED",          DVI$_NOSHARE_MOUNTED          },
   { "NOXFCCACHE_ON_VOLUME",     DVI$_NOXFCCACHE_ON_VOLUME     },
   { "ODS2_SUBSET0",             DVI$_ODS2_SUBSET0             },
   { "ODS5",                     DVI$_ODS5                     },
   { "ODV",                      DVI$_ODV                      },
   { "OPCNT",                    DVI$_OPCNT                    },
   { "OPR",                      DVI$_OPR                      },
   { "OWNUIC",                   DVI$_OWNUIC                   },
   { "PATH_AVAILABLE",           DVI$_PATH_AVAILABLE           },
   { "PATH_NOT_RESPONDING",      DVI$_PATH_NOT_RESPONDING      },
   { "PATH_POLL_ENABLED",        DVI$_PATH_POLL_ENABLED        },
   { "PATH_SWITCH_FROM_TIME",    DVI$_PATH_SWITCH_FROM_TIME    },
   { "PATH_SWITCH_TO_TIME",      DVI$_PATH_SWITCH_TO_TIME      },
   { "PATH_USER_DISABLED",       DVI$_PATH_USER_DISABLED       },
   { "PID",                      DVI$_PID                      },
   { "PREFERRED_CPU",            DVI$_PREFERRED_CPU            },
   { "PREFERRED_CPU_BITMAP",     DVI$_PREFERRED_CPU_BITMAP     },
   { "PROT_SUBSYSTEM_ENABLED",   DVI$_PROT_SUBSYSTEM_ENABLED   },
   { "QLEN",                     DVI$_QLEN                     },
   { "RCK",                      DVI$_RCK                      },
   { "RCT",                      DVI$_RCT                      },
   { "REC",                      DVI$_REC                      },
   { "RECSIZ",                   DVI$_RECSIZ                   },
   { "REFCNT",                   DVI$_REFCNT                   },
   { "REMOTE_DEVICE",            DVI$_REMOTE_DEVICE            },
   { "RND",                      DVI$_RND                      },
   { "ROOTDEVNAM",               DVI$_ROOTDEVNAM               },
   { "RTM",                      DVI$_RTM                      },
   { "SCSI_DEVICE_FIRMWARE_REV", DVI$_SCSI_DEVICE_FIRMWARE_REV },
   { "SDI",                      DVI$_SDI                      },
   { "SECTORS",                  DVI$_SECTORS                  },
   { "SERIALNUM",                DVI$_SERIALNUM                },
   { "SERVED_DEVICE",            DVI$_SERVED_DEVICE            },
   { "SET_HOST_TERMINAL",        DVI$_SET_HOST_TERMINAL        },
   { "SHDW_CATCHUP_COPYING",     DVI$_SHDW_CATCHUP_COPYING     },
   { "SHDW_COPIER_NODE",         DVI$_SHDW_COPIER_NODE         },
   { "SHDW_DEVICE_COUNT",        DVI$_SHDW_DEVICE_COUNT        },
   { "SHDW_FAILED_MEMBER",       DVI$_SHDW_FAILED_MEMBER       },
   { "SHDW_GENERATION",          DVI$_SHDW_GENERATION          },
   { "SHDW_MASTER",              DVI$_SHDW_MASTER              },
   { "SHDW_MASTER_MBR",          DVI$_SHDW_MASTER_MBR          },
   { "SHDW_MASTER_NAME",         DVI$_SHDW_MASTER_NAME         },
   { "SHDW_MBR_COPY_DONE",       DVI$_SHDW_MBR_COPY_DONE       },
   { "SHDW_MBR_COUNT",           DVI$_SHDW_MBR_COUNT           },
   { "SHDW_MBR_MERGE_DONE",      DVI$_SHDW_MBR_MERGE_DONE      },
   { "SHDW_MBR_READ_COST",       DVI$_SHDW_MBR_READ_COST       },
   { "SHDW_MEMBER",              DVI$_SHDW_MEMBER              },
   { "SHDW_MERGE_COPYING",       DVI$_SHDW_MERGE_COPYING       },
   { "SHDW_MINIMERGE_ENABLE",    DVI$_SHDW_MINIMERGE_ENABLE    },
   { "SHDW_NEXT_MBR_NAME",       DVI$_SHDW_NEXT_MBR_NAME       },
   { "SHDW_READ_SOURCE",         DVI$_SHDW_READ_SOURCE         },
   { "SHDW_SITE",                DVI$_SHDW_SITE                },
   { "SHDW_TIMEOUT",             DVI$_SHDW_TIMEOUT             },
   { "SHR",                      DVI$_SHR                      },
   { "SPECIAL_FILES",            DVI$_SPECIAL_FILES            },
   { "SPL",                      DVI$_SPL                      },
   { "SPLDEVNAM",               (DVI$_DEVNAM | DVI$C_SECONDARY) },
   { "SQD",                      DVI$_SQD                      },
   { "SSD_LIFE_REMAINING",       DVI$_SSD_LIFE_REMAINING       },
   { "SSD_USAGE_REMAINING",      DVI$_SSD_USAGE_REMAINING      },
   { "STS",                      DVI$_STS                      },
   { "SWL",                      DVI$_SWL                      },
   { "TOTAL_PATH_COUNT",         DVI$_TOTAL_PATH_COUNT         },
   { "TRACKS",                   DVI$_TRACKS                   },
   { "TRANSCNT",                 DVI$_TRANSCNT                 },
   { "TRM",                      DVI$_TRM                      },
   { "TT_ACCPORNAM",             DVI$_TT_ACCPORNAM             },
   { "TT_ALTYPEAHD",             DVI$_TT_ALTYPEAHD             },
   { "TT_ANSICRT",               DVI$_TT_ANSICRT               },
   { "TT_APP_KEYPAD",            DVI$_TT_APP_KEYPAD            },
   { "TT_AUTOBAUD",              DVI$_TT_AUTOBAUD              },
   { "TT_AVO",                   DVI$_TT_AVO                   },
   { "TT_BLOCK",                 DVI$_TT_BLOCK                 },
   { "TT_BRDCSTMBX",             DVI$_TT_BRDCSTMBX             },
   { "TT_CHARSET",               DVI$_TT_CHARSET               },
   { "TT_CRFILL",                DVI$_TT_CRFILL                },
   { "TT_CS_HANGUL",             DVI$_TT_CS_HANGUL             },
   { "TT_CS_HANYU",              DVI$_TT_CS_HANYU              },
   { "TT_CS_HANZI",              DVI$_TT_CS_HANZI              },
   { "TT_CS_KANA",               DVI$_TT_CS_KANA               },
   { "TT_CS_KANJI",              DVI$_TT_CS_KANJI              },
   { "TT_CS_THAI",               DVI$_TT_CS_THAI               },
   { "TT_DECCRT",                DVI$_TT_DECCRT                },
   { "TT_DECCRT2",               DVI$_TT_DECCRT2               },
   { "TT_DECCRT3",               DVI$_TT_DECCRT3               },
   { "TT_DECCRT4",               DVI$_TT_DECCRT4               },
   { "TT_DECCRT5",               DVI$_TT_DECCRT5               },
   { "TT_DIALUP",                DVI$_TT_DIALUP                },
   { "TT_DISCONNECT",            DVI$_TT_DISCONNECT            },
   { "TT_DMA",                   DVI$_TT_DMA                   },
   { "TT_DRCS",                  DVI$_TT_DRCS                  },
   { "TT_EDIT",                  DVI$_TT_EDIT                  },
   { "TT_EDITING",               DVI$_TT_EDITING               },
   { "TT_EIGHTBIT",              DVI$_TT_EIGHTBIT              },
   { "TT_ESCAPE",                DVI$_TT_ESCAPE                },
   { "TT_FALLBACK",              DVI$_TT_FALLBACK              },
   { "TT_HALFDUP",               DVI$_TT_HALFDUP               },
   { "TT_HANGUP",                DVI$_TT_HANGUP                },
   { "TT_HOSTSYNC",              DVI$_TT_HOSTSYNC              },
   { "TT_INSERT",                DVI$_TT_INSERT                },
   { "TT_LFFILL",                DVI$_TT_LFFILL                },
   { "TT_LOCALECHO",             DVI$_TT_LOCALECHO             },
   { "TT_LOWER",                 DVI$_TT_LOWER                 },
   { "TT_MBXDSABL",              DVI$_TT_MBXDSABL              },
   { "TT_MECHFORM",              DVI$_TT_MECHFORM              },
   { "TT_MECHTAB",               DVI$_TT_MECHTAB               },
   { "TT_MODEM",                 DVI$_TT_MODEM                 },
   { "TT_MODHANGUP",             DVI$_TT_MODHANGUP             },
   { "TT_NOBRDCST",              DVI$_TT_NOBRDCST              },
   { "TT_NOECHO",                DVI$_TT_NOECHO                },
   { "TT_NOTYPEAHD",             DVI$_TT_NOTYPEAHD             },
   { "TT_OPER",                  DVI$_TT_OPER                  },
   { "TT_PAGE",                  DVI$_TT_PAGE                  },
   { "TT_PASTHRU",               DVI$_TT_PASTHRU               },
   { "TT_PHYDEVNAM",             DVI$_TT_PHYDEVNAM             },
   { "TT_PRINTER",               DVI$_TT_PRINTER               },
   { "TT_READSYNC",              DVI$_TT_READSYNC              },
   { "TT_REGIS",                 DVI$_TT_REGIS                 },
   { "TT_REMOTE",                DVI$_TT_REMOTE                },
   { "TT_SCOPE",                 DVI$_TT_SCOPE                 },
   { "TT_SECURE",                DVI$_TT_SECURE                },
   { "TT_SETSPEED",              DVI$_TT_SETSPEED              },
   { "TT_SIXEL",                 DVI$_TT_SIXEL                 },
   { "TT_SYSPWD",                DVI$_TT_SYSPWD                },
   { "TT_TTSYNC",                DVI$_TT_TTSYNC                },
   { "TT_WRAP",                  DVI$_TT_WRAP                  },
   { "UNIT",                     DVI$_UNIT                     },
   { "VOLCHAR",                  DVI$_VOLCHAR                  },
   { "VOLCOUNT",                 DVI$_VOLCOUNT                 },
   { "VOLNAM",                   DVI$_VOLNAM                   },
   { "VOLNUMBER",                DVI$_VOLNUMBER                },
   { "VOLSETMEM",                DVI$_VOLSETMEM                },
   { "VOLSIZE",                  DVI$_VOLSIZE                  },
   { "VOLUME_EXTEND_QUANTITY",   DVI$_VOLUME_EXTEND_QUANTITY   },
   { "VOLUME_MOUNT_GROUP",       DVI$_VOLUME_MOUNT_GROUP       },
   { "VOLUME_MOUNT_SYS",         DVI$_VOLUME_MOUNT_SYS         },
   { "VOLUME_PENDING_WRITE_ERR", DVI$_VOLUME_PENDING_WRITE_ERR },
   { "VOLUME_RETAIN_MAX",        DVI$_VOLUME_RETAIN_MAX        },
   { "VOLUME_RETAIN_MIN",        DVI$_VOLUME_RETAIN_MIN        },
   { "VOLUME_SPOOLED_DEV_CNT",   DVI$_VOLUME_SPOOLED_DEV_CNT   },
   { "VOLUME_WINDOW",            DVI$_VOLUME_WINDOW            },
   { "VPROT",                    DVI$_VPROT                    },
   { "WCK",                      DVI$_WCK                      },
   { "WRITETHRU_CACHE_ENABLED",  DVI$_WRITETHRU_CACHE_ENABLED  },
   { "WWID",                     DVI$_WWID                     },
   { "XFC_DEPOSING",             DVI$_XFC_DEPOSING             },
} ;

/* Binary search alphabetized string lists to find the corresponding
 * item code. Case-insensitive ASCII comparison. Pass the number of
 * array elements as the length.
 *
 * NOTE: DCL removes leading, trailing, and embedded whitespace before
 * searching for items, e.g. f$getsyi("n o d e n a m e  ", "   ") works.
 * This implementation doesn't perform whitespace removal.
 */
static const struct dvi_items_type *item_info(
     const char *name, int name_len, const struct dvi_items_type *xlist,
     int list_len )
{
   int top, bot, mid ;
   const char *poss ;

   top = list_len - 1 ;
   bot = 0 ;

   while ( bot<=top )
   {
      mid = (top+bot)/2 ;
      poss = xlist[mid].name ;

      int tmp = strncasecmp( name, poss, name_len );
      if (tmp < 0 || (tmp == 0 && poss[name_len] != '\0'))
         top = mid - 1 ;
      else if (tmp)
         bot = mid + 1 ;
      else
         return &(xlist[mid]) ;
   }
   return NULL ;
}


/*
 * Modified the following to use lib$get*() instead of sys$get*(), to eliminate
 * the code to decode return values into strings. We still have to convert the
 * input parameter strings into item codes, and there's an extra lib$getdvi()
 * call to check if the device is spooled, to determine whether to OR the item
 * code with DVI$C_SECONDARY (except for SPLDEVNAM), but the new code should be
 * easier to maintain, and it handles more result types (such as bitmaps).
 */

streng *vms_f_getdvi( tsd_t *TSD, cparamboxptr parms )
{
   char buffer[64] ;
   int spooled, itemcode ;
   unsigned int rc ;
   struct dsc$descriptor_s name ;
   struct dsc$descriptor_s result = {
      64, DSC$K_DTYPE_T, DSC$K_CLASS_S, buffer } ;
   const struct dvi_items_type *ptr ;

   checkparam( parms, 2, 2, "VMS_F_GETDVI" ) ;

   streng *item = parms->next->value ;
   ptr = item_info( item->value, item->len, dvi_items, ARRAY_LEN(dvi_items) ) ;
   if (!ptr)
      exiterror( ERR_INCORRECT_CALL , 0 ) ;

   name.dsc$w_length = parms->value->len ;
   name.dsc$b_dtype = DSC$K_DTYPE_T ;
   name.dsc$b_class = DSC$K_CLASS_S ;
   name.dsc$a_pointer = parms->value->value ;

   /* Get secondary characteristics for spooled devices, except SPLDEVNAM. */
   if ( ptr->item_code == (DVI$_DEVNAM | DVI$C_SECONDARY) ) {
      spooled = 0;
      itemcode = DVI$_DEVNAM;    /* query without DVI$C_SECONDARY */
   } else {
      itemcode = (DVI$_SPL | DVI$C_SECONDARY);  /* is spooled device? */
      rc = lib$getdvi( &itemcode, 0, &name, &spooled ) ;
      if (rc != SS$_NORMAL)
      {
         vms_error( TSD, rc ) ;
         return nullstringptr() ;
      }

      /* If the request was "SPL", we're done. */
      if ( ptr->item_code == DVI$_SPL ) {
         return boolean( TSD, spooled ) ;
      }

      itemcode = ptr->item_code | ( spooled ? DVI$C_SECONDARY : 0 ) ;
   }

   rc = lib$getdvi( &itemcode, 0, &name, 0, &result, &(result.dsc$w_length) ) ;

   if (rc != SS$_NORMAL)
   {
      vms_error( TSD, rc ) ;

      /* Return the truncated value if we got one, or else an empty string. */
      if ( rc != LIB$_STRTRU )
         return nullstringptr() ;
   }

   return Str_ncreTSD( result.dsc$a_pointer, result.dsc$w_length ) ;
}


static const struct dvi_items_type jpi_items[] =
{
   { "ACCOUNT",               JPI$_ACCOUNT               },
   { "APTCNT",                JPI$_APTCNT                },
   { "ASTACT",                JPI$_ASTACT                },
   { "ASTCNT",                JPI$_ASTCNT                },
   { "ASTEN",                 JPI$_ASTEN                 },
   { "ASTLM",                 JPI$_ASTLM                 },
   { "AUTHPRI",               JPI$_AUTHPRI               },
   { "AUTHPRIV",              JPI$_AUTHPRIV              },
   { "BIOCNT",                JPI$_BIOCNT                },
   { "BIOLM",                 JPI$_BIOLM                 },
   { "BUFIO",                 JPI$_BUFIO                 },
   { "BYTCNT",                JPI$_BYTCNT                },
   { "BYTLM",                 JPI$_BYTLM                 },
   { "CASE_LOOKUP_IMAGE",     JPI$_CASE_LOOKUP_IMAGE     },
   { "CASE_LOOKUP_PERM",      JPI$_CASE_LOOKUP_PERM      },
   { "CLASSIFICATION",        JPI$_CLASSIFICATION        },
   { "CLINAME",               JPI$_CLINAME               },
   { "CPULIM",                JPI$_CPULIM                },
   { "CPUTIM",                JPI$_CPUTIM                },
   { "CREPRC_FLAGS",          JPI$_CREPRC_FLAGS          },
   { "CURPRIV",               JPI$_CURPRIV               },
   { "CURRENT_AFFINITY_MASK", JPI$_CURRENT_AFFINITY_MASK },
   { "CURRENT_CAP_MASK",      JPI$_CURRENT_CAP_MASK      },
   { "CURRENT_USERCAP_MASK",  JPI$_CURRENT_USERCAP_MASK  },
   { "DEADLOCK_WAIT",         JPI$_DEADLOCK_WAIT         },
   { "DFPFC",                 JPI$_DFPFC                 },
   { "DFWSCNT",               JPI$_DFWSCNT               },
   { "DIOCNT",                JPI$_DIOCNT                },
   { "DIOLM",                 JPI$_DIOLM                 },
   { "DIRIO",                 JPI$_DIRIO                 },
   { "EFCS",                  JPI$_EFCS                  },
   { "EFCU",                  JPI$_EFCU                  },
   { "EFWM",                  JPI$_EFWM                  },
   { "ENQCNT",                JPI$_ENQCNT                },
   { "ENQLM",                 JPI$_ENQLM                 },
   { "EXCVEC",                JPI$_EXCVEC                },
   { "FAST_VP_SWITCH",        JPI$_FAST_VP_SWITCH        },
   { "FILCNT",                JPI$_FILCNT                },
   { "FILLM",                 JPI$_FILLM                 },
   { "FINALEXC",              JPI$_FINALEXC              },
   { "FREP0VA",               JPI$_FREP0VA               },
   { "FREP1VA",               JPI$_FREP1VA               },
   { "FREPTECNT",             JPI$_FREPTECNT             },
   { "GPGCNT",                JPI$_GPGCNT                },
   { "GRP",                   JPI$_GRP                   },
   { "HOME_RAD",              JPI$_HOME_RAD              },
   { "IMAGECOUNT",            JPI$_IMAGECOUNT            },
   { "IMAGE_AUTHPRIV",        JPI$_IMAGE_AUTHPRIV        },
   { "IMAGE_PERMPRIV",        JPI$_IMAGE_PERMPRIV        },
   { "IMAGE_WORKPRIV",        JPI$_IMAGE_WORKPRIV        },
   { "IMAGNAME",              JPI$_IMAGNAME              },
   { "IMAGPRIV",              JPI$_IMAGPRIV              },
   { "INSTALL_RIGHTS",        JPI$_INSTALL_RIGHTS        },
   { "INSTALL_RIGHTS_SIZE",   JPI$_INSTALL_RIGHTS_SIZE   },
   { "JOBPRCCNT",             JPI$_JOBPRCCNT             },
   { "JOBTYPE",               JPI$_JOBTYPE               },
   { "KT_COUNT",              JPI$_KT_COUNT              },
   { "KT_LIMIT",              JPI$_KT_LIMIT              },
   { "LAST_LOGIN_I",          JPI$_LAST_LOGIN_I          },
   { "LAST_LOGIN_N",          JPI$_LAST_LOGIN_N          },
   { "LOGINTIM",              JPI$_LOGINTIM              },
   { "LOGIN_FAILURES",        JPI$_LOGIN_FAILURES        },
   { "LOGIN_FLAGS",           JPI$_LOGIN_FLAGS           },
   { "MASTER_PID",            JPI$_MASTER_PID            },
   { "MAXDETACH",             JPI$_MAXDETACH             },
   { "MAXJOBS",               JPI$_MAXJOBS               },
   { "MEM",                   JPI$_MEM                   },
   { "MODE",                  JPI$_MODE                  },
   { "MSGMASK",               JPI$_MSGMASK               },
   { "MULTITHREAD",           JPI$_MULTITHREAD           },
   { "NODENAME",              JPI$_NODENAME              },
   { "NODE_CSID",             JPI$_NODE_CSID             },
   { "NODE_VERSION",          JPI$_NODE_VERSION          },
   { "OWNER",                 JPI$_OWNER                 },
   { "PAGEFLTS",              JPI$_PAGEFLTS              },
   { "PAGFILCNT",             JPI$_PAGFILCNT             },
   { "PAGFILLOC",             JPI$_PAGFILLOC             },
   { "PARSE_STYLE_IMAGE",     JPI$_PARSE_STYLE_IMAGE     },
   { "PARSE_STYLE_PERM",      JPI$_PARSE_STYLE_PERM      },
   { "PERMANENT_CAP_MASK",    JPI$_PERMANENT_CAP_MASK    },
   { "PERSONA_AUTHPRIV",      JPI$_PERSONA_AUTHPRIV      },
   { "PERSONA_ID",            JPI$_PERSONA_ID            },
   { "PERSONA_PERMPRIV",      JPI$_PERSONA_PERMPRIV      },
   { "PERSONA_RIGHTS",        JPI$_PERSONA_RIGHTS        },
   { "PERSONA_RIGHTS_SIZE",   JPI$_PERSONA_RIGHTS_SIZE   },
   { "PERSONA_WORKPRIV",      JPI$_PERSONA_WORKPRIV      },
   { "PGFLQUOTA",             JPI$_PGFLQUOTA             },
   { "PHDFLAGS",              JPI$_PHDFLAGS              },
   { "PID",                   JPI$_PID                   },
   { "PPGCNT",                JPI$_PPGCNT                },
   { "PRCCNT",                JPI$_PRCCNT                },
   { "PRCLM",                 JPI$_PRCLM                 },
   { "PRCNAM",                JPI$_PRCNAM                },
   { "PRI",                   JPI$_PRI                   },
   { "PRIB",                  JPI$_PRIB                  },
   { "PROCESS_RIGHTS",        JPI$_PROCESS_RIGHTS        },
   { "PROCPRIV",              JPI$_PROCPRIV              },
   { "PROC_INDEX",            JPI$_PROC_INDEX            },
   { "RIGHTSLIST",            JPI$_RIGHTSLIST            },
   { "RIGHTS_SIZE",           JPI$_RIGHTS_SIZE           },
   { "SCHED_CLASS_NAME",      JPI$_SCHED_CLASS_NAME      },
   { "SCHED_POLICY",          JPI$_SCHED_POLICY          },
   { "SEARCH_SYMLINK_PERM",   JPI$_SEARCH_SYMLINK_PERM   },
   { "SEARCH_SYMLINK_TEMP",   JPI$_SEARCH_SYMLINK_TEMP   },
   { "SHRFILLM",              JPI$_SHRFILLM              },
   { "SITESPEC",              JPI$_SITESPEC              },
   { "SLOW_VP_SWITCH",        JPI$_SLOW_VP_SWITCH        },
   { "STATE",                 JPI$_STATE                 },
   { "STS",                   JPI$_STS                   },
   { "STS2",                  JPI$_STS2                  },
   { "SUBSYSTEM_RIGHTS",      JPI$_SUBSYSTEM_RIGHTS      },
   { "SUBSYSTEM_RIGHTS_SIZE", JPI$_SUBSYSTEM_RIGHTS_SIZE },
   { "SWPFILLOC",             JPI$_SWPFILLOC             },
   { "SYSTEM_RIGHTS",         JPI$_SYSTEM_RIGHTS         },
   { "SYSTEM_RIGHTS_SIZE",    JPI$_SYSTEM_RIGHTS_SIZE    },
   { "TABLENAME",             JPI$_TABLENAME             },
   { "TERMINAL",              JPI$_TERMINAL              },
   { "TMBU",                  JPI$_TMBU                  },
   { "TOKEN",                 JPI$_TOKEN                 },
   { "TQCNT",                 JPI$_TQCNT                 },
   { "TQLM",                  JPI$_TQLM                  },
   { "TT_ACCPORNAM",          JPI$_TT_ACCPORNAM          },
   { "TT_PHYDEVNAM",          JPI$_TT_PHYDEVNAM          },
   { "UAF_FLAGS",             JPI$_UAF_FLAGS             },
   { "UIC",                   JPI$_UIC                   },
   { "USERNAME",              JPI$_USERNAME              },
   { "VIRTPEAK",              JPI$_VIRTPEAK              },
   { "VOLUMES",               JPI$_VOLUMES               },
   { "WSAUTH",                JPI$_WSAUTH                },
   { "WSAUTHEXT",             JPI$_WSAUTHEXT             },
   { "WSEXTENT",              JPI$_WSEXTENT              },
   { "WSPEAK",                JPI$_WSPEAK                },
   { "WSQUOTA",               JPI$_WSQUOTA               },
   { "WSSIZE",                JPI$_WSSIZE                },
} ;


streng *vms_f_getjpi( tsd_t *TSD, cparamboxptr parms )
{
   char buffer[64] ;
   int itemcode ;
   unsigned int rc, pid, *pidaddr ;
   struct dsc$descriptor_s result = {
      64, DSC$K_DTYPE_T, DSC$K_CLASS_S, buffer } ;
   const struct dvi_items_type *ptr ;

   checkparam( parms, 2, 2, "VMS_F_GETJPI" ) ;

   streng *item = parms->next->value ;
   ptr = item_info( item->value, item->len, jpi_items, ARRAY_LEN(jpi_items) ) ;
   if (!ptr)
      exiterror( ERR_INCORRECT_CALL , 0 ) ;

   if ((!parms->value) || (!parms->value->len))
      pidaddr = NULL ;
   else
   {
      pid = read_pid( parms->value ) ;
      pidaddr = &pid ;
   }

   itemcode = ptr->item_code ;   /* make a non-const copy */

   rc = lib$getjpi( &itemcode, pidaddr, NULL, NULL, &result,
                    &(result.dsc$w_length) ) ;

   if (rc != SS$_NORMAL)
   {
      vms_error( TSD, rc ) ;

      /* Return the truncated value if we got one, or else an empty string. */
      if ( rc != LIB$_STRTRU )
         return nullstringptr() ;
   }

   return Str_ncreTSD( result.dsc$a_pointer, result.dsc$w_length ) ;
}

/*
 * Warning, the sequence of these records *must* match the macros
 * given below (DCHAR, ENTRY, DFILE), which is used in initializing
 * the array legal_items_and_types
 */
static const struct dvi_items_type qui_funcs[] = {
   { "CANCEL_OPERATION",       QUI$_CANCEL_OPERATION },
   { "DISPLAY_CHARACTERISTIC", QUI$_DISPLAY_CHARACTERISTIC },
   { "DISPLAY_ENTRY",          QUI$_DISPLAY_ENTRY },
   { "DISPLAY_FILE",           QUI$_DISPLAY_FILE },
   { "DISPLAY_FORM",           QUI$_DISPLAY_FORM },
   { "DISPLAY_JOB",            QUI$_DISPLAY_JOB },
   { "DISPLAY_MANAGER",        QUI$_DISPLAY_MANAGER },
   { "DISPLAY_QUEUE",          QUI$_DISPLAY_QUEUE },
   { "TRANSLATE_QUEUE",        QUI$_TRANSLATE_QUEUE },
} ;

#define DCHAR        0x0001   /* display_characteristics */
#define ENTRY        0x0002   /* display_entry */
#define DFILE        0x0004   /* display_file */
#define FORM         0x0008   /* display_form */
#define JOB          0x0010   /* display_job */
#define DMGR         0x0020   /* display_manager */
#define QUEUE        0x0040   /* display_queue */
#define TRANS        0x0080   /* translate_queue */

/* Some item codes are actually flag bits of bit vector items. */
#define FILE_FLAGS_TYPE    0
#define FILE_STATUS_TYPE   1
#define FORM_FLAGS_TYPE    2
#define JOB_FLAGS_TYPE     3
#define JOB_STATUS_TYPE    4
#define MGR_STATUS_TYPE    5
#define PEND_REASON_TYPE   6
#define QUEUE_FLAGS_TYPE   7
#define QUEUE_STATUS_TYPE  8

#define FLAG_TYPE          0x0100
#define IS_FLAG_TYPE(t)    (t & FLAG_TYPE)
#define TYPE_OF_FLAG(t)    ((t >> 9) & 0x0f)
#define FILE_FLAGS   (FLAG_TYPE | (FILE_FLAGS_TYPE    << 9))
#define FILE_STATUS  (FLAG_TYPE | (FILE_STATUS_TYPE   << 9))
#define FORM_FLAGS   (FLAG_TYPE | (FORM_FLAGS_TYPE    << 9))
#define JOB_FLAGS    (FLAG_TYPE | (JOB_FLAGS_TYPE     << 9))
#define JOB_STATUS   (FLAG_TYPE | (JOB_STATUS_TYPE    << 9))
#define MGR_STATUS   (FLAG_TYPE | (MGR_STATUS_TYPE    << 9))
#define PEND_REASON  (FLAG_TYPE | (PEND_REASON_TYPE   << 9))
#define QUEUE_FLAGS  (FLAG_TYPE | (QUEUE_FLAGS_TYPE   << 9))
#define QUEUE_STATUS (FLAG_TYPE | (QUEUE_STATUS_TYPE  << 9))

/* These must be in the same order as the flag types defined above */
static const int qui_flag_item[] = {
   QUI$_FILE_FLAGS,
   QUI$_FILE_STATUS,
   QUI$_FORM_FLAGS,
   QUI$_JOB_FLAGS,
   QUI$_JOB_STATUS,
   QUI$_PENDING_JOB_REASON,
   QUI$_QUEUE_FLAGS,
   QUI$_QUEUE_STATUS
} ;

static const unsigned short legal_items_and_types[] = {
                  ENTRY | JOB,                  /* ACCOUNT_NAME */
                  ENTRY | JOB,                  /* AFTER_TIME */
                ENTRY | QUEUE,                  /* ASSIGNED_QUEUE_NAME */
                        QUEUE,                  /* AUTOSTART_ON */
                        QUEUE,                  /* BASE_PRIORITY */
          ENTRY | JOB | QUEUE,                  /* CHARACTERISTICS */
                        DCHAR,                  /* CHARACTERISTIC_NAME */
                        DCHAR,                  /* CHARACTERISTIC_NUMBER */
                  ENTRY | JOB,                  /* CHECKPOINT_DATA */
                  ENTRY | JOB,                  /* CLI */
                  ENTRY | JOB,                  /* COMPLETED_BLOCKS */
                  ENTRY | JOB,                  /* CONDITION_VECTOR */
                        QUEUE,                  /* CPU_DEFAULT */
          ENTRY | JOB | QUEUE,                  /* CPU_LIMIT */
                        QUEUE,                  /* DEFAULT_FORM_NAME */
                        QUEUE,                  /* DEFAULT_FORM_STOCK */
                        QUEUE,                  /* DEVICE_NAME */
                  ENTRY | JOB,                  /* ENTRY_NUMBER */
                        QUEUE,                  /* EXECUTING_JOB_COUNT */
                        DFILE | FILE_FLAGS,     /* FILE_BURST */
                        DFILE | FILE_STATUS,    /* FILE_CHECKPOINTED */
                        DFILE,                  /* FILE_COPIES */
                        DFILE,                  /* FILE_COPIES_DONE */
                  ENTRY | JOB,                  /* FILE_COUNT */
                        DFILE | FILE_FLAGS,     /* FILE_DELETE */
 /* TODO: "FILE_DEVICE" (NAM$T_DVI) and "FILE_DID" (NAM$W_DID) */
                        DFILE | FILE_FLAGS,     /* FILE_DOUBLE_SPACE */
                        DFILE | FILE_STATUS,    /* FILE_EXECUTING */
                        DFILE | FILE_FLAGS,     /* FILE_FLAG */
                        DFILE,                  /* FILE_FLAGS */
                        DFILE,                  /* FILE_IDENTIFICATION */
                        DFILE | FILE_FLAGS,     /* FILE_PAGE_HEADER */
                        DFILE | FILE_FLAGS,     /* FILE_PAGINATE */
                        DFILE | FILE_FLAGS,     /* FILE_PASSALL */
                        DFILE,                  /* FILE_SETUP_MODULES */
                        DFILE,                  /* FILE_SPECIFICATION */
                        DFILE,                  /* FILE_STATUS */
                        DFILE | FILE_FLAGS,     /* FILE_TRAILER */
                        DFILE,                  /* FIRST_PAGE */
                         FORM,                  /* FORM_DESCRIPTION */
                         FORM,                  /* FORM_FLAGS */
                         FORM,                  /* FORM_LENGTH */
                         FORM,                  /* FORM_MARGIN_BOTTOM */
                         FORM,                  /* FORM_MARGIN_LEFT */
                         FORM,                  /* FORM_MARGIN_RIGHT */
                         FORM,                  /* FORM_MARGIN_TOP */
   FORM | ENTRY | JOB | QUEUE,                  /* FORM_NAME */
                         FORM,                  /* FORM_NUMBER */
                         FORM,                  /* FORM_SETUP_MODULES */
                         FORM | FORM_FLAGS,     /* FORM_SHEET_FEED */
   FORM | ENTRY | JOB | QUEUE,                  /* FORM_STOCK */
                         FORM | FORM_FLAGS,     /* FORM_TRUNCATE */
                         FORM,                  /* FORM_WIDTH */
                         FORM | FORM_FLAGS,     /* FORM_WRAP */
                        QUEUE,                  /* GENERIC_TARGET */
                        QUEUE,                  /* HOLDING_JOB_COUNT */
                          JOB,                  /* INTERVENING_BLOCKS */
                          JOB,                  /* INTERVENING_JOBS */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_ABORTING */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_COMPLETING */
                  ENTRY | JOB,                  /* JOB_COMPLETION_QUEUE */
                  ENTRY | JOB,                  /* JOB_COMPLETION_TIME */
                  ENTRY | JOB,                  /* JOB_COPIES */
                  ENTRY | JOB,                  /* JOB_COPIES_DONE */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_CPU_LIMIT */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_ERROR_RETENTION */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_EXECUTING */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_FILE_BURST */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_FILE_BURST_ONE */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_FILE_FLAG */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_FILE_FLAG_ONE */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_FILE_PAGINATE */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_FILE_TRAILER */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_FILE_TRAILER_ONE */
                  ENTRY | JOB,                  /* JOB_FLAGS */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_HOLDING */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_INACCESSIBLE */
                        QUEUE,                  /* JOB_LIMIT */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_LOG_DELETE */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_LOG_NULL */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_LOG_SPOOL */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_LOWERCASE */
                  ENTRY | JOB,                  /* JOB_NAME */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_NOTIFY */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_PENDING */
                  ENTRY | JOB,                  /* JOB_PID */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_REFUSED */
                        QUEUE,                  /* JOB_RESET_MODULES */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_RESTART */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_RETAINED */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_RETENTION */
                  ENTRY | JOB,                  /* JOB_RETENTION_TIME */
                  ENTRY | JOB,                  /* JOB_SIZE */
                        QUEUE,                  /* JOB_SIZE_MAXIMUM */
                        QUEUE,                  /* JOB_SIZE_MINIMUM */
                  ENTRY | JOB,                  /* JOB_STALLED */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_STARTING */
                  ENTRY | JOB,                  /* JOB_STATUS */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_SUSPENDED */
                  ENTRY | JOB | JOB_STATUS,     /* JOB_TIMED_RELEASE */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_WSDEFAULT */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_WSEXTENT */
                  ENTRY | JOB | JOB_FLAGS,      /* JOB_WSQUOTA */
                        DFILE,                  /* LAST_PAGE */
                        QUEUE,                  /* LIBRARY_SPECIFICATION */
                  ENTRY | JOB,                  /* LOG_QUEUE */
                  ENTRY | JOB,                  /* LOG_SPECIFICATION */
                         DMGR | MGR_STATUS,     /* MANAGER_FAILOVER */
                         DMGR,                  /* MANAGER_NAME */
                         DMGR,                  /* MANAGER_NODES */
                         DMGR | MGR_STATUS,     /* MANAGER_RUNNING */
                         DMGR | MGR_STATUS,     /* MANAGER_STOPPED */
                         DMGR | MGR_STATUS,     /* MANAGER_STOPPING */
                         DMGR | MGR_STATUS,     /* MANAGER_STARTING */
                         DMGR | MGR_STATUS,     /* MANAGER_START_PENDING */
                         DMGR,                  /* MANAGER_STATUS */
                  ENTRY | JOB,                  /* NOTE */
                  ENTRY | JOB,                  /* OPERATOR_REQUEST */
                        QUEUE,                  /* OWNER_UIC */
                         FORM,                  /* PAGE_SETUP_MODULES */
                  ENTRY | JOB,                  /* PARAMETER_1 */
                  ENTRY | JOB,                  /* PARAMETER_2 */
                  ENTRY | JOB,                  /* PARAMETER_3 */
                  ENTRY | JOB,                  /* PARAMETER_4 */
                  ENTRY | JOB,                  /* PARAMETER_5 */
                  ENTRY | JOB,                  /* PARAMETER_6 */
                  ENTRY | JOB,                  /* PARAMETER_7 */
                  ENTRY | JOB,                  /* PARAMETER_8 */
                        QUEUE,                  /* PENDING_JOB_BLOCK_COUNT */
                        QUEUE,                  /* PENDING_JOB_COUNT */
                  ENTRY | JOB,                  /* PENDING_JOB_REASON */
                  ENTRY | JOB | PEND_REASON,    /* PEND_CHAR_MISMATCH */
                  ENTRY | JOB | PEND_REASON,    /* PEND_JOB_SIZE_MAX */
                  ENTRY | JOB | PEND_REASON,    /* PEND_JOB_SIZE_MIN */
                  ENTRY | JOB | PEND_REASON,    /* PEND_LOWERCASE_MISMATCH */
                  ENTRY | JOB | PEND_REASON,    /* PEND_NO_ACCESS */
                  ENTRY | JOB | PEND_REASON,    /* PEND_QUEUE_BUSY */
                  ENTRY | JOB | PEND_REASON,    /* PEND_QUEUE_STATE */
                  ENTRY | JOB | PEND_REASON,    /* PEND_STOCK_MISMATCH */
                  ENTRY | JOB,                  /* PRIORITY */
                ENTRY | QUEUE,                  /* PROCESSOR */
                        QUEUE,                  /* PROTECTION */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_ACL_SPECIFIED */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_ALIGNING */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_AUTOSTART */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_AUTOSTART_INACTIVE */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_AVAILABLE */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_BATCH */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_BUSY */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_CLOSED */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_CPU_DEFAULT */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_CPU_LIMIT */
                        QUEUE,                  /* QUEUE_DESCRIPTION */
                         DMGR,                  /* QUEUE_DIRECTORY */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_DISABLED */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_FILE_BURST */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_FILE_BURST_ONE */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_FILE_FLAG */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_FILE_FLAG_ONE */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_FILE_PAGINATE */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_FILE_TRAILER */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_FILE_TRAILER_ONE */
                ENTRY | QUEUE,                  /* QUEUE_FLAGS */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_GENERIC */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_GENERIC_SELECTION */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_IDLE */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_JOB_BURST */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_JOB_FLAG */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_JOB_SIZE_SCHED */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_JOB_TRAILER */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_LOWERCASE */
  TRANS | ENTRY | JOB | QUEUE,                  /* QUEUE_NAME */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_NO_INITIAL_FF */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_PAUSED */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_PAUSING */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_PRINTER */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_RAD */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_RECORD_BLOCKING */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_REMOTE */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_RESETTING */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_RESUMING */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_RETAIN_ALL */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_RETAIN_ERROR */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_SERVER */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_STALLED */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_STARTING */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_STATUS */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_STOPPED */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_STOPPING */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_STOP_PENDING */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_SWAP */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_TERMINAL */
                ENTRY | QUEUE | QUEUE_STATUS,   /* QUEUE_UNAVAILABLE */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_WSDEFAULT */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_WSEXTENT */
                ENTRY | QUEUE | QUEUE_FLAGS,    /* QUEUE_WSQUOTA */
          ENTRY | JOB | QUEUE,                  /* RAD */
                  ENTRY | JOB,                  /* REQUEUE_QUEUE_NAME */
                  ENTRY | JOB,                  /* RESTART_QUEUE_NAME */
                        QUEUE,                  /* RETAINED_JOB_COUNT */
                 DMGR | QUEUE,                  /* SCSNODE_NAME */
                        QUEUE,                  /* SECURITY_INACCESSIBLE */
                  ENTRY | JOB,                  /* SUBMISSION_TIME */
                        QUEUE,                  /* TIMED_RELEASE_JOB_COUNT */
                  ENTRY | JOB,                  /* UIC */
                  ENTRY | JOB,                  /* USERNAME */
          ENTRY | JOB | QUEUE,                  /* WSDEFAULT */
          ENTRY | JOB | QUEUE,                  /* WSEXTENT */
          ENTRY | JOB | QUEUE,                  /* WSQUOTA */
} ;


static const struct dvi_items_type qui_items[] = {
   { "ACCOUNT_NAME",             QUI$_ACCOUNT_NAME               },
   { "AFTER_TIME",               QUI$_AFTER_TIME                 },
   { "ASSIGNED_QUEUE_NAME",      QUI$_ASSIGNED_QUEUE_NAME        },
   { "AUTOSTART_ON",             QUI$_AUTOSTART_ON               },
   { "BASE_PRIORITY",            QUI$_BASE_PRIORITY              },
   { "CHARACTERISTICS",          QUI$_CHARACTERISTICS            },
   { "CHARACTERISTIC_NAME",      QUI$_CHARACTERISTIC_NAME        },
   { "CHARACTERISTIC_NUMBER",    QUI$_CHARACTERISTIC_NUMBER      },
   { "CHECKPOINT_DATA",          QUI$_CHECKPOINT_DATA            },
   { "CLI",                      QUI$_CLI                        },
   { "COMPLETED_BLOCKS",         QUI$_COMPLETED_BLOCKS           },
   { "CONDITION_VECTOR",         QUI$_CONDITION_VECTOR           },
   { "CPU_DEFAULT",              QUI$_CPU_DEFAULT                },
   { "CPU_LIMIT",                QUI$_CPU_LIMIT                  },
   { "DEFAULT_FORM_NAME",        QUI$_DEFAULT_FORM_NAME          },
   { "DEFAULT_FORM_STOCK",       QUI$_DEFAULT_FORM_STOCK         },
   { "DEVICE_NAME",              QUI$_DEVICE_NAME                },
   { "ENTRY_NUMBER",             QUI$_ENTRY_NUMBER               },
   { "EXECUTING_JOB_COUNT",      QUI$_EXECUTING_JOB_COUNT        },
   { "FILE_BURST",               QUI$M_FILE_BURST                },
   { "FILE_CHECKPOINTED",        QUI$M_FILE_CHECKPOINTED         },
   { "FILE_COPIES",              QUI$_FILE_COPIES                },
   { "FILE_COPIES_DONE",         QUI$_FILE_COPIES_DONE           },
   { "FILE_COUNT",               QUI$_FILE_COUNT                 },
   { "FILE_DELETE",              QUI$M_FILE_DELETE               },
 /* TODO: "FILE_DEVICE" (NAM$T_DVI) and "FILE_DID" (NAM$W_DID) */
   { "FILE_DOUBLE_SPACE",        QUI$M_FILE_DOUBLE_SPACE         },
   { "FILE_EXECUTING",           QUI$M_FILE_EXECUTING            },
   { "FILE_FLAG",                QUI$M_FILE_FLAG                 },
   { "FILE_FLAGS",               QUI$_FILE_FLAGS                 },
   { "FILE_IDENTIFICATION",      QUI$_FILE_IDENTIFICATION        },
   { "FILE_PAGE_HEADER",         QUI$M_FILE_PAGE_HEADER          },
   { "FILE_PAGINATE",            QUI$M_FILE_PAGINATE             },
   { "FILE_PASSALL",             QUI$M_FILE_PASSALL              },
   { "FILE_SETUP_MODULES",       QUI$_FILE_SETUP_MODULES         },
   { "FILE_SPECIFICATION",       QUI$_FILE_SPECIFICATION         },
   { "FILE_STATUS",              QUI$_FILE_STATUS                },
   { "FILE_TRAILER",             QUI$M_FILE_TRAILER              },
   { "FIRST_PAGE",               QUI$_FIRST_PAGE                 },
   { "FORM_DESCRIPTION",         QUI$_FORM_DESCRIPTION           },
   { "FORM_FLAGS",               QUI$_FORM_FLAGS                 },
   { "FORM_LENGTH",              QUI$_FORM_LENGTH                },
   { "FORM_MARGIN_BOTTOM",       QUI$_FORM_MARGIN_BOTTOM         },
   { "FORM_MARGIN_LEFT",         QUI$_FORM_MARGIN_LEFT           },
   { "FORM_MARGIN_RIGHT",        QUI$_FORM_MARGIN_RIGHT          },
   { "FORM_MARGIN_TOP",          QUI$_FORM_MARGIN_TOP            },
   { "FORM_NAME",                QUI$_FORM_NAME                  },
   { "FORM_NUMBER",              QUI$_FORM_NUMBER                },
   { "FORM_SETUP_MODULES",       QUI$_FORM_SETUP_MODULES         },
   { "FORM_SHEET_FEED",          QUI$M_FORM_SHEET_FEED           },
   { "FORM_STOCK",               QUI$_FORM_STOCK                 },
   { "FORM_TRUNCATE",            QUI$M_FORM_TRUNCATE             },
   { "FORM_WIDTH",               QUI$_FORM_WIDTH                 },
   { "FORM_WRAP",                QUI$M_FORM_WRAP                 },
   { "GENERIC_TARGET",           QUI$_GENERIC_TARGET             },
   { "HOLDING_JOB_COUNT",        QUI$_HOLDING_JOB_COUNT          },
   { "INTERVENING_BLOCKS",       QUI$_INTERVENING_BLOCKS         },
   { "INTERVENING_JOBS",         QUI$_INTERVENING_JOBS           },
   { "JOB_ABORTING",             QUI$M_JOB_ABORTING              },
   { "JOB_COMPLETING",           QUI$M_JOB_COMPLETING            }, /* undoc'ed */
   { "JOB_COMPLETION_QUEUE",     QUI$_JOB_COMPLETION_QUEUE       },
   { "JOB_COMPLETION_TIME",      QUI$_JOB_COMPLETION_TIME        },
   { "JOB_COPIES",               QUI$_JOB_COPIES                 },
   { "JOB_COPIES_DONE",          QUI$_JOB_COPIES_DONE            },
   { "JOB_CPU_LIMIT",            QUI$M_JOB_CPU_LIMIT             },
   { "JOB_ERROR_RETENTION",      QUI$M_JOB_ERROR_RETENTION       },
   { "JOB_EXECUTING",            QUI$M_JOB_EXECUTING             },
   { "JOB_FILE_BURST",           QUI$M_JOB_FILE_BURST            },
   { "JOB_FILE_BURST_ONE",       QUI$M_JOB_FILE_BURST_ONE        },
   { "JOB_FILE_FLAG",            QUI$M_JOB_FILE_FLAG             },
   { "JOB_FILE_FLAG_ONE",        QUI$M_JOB_FILE_FLAG_ONE         },
   { "JOB_FILE_PAGINATE",        QUI$M_JOB_FILE_PAGINATE         },
   { "JOB_FILE_TRAILER",         QUI$M_JOB_FILE_TRAILER          },
   { "JOB_FILE_TRAILER_ONE",     QUI$M_JOB_FILE_TRAILER_ONE      },
   { "JOB_FLAGS",                QUI$_JOB_FLAGS                  },
   { "JOB_HOLDING",              QUI$M_JOB_HOLDING               },
   { "JOB_INACCESSIBLE",         QUI$M_JOB_INACCESSIBLE          },
   { "JOB_LIMIT",                QUI$_JOB_LIMIT                  },
   { "JOB_LOG_DELETE",           QUI$M_JOB_LOG_DELETE            },
   { "JOB_LOG_NULL",             QUI$M_JOB_LOG_NULL              },
   { "JOB_LOG_SPOOL",            QUI$M_JOB_LOG_SPOOL             },
   { "JOB_LOWERCASE",            QUI$M_JOB_LOWERCASE             },
   { "JOB_NAME",                 QUI$_JOB_NAME                   },
   { "JOB_NOTIFY",               QUI$M_JOB_NOTIFY                },
   { "JOB_PENDING",              QUI$M_JOB_PENDING               },
   { "JOB_PID",                  QUI$_JOB_PID                    },
   { "JOB_REFUSED",              QUI$M_JOB_REFUSED               },
   { "JOB_RESET_MODULES",        QUI$_JOB_RESET_MODULES          },
   { "JOB_RESTART",              QUI$M_JOB_RESTART               },
   { "JOB_RETAINED",             QUI$M_JOB_RETAINED              },
   { "JOB_RETENTION",            QUI$M_JOB_RETENTION             },
   { "JOB_RETENTION_TIME",       QUI$_JOB_RETENTION_TIME         },
   { "JOB_SIZE",                 QUI$_JOB_SIZE                   },
   { "JOB_SIZE_MAXIMUM",         QUI$_JOB_SIZE_MAXIMUM           },
   { "JOB_SIZE_MINIMUM",         QUI$_JOB_SIZE_MINIMUM           },
   { "JOB_STALLED",              QUI$M_JOB_STALLED               },
   { "JOB_STARTING",             QUI$M_JOB_STARTING              },
   { "JOB_STATUS",               QUI$_JOB_STATUS                 },
   { "JOB_SUSPENDED",            QUI$M_JOB_SUSPENDED             },
   { "JOB_TIMED_RELEASE",        QUI$M_JOB_TIMED_RELEASE         },
   { "JOB_WSDEFAULT",            QUI$M_JOB_WSDEFAULT             },
   { "JOB_WSEXTENT",             QUI$M_JOB_WSEXTENT              },
   { "JOB_WSQUOTA",              QUI$M_JOB_WSQUOTA               },
   { "LAST_PAGE",                QUI$_LAST_PAGE                  },
   { "LIBRARY_SPECIFICATION",    QUI$_LIBRARY_SPECIFICATION      },
   { "LOG_QUEUE",                QUI$_LOG_QUEUE                  },
   { "LOG_SPECIFICATION",        QUI$_LOG_SPECIFICATION          },
   { "MANAGER_FAILOVER",         QUI$M_MANAGER_FAILOVER          },
   { "MANAGER_NAME",             QUI$_MANAGER_NAME               },
   { "MANAGER_NODES",            QUI$_MANAGER_NODES              },
   { "MANAGER_RUNNING",          QUI$M_MANAGER_RUNNING           },
   { "MANAGER_STARTING",         QUI$M_MANAGER_STARTING          },
   { "MANAGER_START_PENDING",    QUI$M_MANAGER_START_PENDING     },
   { "MANAGER_STATUS",           QUI$_MANAGER_STATUS             },
   { "MANAGER_STOPPED",          QUI$M_MANAGER_STOPPED           },
   { "MANAGER_STOPPING",         QUI$M_MANAGER_STOPPING          },
   { "NOTE",                     QUI$_NOTE                       },
   { "OPERATOR_REQUEST",         QUI$_OPERATOR_REQUEST           },
   { "OWNER_UIC",                QUI$_OWNER_UIC                  },
   { "PAGE_SETUP_MODULES",       QUI$_PAGE_SETUP_MODULES         },
   { "PARAMETER_1",              QUI$_PARAMETER_1                },
   { "PARAMETER_2",              QUI$_PARAMETER_2                },
   { "PARAMETER_3",              QUI$_PARAMETER_3                },
   { "PARAMETER_4",              QUI$_PARAMETER_4                },
   { "PARAMETER_5",              QUI$_PARAMETER_5                },
   { "PARAMETER_6",              QUI$_PARAMETER_6                },
   { "PARAMETER_7",              QUI$_PARAMETER_7                },
   { "PARAMETER_8",              QUI$_PARAMETER_8                },
   { "PENDING_JOB_BLOCK_COUNT",  QUI$_PENDING_JOB_BLOCK_COUNT    },
   { "PENDING_JOB_COUNT",        QUI$_PENDING_JOB_COUNT          },
   { "PENDING_JOB_REASON",       QUI$_PENDING_JOB_REASON         },
   { "PEND_CHAR_MISMATCH",       QUI$M_PEND_CHAR_MISMATCH        },
   { "PEND_JOB_SIZE_MAX",        QUI$M_PEND_JOB_SIZE_MAX         },
   { "PEND_JOB_SIZE_MIN",        QUI$M_PEND_JOB_SIZE_MIN         },
   { "PEND_LOWERCASE_MISMATCH",  QUI$M_PEND_LOWERCASE_MISMATCH   },
   { "PEND_NO_ACCESS",           QUI$M_PEND_NO_ACCESS            },
   { "PEND_QUEUE_BUSY",          QUI$M_PEND_QUEUE_BUSY           },
   { "PEND_QUEUE_STATE",         QUI$M_PEND_QUEUE_STATE          },
   { "PEND_STOCK_MISMATCH",      QUI$M_PEND_STOCK_MISMATCH       },
   { "PRIORITY",                 QUI$_PRIORITY                   },
   { "PROCESSOR",                QUI$_PROCESSOR                  },
   { "PROTECTION",               QUI$_PROTECTION                 },
   { "QUEUE_ACL_SPECIFIED",      QUI$M_QUEUE_ACL_SPECIFIED       },
   { "QUEUE_ALIGNING",           QUI$M_QUEUE_ALIGNING            },
   { "QUEUE_AUTOSTART",          QUI$M_QUEUE_AUTOSTART           },
   { "QUEUE_AUTOSTART_INACTIVE", QUI$M_QUEUE_AUTOSTART_INACTIVE  },
   { "QUEUE_AVAILABLE",          QUI$M_QUEUE_AVAILABLE           },
   { "QUEUE_BATCH",              QUI$M_QUEUE_BATCH               },
   { "QUEUE_BUSY",               QUI$M_QUEUE_BUSY                },
   { "QUEUE_CLOSED",             QUI$M_QUEUE_CLOSED              },
   { "QUEUE_CPU_DEFAULT",        QUI$M_QUEUE_CPU_DEFAULT         },
   { "QUEUE_CPU_LIMIT",          QUI$M_QUEUE_CPU_LIMIT           },
   { "QUEUE_DESCRIPTION",        QUI$_QUEUE_DESCRIPTION          },
   { "QUEUE_DIRECTORY",          QUI$_QUEUE_DIRECTORY            },
   { "QUEUE_DISABLED",           QUI$M_QUEUE_DISABLED            },
   { "QUEUE_FILE_BURST",         QUI$M_QUEUE_FILE_BURST          },
   { "QUEUE_FILE_BURST_ONE",     QUI$M_QUEUE_FILE_BURST_ONE      },
   { "QUEUE_FILE_FLAG",          QUI$M_QUEUE_FILE_FLAG           },
   { "QUEUE_FILE_FLAG_ONE",      QUI$M_QUEUE_FILE_FLAG_ONE       },
   { "QUEUE_FILE_PAGINATE",      QUI$M_QUEUE_FILE_PAGINATE       },
   { "QUEUE_FILE_TRAILER",       QUI$M_QUEUE_FILE_TRAILER        },
   { "QUEUE_FILE_TRAILER_ONE",   QUI$M_QUEUE_FILE_TRAILER_ONE    },
   { "QUEUE_FLAGS",              QUI$_QUEUE_FLAGS                },
   { "QUEUE_GENERIC",            QUI$M_QUEUE_GENERIC             },
   { "QUEUE_GENERIC_SELECTION",  QUI$M_QUEUE_GENERIC_SELECTION   },
   { "QUEUE_IDLE",               QUI$M_QUEUE_IDLE                },
   { "QUEUE_JOB_BURST",          QUI$M_QUEUE_JOB_BURST           },
   { "QUEUE_JOB_FLAG",           QUI$M_QUEUE_JOB_FLAG            },
   { "QUEUE_JOB_SIZE_SCHED",     QUI$M_QUEUE_JOB_SIZE_SCHED      },
   { "QUEUE_JOB_TRAILER",        QUI$M_QUEUE_JOB_TRAILER         },
   { "QUEUE_LOWERCASE",          QUI$M_QUEUE_LOWERCASE           },
   { "QUEUE_NAME",               QUI$_QUEUE_NAME                 },
   { "QUEUE_NO_INITIAL_FF",      QUI$M_QUEUE_NO_INITIAL_FF       },
   { "QUEUE_PAUSED",             QUI$M_QUEUE_PAUSED              },
   { "QUEUE_PAUSING",            QUI$M_QUEUE_PAUSING             },
   { "QUEUE_PRINTER",            QUI$M_QUEUE_PRINTER             },
   { "QUEUE_RAD",                QUI$M_QUEUE_RAD                 },
   { "QUEUE_RECORD_BLOCKING",    QUI$M_QUEUE_RECORD_BLOCKING     },
   { "QUEUE_REMOTE",             QUI$M_QUEUE_REMOTE              },
   { "QUEUE_RESETTING",          QUI$M_QUEUE_RESETTING           },
   { "QUEUE_RESUMING",           QUI$M_QUEUE_RESUMING            },
   { "QUEUE_RETAIN_ALL",         QUI$M_QUEUE_RETAIN_ALL          },
   { "QUEUE_RETAIN_ERROR",       QUI$M_QUEUE_RETAIN_ERROR        },
   { "QUEUE_SERVER",             QUI$M_QUEUE_SERVER              },
   { "QUEUE_STALLED",            QUI$M_QUEUE_STALLED             },
   { "QUEUE_STARTING",           QUI$M_QUEUE_STARTING            },
   { "QUEUE_STATUS",             QUI$_QUEUE_STATUS               },
   { "QUEUE_STOPPED",            QUI$M_QUEUE_STOPPED             },
   { "QUEUE_STOPPING",           QUI$M_QUEUE_STOPPING            },
   { "QUEUE_STOP_PENDING",       QUI$M_QUEUE_STOP_PENDING        },
   { "QUEUE_SWAP",               QUI$M_QUEUE_SWAP                },
   { "QUEUE_TERMINAL",           QUI$M_QUEUE_TERMINAL            },
   { "QUEUE_UNAVAILABLE",        QUI$M_QUEUE_UNAVAILABLE         },
   { "QUEUE_WSDEFAULT",          QUI$M_QUEUE_WSDEFAULT           },
   { "QUEUE_WSEXTENT",           QUI$M_QUEUE_WSEXTENT            },
   { "QUEUE_WSQUOTA",            QUI$M_QUEUE_WSQUOTA             },
   { "RAD",                      QUI$_RAD                        },
   { "REQUEUE_QUEUE_NAME",       QUI$_REQUEUE_QUEUE_NAME         },
   { "RESTART_QUEUE_NAME",       QUI$_RESTART_QUEUE_NAME         },
   { "RETAINED_JOB_COUNT",       QUI$_RETAINED_JOB_COUNT         },
   { "SCSNODE_NAME",             QUI$_SCSNODE_NAME               },
   { "SECURITY_INACCESSIBLE",    QUI$M_SECURITY_INACCESSIBLE     },
   { "SUBMISSION_TIME",          QUI$_SUBMISSION_TIME            },
   { "TIMED_RELEASE_JOB_COUNT",  QUI$_TIMED_RELEASE_JOB_COUNT    },
   { "UIC",                      QUI$_UIC                        },
   { "USERNAME",                 QUI$_USERNAME                   },
   { "WSDEFAULT",                QUI$_WSDEFAULT                  },
   { "WSEXTENT",                 QUI$_WSEXTENT                   },
   { "WSQUOTA",                  QUI$_WSQUOTA                    },
} ;

static const struct dvi_items_type qui_flags[] = {
   { "ALL_JOBS",                 QUI$M_SEARCH_ALL_JOBS           },
   { "BATCH",                    QUI$M_SEARCH_BATCH              },
   { "EXECUTING_JOBS",           QUI$M_SEARCH_EXECUTING_JOBS     },
   { "FREEZE_CONTEXT",           QUI$M_SEARCH_FREEZE_CONTEXT     },
   { "GENERIC",                  QUI$M_SEARCH_GENERIC            },
   { "HOLDING_JOBS",             QUI$M_SEARCH_HOLDING_JOBS       },
   { "PENDING_JOBS",             QUI$M_SEARCH_PENDING_JOBS       },
   { "PRINTER",                  QUI$M_SEARCH_PRINTER            },
   { "RETAINED_JOBS",            QUI$M_SEARCH_RETAINED_JOBS      },
   { "SERVER",                   QUI$M_SEARCH_SERVER             },
   { "SYMBIONT",                 QUI$M_SEARCH_SYMBIONT           },
   { "TERMINAL",                 QUI$M_SEARCH_TERMINAL           },
   { "THIS_JOB",                 QUI$M_SEARCH_THIS_JOB           },
   { "TIMED_RELEASE_JOBS",       QUI$M_SEARCH_TIMED_RELEASE_JOBS },
   { "WILDCARD",                 QUI$M_SEARCH_WILDCARD           },
} ;

static const unsigned short legal_qui_flags[] = {
                                                 JOB, /* ALL_JOBS */
                                       ENTRY | QUEUE, /* BATCH */
                                         ENTRY | JOB, /* EXECUTING_JOBS */
   DCHAR | ENTRY | DFILE | FORM | JOB | DMGR | QUEUE, /* FREEZE_CONTEXT */
                                       ENTRY | QUEUE, /* GENERIC */
                                         ENTRY | JOB, /* HOLDING_JOBS */
                                         ENTRY | JOB, /* PENDING_JOBS */
                                       ENTRY | QUEUE, /* PRINTER */
                                         ENTRY | JOB, /* RETAINED_JOBS */
                                       ENTRY | QUEUE, /* SERVER */
                                       ENTRY | QUEUE, /* SYMBIONT */
                                       ENTRY | QUEUE, /* TERMINAL */
                         ENTRY | DFILE | JOB | QUEUE, /* THIS_JOB */
                                         ENTRY | JOB, /* TIMED_RELEASE_JOBS */
                 DCHAR | ENTRY | FORM | DMGR | QUEUE, /* WILDCARD */
} ;


/* Helper function used by vms_f_getqui and vms_f_trnlnm */
static unsigned int parse_flags( streng *parm_str,
                                 const struct dvi_items_type *xlist,
                                 int list_len, int func_index,
                                 const unsigned short *legal_flags ) {
   const char *parm_chr = parm_str->value ;
   unsigned int flags = 0 ;
   int parm_len = parm_str->len ;
   int start_pos = 0 ;

   while ( start_pos < parm_len ) {
      while (isspace(parm_chr[start_pos]) && (start_pos + 1) < parm_len ) {
         start_pos++ ;
      }

      int end_pos = start_pos + 1 ;
      while ( parm_chr[end_pos] != ',' && !isspace( parm_chr[end_pos] ) &&
              (end_pos + 1) < parm_len ) {
         end_pos++ ;
      }

      const struct dvi_items_type *tmp_ptr =
            item_info( parm_chr, (end_pos - start_pos), xlist, list_len );
      if (!tmp_ptr) {
         exiterror( ERR_INCORRECT_CALL , 0 ) ;  /* flag not found */
      }

      /* verify that this flag is allowed for this function (optional) */
      if ( legal_flags ) {
         int flag_info = legal_flags[tmp_ptr - &(xlist[0])] ;
         if ( (flag_info & (1 << (func_index - 1))) == 0 ) {
            exiterror( ERR_INCORRECT_CALL , 0 ) ;  /* flag not allowed here */
         }
      }

      flags |= tmp_ptr->item_code ;

      /* skip trailing whitespace and comma */
      start_pos = end_pos ;
      while (isspace(parm_chr[start_pos]) && (start_pos + 1) < parm_len ) {
         start_pos++ ;
      }

      if ( start_pos < parm_len && (parm_chr[start_pos] != ',') ) {
         exiterror( ERR_INCORRECT_CALL , 0 ) ;  /* expected comma */
      }
      start_pos++ ;
   }

   return flags ;
}


streng *vms_f_getqui( tsd_t *TSD, cparamboxptr parms )
{
   unsigned int rc, flags = 0, *flags_ptr ;
   int func_code, item_code, search_num, item_type_info = 0 ;
   int func_index ;
   int *item_code_ptr, *search_num_ptr ;
   struct dsc$descriptor_s *search_name_ptr ;
   const struct dvi_items_type *tmp_ptr ;
   cparamboxptr parm_ptr ;
   streng *parm_str ;
   char buffer[256] ;
   struct dsc$descriptor_s search_name = { 0,
         DSC$K_DTYPE_T, DSC$K_CLASS_S, NULL } ;
   $DESCRIPTOR( result, buffer ) ;

   /* The function code is always required. */
   parm_str = parms->value ;
   if (!parm_str)
      exiterror( ERR_INCORRECT_CALL , 0 ) ;

/*
 * First, find the function we are to perform, that is the first parameter
 * in the call to f$getqui().
 */
   if ( parm_str->len == 0 ) {
      func_code = QUI$_CANCEL_OPERATION ;
      func_index = 0 ;
   } else if (!(tmp_ptr = item_info( parm_str->value, parm_str->len,
                                     qui_funcs, ARRAY_LEN(qui_funcs)))) {
      exiterror( ERR_INCORRECT_CALL , 0 )  ;
   } else {
      func_code = tmp_ptr->item_code ;
      func_index = ( tmp_ptr - &(qui_funcs[0]) ) ;
   }

/*
 * Depending on the function chosen, check that the parameters are legal
 * for that function, i.e. all parameters that must be specified exist,
 * and no illegal parameters are specified.
 */
   parm_ptr = parms->next ;

   /* second param is item name */
   if ( parm_ptr && parm_ptr->value ) {
      /* QUI$_CANCEL_OPERATION accepts no parameters */
      if ( func_index == 0 ) {
         exiterror( ERR_INCORRECT_CALL , 0 ) ;
      }

      parm_str = parm_ptr->value ;
      tmp_ptr = item_info( parm_str->value, parm_str->len,
                           qui_items, ARRAY_LEN(qui_items) );
      if (!tmp_ptr) {
         exiterror( ERR_INCORRECT_CALL , 0 ) ;  /* item code not found */
      }

      item_code = tmp_ptr->item_code ;
      item_code_ptr = &item_code ;

      /* verify that this item code is allowed for this function */
      item_type_info = legal_items_and_types[tmp_ptr - &(qui_items[0])] ;
      if ( (item_type_info & (1 << (func_index - 1))) == 0 ) {
         exiterror( ERR_INCORRECT_CALL , 0 ) ;  /* item not allowed here */
      }
   } else {
      item_code_ptr = NULL ;
   }

   /* third param is object-id */
   parm_ptr = (parm_ptr) ? parm_ptr->next : NULL ;

   if (parm_ptr && (parm_ptr->value)) {
      /* object-id not allowed */
      if ( func_code == QUI$_DISPLAY_FILE || func_code == QUI$_DISPLAY_JOB ) {
         exiterror( ERR_INCORRECT_CALL , 0 ) ;
      }

      parm_str = parm_ptr->value ;
      if ( myisnumber(TSD, parm_str ) ) {
         search_num = atozpos( TSD, parm_str, "VMS_F_GETQUI", 0 ) ;
         search_num_ptr = &search_num ;
         search_name_ptr = NULL ;
      } else {
         search_name.dsc$w_length = parm_str->len ;
         search_name.dsc$a_pointer = parm_str->value ;
         search_num_ptr = NULL ;
         search_name_ptr = &search_name ;
      }
   } else {
      if ( func_code != QUI$_CANCEL_OPERATION &&
           func_code != QUI$_DISPLAY_ENTRY &&
           func_code != QUI$_DISPLAY_FILE &&
           func_code != QUI$_DISPLAY_JOB ) {
         /* object-id required for the other functions */
         exiterror( ERR_INCORRECT_CALL , 0 ) ;
      }
      search_num_ptr = NULL ;
      search_name_ptr = NULL ;
   }

   /* fourth param is comma-separated list of flag keywords */
   parm_ptr = (parm_ptr) ? parm_ptr->next : NULL ;

   if (parm_ptr && (parm_ptr->value)) {
      /* flags not allowed */
      if ( func_code == QUI$_TRANSLATE_QUEUE ) {
         exiterror( ERR_INCORRECT_CALL , 0 ) ;
      }

      parm_str = parm_ptr->value ;
      flags = parse_flags( parm_str, qui_flags, ARRAY_LEN(qui_flags),
                           func_index, legal_qui_flags ) ;

      flags_ptr = (flags) ? &flags : NULL ;
   } else {
      flags_ptr = NULL ;
   }

   if ( IS_FLAG_TYPE(item_type_info) ) {
      unsigned int result_value = 0 ;

      /* get the relevant bitfield, then use the item_code as a mask */
      rc = lib$getqui( &func_code, qui_flag_item[TYPE_OF_FLAG(item_type_info)],
                       search_num_ptr, search_name_ptr, flags_ptr,
                       &result_value );

      if (rc != SS$_NORMAL)
      {
         vms_error( TSD, rc ) ;
      }

      return boolean( TSD, result_value & item_code ) ;
   } else {
      rc = lib$getqui( &func_code, item_code_ptr, search_num_ptr,
                       search_name_ptr, flags_ptr, NULL, &result,
                       &(result.dsc$w_length) ) ;

      if (rc != SS$_NORMAL)
      {
         vms_error( TSD, rc ) ;

         /* Return the truncated value if we got one, or else an empty string. */
         if ( rc != LIB$_STRTRU )
            return nullstringptr() ;
      }

      return Str_ncreTSD( result.dsc$a_pointer, result.dsc$w_length ) ;
   }
}


static const struct dvi_items_type syi_items[] = {
   { "ACTIVECPU_CNT",         SYI$_ACTIVECPU_CNT         },
   { "ACTIVE_CORE_CNT",       SYI$_ACTIVE_CORE_CNT       },
   { "ACTIVE_CPU_BITMAP",     SYI$_ACTIVE_CPU_BITMAP     },
   { "ACTIVE_CPU_MASK",       SYI$_ACTIVE_CPU_MASK       },
   { "ARCHFLAG",              SYI$_ARCHFLAG              },
   { "ARCH_NAME",             SYI$_ARCH_NAME             },
   { "ARCH_TYPE",             SYI$_ARCH_TYPE             },
   { "AVAILCPU_CNT",          SYI$_AVAILCPU_CNT          },
   { "AVAIL_CPU_BITMAP",      SYI$_ACTIVE_CPU_BITMAP     },
   { "AVAIL_CPU_MASK",        SYI$_ACTIVE_CPU_MASK       },
   { "BOOTTIME",              SYI$_BOOTTIME              },
   { "BOOT_DEVICE",           SYI$_BOOT_DEVICE           },
   { "CHARACTER_EMULATED",    SYI$_CHARACTER_EMULATED    },
   { "CLUSTER_EVOTES",        SYI$_CLUSTER_EVOTES        },
   { "CLUSTER_FSYSID",        SYI$_CLUSTER_FSYSID        },
   { "CLUSTER_FTIME",         SYI$_CLUSTER_FTIME         },
   { "CLUSTER_MEMBER",        SYI$_CLUSTER_MEMBER        },
   { "CLUSTER_NODES",         SYI$_CLUSTER_NODES         },
   { "CLUSTER_QUORUM",        SYI$_CLUSTER_QUORUM        },
   { "CLUSTER_VOTES",         SYI$_CLUSTER_VOTES         },
   { "COMMUNITY_ID",          SYI$_COMMUNITY_ID          },
   { "CONSOLE_VERSION",       SYI$_CONSOLE_VERSION       },
   { "CONTIG_GBLPAGES",       SYI$_CONTIG_GBLPAGES       },
   { "CPU",                   SYI$_CPU                   },
   { "CPUCAP_MASK",           SYI$_CPUCAP_MASK           },
   { "CPUCONF",               SYI$_CPUCONF               },
   { "CPUTYPE",               SYI$_CPUTYPE               },
   { "CPU_AUTOSTART",         SYI$_CPU_AUTOSTART         },
   { "CPU_FAILOVER",          SYI$_CPU_FAILOVER          },
   { "CWLOGICALS",            SYI$_CWLOGICALS            },
   { "DAY_OVERRIDE",          SYI$_DAY_OVERRIDE          },
   { "DAY_SECONDARY",         SYI$_DAY_SECONDARY         },
   { "DECIMAL_EMULATED",      SYI$_DECIMAL_EMULATED      },
   { "DECNET_FULLNAME",       SYI$_DECNET_FULLNAME       },
   { "DECNET_VERSION",        SYI$_DECNET_VERSION        },
   { "DEF_PRIO_MAX",          SYI$_DEF_PRIO_MAX          },
   { "DEF_PRIO_MIN",          SYI$_DEF_PRIO_MIN          },
   { "D_FLOAT_EMULATED",      SYI$_D_FLOAT_EMULATED      },
   { "ERLBUFFERPAGES",        SYI$_ERLBUFFERPAGES        },
   { "ERLBUFFERPAG_S2",       SYI$_ERLBUFFERPAG_S2       },
   { "ERRORLOGBUFFERS",       SYI$_ERRORLOGBUFFERS       },
   { "FREE_GBLPAGES",         SYI$_FREE_GBLPAGES         },
   { "FREE_GBLSECTS",         SYI$_FREE_GBLSECTS         },
 /* TODO: how do we get total FREE_PAGES? */
   { "F_FLOAT_EMULATED",      SYI$_F_FLOAT_EMULATED      },
   { "GALAXY",                SYI$_GALAXY                },
   { "GALAXY_ID",             SYI$_GALAXY_ID             },
   { "GALAXY_MEMBER",         SYI$_GALAXY_MEMBER         },
   { "GALAXY_PLATFORM",       SYI$_GALAXY_PLATFORM       },
   { "GALAXY_SHMEMSIZE",      SYI$_GALAXY_SHMEMSIZE      },
   { "GBLPAGES",              SYI$_GBLPAGES              },
   { "GBLPAGFIL",             SYI$_GBLPAGFIL             },
   { "GBLSECTIONS",           SYI$_GBLSECTIONS           },
   { "GH_RSRVPGCNT",          SYI$_GH_RSRVPGCNT          },
   { "GLX_FORMATION",         SYI$_GLX_FORMATION         },
   { "GLX_INCARNATION",       SYI$_GLX_INCARNATION       },
   { "GLX_INST_TMO",          SYI$_GLX_INST_TMO          },
   { "GLX_MAX_MEMBERS",       SYI$_GLX_MAX_MEMBERS       },
   { "GLX_MBR_INCARNATION",   SYI$_GLX_MBR_INCARNATION   },
   { "GLX_MBR_JOINED",        SYI$_GLX_MBR_JOINED        },
   { "GLX_MBR_MEMBER",        SYI$_GLX_MBR_MEMBER        },
   { "GLX_MBR_NAME",          SYI$_GLX_MBR_NAME          },
   { "GLX_SHM_REG",           SYI$_GLX_SHM_REG           },
   { "GLX_SW_VERSION",        SYI$_GLX_SW_VERSION        },
   { "GLX_TERMINATION",       SYI$_GLX_TERMINATION       },
   { "GRAPHICS_CONSOLE",      SYI$_GRAPHICS_CONSOLE      },
   { "GROWLIM",               SYI$_GROWLIM               },
   { "G_FLOAT_EMULATED",      SYI$_G_FLOAT_EMULATED      },
   { "HP_ACTIVE_CPU_CNT",     SYI$_HP_ACTIVE_CPU_CNT     },
   { "HP_ACTIVE_SP_CNT",      SYI$_HP_ACTIVE_SP_CNT      },
   { "HP_CONFIG_SBB_CNT",     SYI$_HP_CONFIG_SBB_CNT     },
   { "HP_CONFIG_SP_CNT",      SYI$_HP_CONFIG_SP_CNT      },
   { "HP_CORE_CNT",           SYI$_HP_CORE_CNT           },
   { "HP_ID",                 SYI$_HP_ID                 },
   { "HP_NAME",               SYI$_HP_NAME               },
   { "HW_MODEL",              SYI$_HW_MODEL              },
   { "HW_NAME",               SYI$_HW_NAME               },
   { "H_FLOAT_EMULATED",      SYI$_H_FLOAT_EMULATED      },
   { "IJOBLIM",               SYI$_IJOBLIM               },
   { "IMGIOCNT",              SYI$_IMGIOCNT              },
   { "IMGREG_PAGES",          SYI$_IMGREG_PAGES          },
   { "IOTA",                  SYI$_IOTA                  },
   { "IO_PRCPU_BITMAP",       SYI$_IO_PRCPU_BITMAP       },
   { "ITB_ENTRIES",           SYI$_ITB_ENTRIES           },
   { "MAX_CPUS",              SYI$_MAX_CPUS              },
   { "MAX_PFN",               SYI$_MAX_PFN               },
   { "MEMSIZE",               SYI$_MEMSIZE               },
 /* TODO: how do we get MODIFIED_PAGES? */
   { "MULTIPROCESSING",       SYI$_MULTIPROCESSING       },
   { "MULTITHREAD",           SYI$_MULTITHREAD           },
   { "NODENAME",              SYI$_NODENAME              },
   { "NODE_AREA",             SYI$_NODE_AREA             },
   { "NODE_CSID",             SYI$_NODE_CSID             },
   { "NODE_EVOTES",           SYI$_NODE_EVOTES           },
   { "NODE_HWTYPE",           SYI$_NODE_HWTYPE           },
   { "NODE_HWVERS",           SYI$_NODE_HWVERS           },
   { "NODE_NUMBER",           SYI$_NODE_NUMBER           },
   { "NODE_QUORUM",           SYI$_NODE_QUORUM           },
   { "NODE_SWINCARN",         SYI$_NODE_SWINCARN         },
   { "NODE_SWTYPE",           SYI$_NODE_SWTYPE           },
   { "NODE_SWVERS",           SYI$_NODE_SWVERS           },
   { "NODE_SYSTEMID",         SYI$_NODE_SYSTEMID         },
   { "NODE_VOTES",            SYI$_NODE_VOTES            },
 /* TODO: how do we get NPAGED_* and PAGED_* items? */
   { "PAGEFILE_FREE",         SYI$_PAGEFILE_FREE         },
   { "PAGEFILE_PAGE",         SYI$_PAGEFILE_PAGE         },
   { "PAGE_SIZE",             SYI$_PAGE_SIZE             },
   { "PALCODE_VERSION",       SYI$_PALCODE_VERSION       },
   { "PARTITION_ID",          SYI$_PARTITION_ID          },
   { "PFN_MEMORY_MAP",        SYI$_PFN_MEMORY_MAP        },
   { "PFN_MEMORY_MAP_64",     SYI$_PFN_MEMORY_MAP_64     },
   { "PHYSICALPAGES",         SYI$_PHYSICALPAGES         },
   { "PMD_COUNT",             SYI$_PMD_COUNT             },
   { "POTENTIALCPU_CNT",      SYI$_POTENTIALCPU_CNT      },
   { "POTENTIAL_CPU_BITMAP",  SYI$_POTENTIAL_CPU_BITMAP  },
   { "POTENTIAL_CPU_MASK",    SYI$_POTENTIAL_CPU_MASK    },
   { "POWEREDCPU_CNT",        SYI$_POWEREDCPU_CNT        },
   { "POWERED_CPU_BITMAP",    SYI$_POWERED_CPU_BITMAP    },
   { "POWERED_CPU_MASK",      SYI$_POWERED_CPU_MASK      },
   { "PRESENTCPU_CNT",        SYI$_PRESENTCPU_CNT        },
   { "PRESENT_CPU_BITMAP",    SYI$_PRESENT_CPU_BITMAP    },
   { "PRESENT_CPU_MASK",      SYI$_PRESENT_CPU_MASK      },
   { "PRIMARY_CPUID",         SYI$_PRIMARY_CPUID         },
   { "PROCESS_SPACE_LIMIT",   SYI$_PROCESS_SPACE_LIMIT   },
   { "PSXFIFO_PRIO_MAX",      SYI$_PSXFIFO_PRIO_MAX      },
   { "PSXFIFO_PRIO_MIN",      SYI$_PSXFIFO_PRIO_MIN      },
   { "PSXRR_PRIO_MAX",        SYI$_PSXRR_PRIO_MAX        },
   { "PSXRR_PRIO_MIN",        SYI$_PSXRR_PRIO_MIN        },
   { "PTES_PER_PAGE",         SYI$_PTES_PER_PAGE         },
   { "PT_BASE",               SYI$_PT_BASE               },
   { "QUANTUM",               SYI$_QUANTUM               },
   { "RAD_CPUS",              SYI$_RAD_CPUS              },
   { "RAD_MAX_RADS",          SYI$_RAD_MAX_RADS          },
   { "RAD_MEMSIZE",           SYI$_RAD_MEMSIZE           },
   { "RAD_SHMEMSIZE",         SYI$_RAD_SHMEMSIZE         },
   { "RAD_SUPPORT",           SYI$_RAD_SUPPORT           },
   { "REAL_CPUTYPE",          SYI$_REAL_CPUTYPE          },
   { "SCSNODE",               SYI$_SCSNODE               },
   { "SCS_EXISTS",            SYI$_SCS_EXISTS            },
   { "SERIAL_NUMBER",         SYI$_SERIAL_NUMBER         },
   { "SHARED_VA_PTES",        SYI$_SHARED_VA_PTES        },
   { "SID",                   SYI$_SID                   },
   { "SWAPFILE_FREE",         SYI$_SWAPFILE_FREE         },
   { "SWAPFILE_PAGE",         SYI$_SWAPFILE_PAGE         },
   { "SYSTEM_RIGHTS",         SYI$_SYSTEM_RIGHTS         },
   { "SYSTEM_UUID",           SYI$_SYSTEM_UUID           },
   { "SYSTYPE",               SYI$_SYSTYPE               },
 /* TODO: how do we get TOTAL_PAGES? */
   { "USED_GBLPAGCNT",        SYI$_USED_GBLPAGCNT        },
   { "USED_GBLPAGMAX",        SYI$_USED_GBLPAGMAX        },
 /* TODO: how do we get USED_PAGES? */
   { "VERSION",               SYI$_VERSION               },
   { "VIRTUAL_MACHINE",       SYI$_VIRTUAL_MACHINE       },
   { "XCPU",                  SYI$_XCPU                  },
   { "XSID",                  SYI$_XSID                  },
} ;

streng *vms_f_getsyi( tsd_t *TSD, cparamboxptr parms )
{
   char buffer[256] ;
   int rc, itemcode ;
   const struct dvi_items_type *ptr ;
   struct dsc$descriptor_s result = {
       sizeof(buffer)-1, DSC$K_DTYPE_T, DSC$K_CLASS_S, buffer } ;

   checkparam( parms, 1, 2, "VMS_F_GETSYI" ) ;

   ptr = item_info( parms->value->value, parms->value->len,
                    syi_items, ARRAY_LEN( syi_items ) ) ;
   if (!ptr)
      exiterror( ERR_INCORRECT_CALL , 0 ) ;

   itemcode = ptr->item_code ;

   if (parms->next && parms->next->value)
   {
      struct dsc$descriptor_s node_name = {
         parms->next->value->len, DSC$K_DTYPE_T, DSC$K_CLASS_S,
         parms->next->value->value } ;

      rc = lib$getsyi( &itemcode, NULL, &result, &(result.dsc$w_length),
                       NULL, &node_name );
   } else {
      rc = lib$getsyi( &itemcode, NULL, &result, &(result.dsc$w_length) );
   }

   if (rc != SS$_NORMAL)
   {
      vms_error( TSD, rc ) ;
      return nullstringptr() ;
   }

   return Str_ncreTSD( result.dsc$a_pointer, result.dsc$w_length ) ;
}


streng *vms_f_identifier( tsd_t *TSD, cparamboxptr parms )
{
   streng *type, *result ;

   checkparam( parms, 2, 2, "VMS_F_IDENTIFIER" ) ;

   type = parms->next->value ;

   if (type->len != 14)
      exiterror( ERR_INCORRECT_CALL , 0 ) ;

   if (!strncasecmp(type->value, "NAME_TO_NUMBER", 14))
      result = int_to_streng( TSD, name_to_num( TSD, parms->value )) ;
   else if (!strncasecmp(type->value, "NUMBER_TO_NAME", 14))
   {
      result = num_to_name( TSD, atozpos( TSD, parms->value, "VMS_F_IDENTIFIER", 1 )) ;
      if (!result)
         result = nullstringptr() ;
   }
   else
      exiterror( ERR_INCORRECT_CALL , 0 ) ;

   return result ;
}



streng *vms_f_message( tsd_t *TSD, cparamboxptr parms )
{
   char buffer[256] ;
   $DESCRIPTOR( name, buffer ) ;
   unsigned int rc, errmsg ;

   checkparam( parms, 1, 1, "VMS_F_MESSAGE" ) ;
   errmsg = atopos( TSD, parms->value, "VMS_F_MESSAGE", 1 ) ;

   rc = sys$getmsg( errmsg, &(name.dsc$w_length), &name, 0, NULL ) ;

   if ((rc != SS$_NORMAL) && (rc != SS$_MSGNOTFND))
      vms_error( TSD, rc ) ;

   return Str_ncreTSD( name.dsc$a_pointer, name.dsc$w_length ) ;
}


streng *vms_f_mode( tsd_t *TSD, cparamboxptr parms )
{
   char buffer[256] ;
   $DESCRIPTOR( descr, buffer ) ;
   int item = JPI$_MODE, length, rc ;

   rc = lib$getjpi( &item, NULL, NULL, NULL, &descr, &length ) ;

   if (rc != SS$_NORMAL)
      vms_error( TSD, rc ) ;

   return Str_ncreTSD( descr.dsc$a_pointer, descr.dsc$w_length ) ;
}


streng *vms_f_pid( tsd_t *TSD, cparamboxptr parms )
{
   unsigned short length ;
   int rc, buffer ;
   unsigned int pid;
   struct _ile3 items[2] ;
   const streng *Pid ;
   vmf_tsd_t *vt;
   streng *val = NULL ;

   vt = TSD->vmf_tsd;
   checkparam( parms, 1, 1, "VMS_F_PID" ) ;

   items[0].ile3$w_length = 4 ;
   items[0].ile3$w_code = JPI$_PID ;
   items[0].ile3$ps_bufaddr = &buffer ;
   items[0].ile3$ps_retlen_addr = &length ;
   memset( &items[1], 0, sizeof(struct _ile3) ) ;

   Pid = getvalue( TSD, parms->value, -1 ) ;

   if (Pid->len)
   {
      pid = strtoul( Pid->value, NULL, 16 ) ;
   }
   else
      pid = (unsigned int)(-1) ;

   do {
      rc = sys$getjpiw( EFN$C_ENF, &pid, NULL, &items, NULL, NULL, 0 ) ;
      }
   while (rc == SS$_NOPRIV) ;

   if ((rc != SS$_NORMAL) && (rc != SS$_NOMOREPROC))
      vms_error( TSD, rc ) ;

   snprintf( (val=Str_makeTSD(10))->value, 10, "%08x", pid ) ;
   val->len = 8 ;
   setvalue( TSD, parms->value, val, -1 ) ;

   if (rc == SS$_NOMOREPROC)
      return nullstringptr() ;

   assert( length==4 ) ;
   snprintf( (val=Str_makeTSD(10))->value, 10, "%08x", buffer ) ;
   val->len = 8 ;

   return val ;
}


#define MAX_PRIVS (sizeof(all_privs)/sizeof(char*))

static streng *map_privs( const tsd_t *TSD, const GENERIC_64 privs )
{
   int i ;
   char *ptr, buffer[512] ;

   *(ptr=buffer) = 0x00 ;
   for (i=0; i<MAX_PRIVS; i++)
      if ((privs.gen64$q_quadword >> i) & 0x01)
      {
         strcat( ptr, all_privs[i] ) ;
         ptr += strlen(all_privs[i]) ;
         strcat( ptr++, "," ) ;
      }

   if (ptr>buffer)
      *(--ptr) = 0x00 ;

   return Str_ncreTSD( buffer, (ptr-buffer) ) ;
}

static int extract_privs( GENERIC_64 *vector, const streng *privs )
{
   int max_priv, negate, i ;
   const char *ptr, *eptr, *tptr, *lptr ;

   max_priv = MAX_PRIVS ;

   eptr = Str_end( privs ) ;
   for (ptr=privs->value; ptr<eptr; ptr=(++lptr) )
   {
      for (; isspace(*ptr) && ptr<eptr; ptr++ ) ;
      for (lptr=ptr; (lptr<eptr) && (*lptr!=','); lptr++) ;
      for (tptr=lptr; isspace(*(tptr-1)) && tptr>=ptr; tptr-- ) ;
      if (tptr-ptr<3)
         return 1 ;

      negate = ((*ptr=='N') && (*(ptr+1)=='O')) * 2 ;
      for (i=0; i<max_priv; i++)
         if ((!strncasecmp(ptr+negate,all_privs[i],tptr-ptr-negate)) &&
                                (all_privs[i][tptr-ptr-negate] == 0x00))
         {
            if (negate)
               vector[1].gen64$q_quadword |= (1 << i) ;
            else
               vector[0].gen64$q_quadword |= (1 << i) ;
            break ;
         }

      if (i==max_priv)
         return 1 ;
   }
   return 0 ;
}


streng *vms_f_privilege( tsd_t *TSD, cparamboxptr parms )
{
   GENERIC_64 privbits[2], privs ;
   int rc ;
   int item = JPI$_PROCPRIV ;

   checkparam( parms, 1, 1, "VMS_F_PRIVILEGE" ) ;
   extract_privs( privbits, parms->value ) ;

   rc = lib$getjpi( &item, NULL, NULL, &privs ) ;
   if (rc != SS$_NORMAL)
      vms_error( TSD, rc ) ;

   return boolean( TSD,
            !((privbits[0].gen64$q_quadword & ~privs.gen64$q_quadword) |
             ( privbits[1].gen64$q_quadword & privs.gen64$q_quadword )) ) ;
}



streng *vms_f_process( tsd_t *TSD, cparamboxptr parms )
{
   int rc ;
   char buffer[64] ;
   $DESCRIPTOR( descr, buffer ) ;
   int item = JPI$_PRCNAM;

   checkparam( parms, 0, 0, "VMS_F_PROCESS" ) ;
   rc = lib$getjpi( &item, NULL, NULL, NULL, &descr,
                    &(descr.dsc$w_length) ) ;

   if ( rc != SS$_NORMAL)
      vms_error( TSD, rc ) ;

   return Str_ncreTSD( descr.dsc$a_pointer, descr.dsc$w_length ) ;
}


streng *vms_f_string( tsd_t *TSD, cparamboxptr parms )
{
   checkparam( parms, 1, 1, "VMS_F_STRING" ) ;

/*   return str_norm( TSD, parms->value ) ;   / * if it existed */
   return Str_dupTSD(parms->value) ;
}



#define DAT_TIME_LEN 23

streng *vms_f_time( tsd_t *TSD, cparamboxptr parms )
{
   int rc ;
   char buffer[DAT_TIME_LEN] ;
   $DESCRIPTOR( descr, buffer ) ;

   checkparam( parms, 0, 0, "VMS_F_TIME" ) ;

   rc = lib$date_time( &descr ) ;
   if (rc != SS$_NORMAL)
      vms_error( TSD, rc ) ;

   return Str_ncreTSD( descr.dsc$a_pointer, descr.dsc$w_length ) ;
}


streng *vms_f_setprv( tsd_t *TSD, cparamboxptr parms )
{
   GENERIC_64 privbits[2], old ;
   int rc ;

   checkparam( parms, 1, 1, "VMS_F_SETPRV" ) ;

   extract_privs( privbits, parms->value ) ;
   rc = sys$setprv( 0, &privbits[0], 0, &old ) ;
   if (rc != SS$_NORMAL)
      vms_error( TSD, rc ) ;

   rc = sys$setprv( 1, &privbits[1], 0, NULL ) ;
   if (rc != SS$_NORMAL)
      vms_error( TSD, rc ) ;

   return map_privs( TSD, old ) ;
}


streng *vms_f_user( tsd_t *TSD, cparamboxptr parms )
{
   int rc ;
   int itemcode = JPI$_UIC ;
   char buffer[14] ;
   $DESCRIPTOR( result, buffer ) ;

   checkparam( parms, 0, 0, "VMS_F_USER" ) ;

   rc = lib$getjpi( &itemcode, NULL, NULL, NULL, &result,
                    &(result.dsc$w_length) ) ;

   if (rc != SS$_NORMAL)
   {
      vms_error( TSD, rc ) ;
      return nullstringptr() ;
   }

   return Str_ncreTSD( result.dsc$a_pointer, result.dsc$w_length ) ;
}


streng *vms_f_locate( tsd_t *TSD, cparamboxptr parms )
{
   int res ;

   checkparam( parms, 2, 2, "VMS_F_LOCATE" ) ;
   res = bmstrstr( parms->next->value, 0, parms->value, 0 ) ;
   if (res==(-1))
      res = parms->next->value->len + 1 ;

   return int_to_streng( TSD, res ) ;
}


streng *vms_f_length( tsd_t *TSD, cparamboxptr parms )
{
   checkparam( parms, 1, 1, "VMS_F_LENGTH" ) ;
   return int_to_streng( TSD, parms->value->len ) ;
}


streng *vms_f_integer( tsd_t *TSD, cparamboxptr parms )
{
   checkparam( parms, 1, 1, "VMS_F_INTEGER" ) ;
   return int_to_streng( TSD, myatol( TSD, parms->value )) ;
}


static const struct dvi_items_type trnlnm_cases[] = {
   { "CASE_BLIND",      LNM$M_CASE_BLIND  },
   { "CASE_SENSITIVE",  0                 },
   { "INTERLOCKED",     LNM$M_INTERLOCKED },
   { "NONINTERLOCKED",  0                 },
} ;

static const struct dvi_items_type trnlnm_modes[] = {
   { "EXECUTIVE",    PSL$C_EXEC      },
   { "KERNEL",       PSL$C_KERNEL    },
   { "SUPERVISOR",   PSL$C_SUPER     },
   { "USER",         PSL$C_USER      },
} ;

enum trnlnm_type {
   TYP_INT,
   TYP_STR,
   TYP_FLAG,
   TYP_MODE
} ;

static const struct dvi_items_type trnlnm_items[] = {
   { "ACCESS_MODE", LNM$_ACMODE     },
   { "CONCEALED",   LNM$M_CONCEALED },
   { "CONFINE",     LNM$M_CONFINE   },
   { "CRELOG",      LNM$M_CRELOG    },
   { "LENGTH",      LNM$_LENGTH     },
   { "MAX_INDEX",   LNM$_MAX_INDEX  },
   { "NO_ALIAS",    LNM$M_NO_ALIAS  },
   { "TABLE",       LNM$M_TABLE     },
   { "TABLE_NAME",  LNM$_TABLE      },
   { "TERMINAL",    LNM$M_TERMINAL  },
   { "VALUE",       LNM$_STRING     },
} ;

/* Note: I had to move the return types to a separate array after
 * refactoring to remove all the return types from the other arrays.
 */
static const enum trnlnm_type trnlnm_item_types[] = {
   TYP_MODE,   /* ACCESS_MODE */
   TYP_FLAG,   /* CONCEALED */
   TYP_FLAG,   /* CONFINE */
   TYP_FLAG,   /* CRELOG */
   TYP_INT,    /* LENGTH */
   TYP_INT,    /* MAX_INDEX */
   TYP_FLAG,   /* NO_ALIAS */
   TYP_FLAG,   /* TABLE */
   TYP_STR,    /* TABLE_NAME */
   TYP_FLAG,   /* TERMINAL */
   TYP_STR,    /* VALUE */
} ;

streng *vms_f_trnlnm( tsd_t *TSD, cparamboxptr parms )
{
   char buffer[NAML$C_MAXRSS] ;
   $DESCRIPTOR( lognam, "" ) ;
   $DESCRIPTOR( tabnam, "LNM$DCL_LOGICAL" ) ;
   unsigned short length ;
   unsigned char mode ;
   enum trnlnm_type item_type = TYP_STR ;
   unsigned int attr=0, item = LNM$_STRING, rc, cnt=0 ;
   int index ;
   const struct dvi_items_type *item_ptr = NULL ;
   cparamboxptr ptr ;
   struct _ile3 items[3] ;
   streng *result ;

   checkparam( parms, 1, 6, "VMS_F_TRNLNM" ) ;

   /* first param is logical name (the only required param) */
   ptr = parms ;
   lognam.dsc$a_pointer = ptr->value->value ;
   lognam.dsc$w_length = ptr->value->len ;

   /* second param is table (default is LNM$DCL_LOGICAL) */
   if (ptr) ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      tabnam.dsc$a_pointer = ptr->value->value ;
      tabnam.dsc$w_length = ptr->value->len ;
   }

   /* third param is index (starting at 0) */
   if (ptr) ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      index = atozpos( TSD, ptr->value, "VMS_F_TRNLNM", 0 ) ;
      if (index<0 || index>127)
         exiterror( ERR_INCORRECT_CALL , 0 ) ;

      items[cnt].ile3$w_length = sizeof(int) ;
      items[cnt].ile3$w_code = LNM$_INDEX ;
      items[cnt].ile3$ps_bufaddr = &index ;
      items[cnt++].ile3$ps_retlen_addr = NULL ;
   }

   /* fourth param is mode (default is USER) */
   if (ptr) ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      item_ptr = item_info( ptr->value->value, ptr->value->len,
                            trnlnm_modes, ARRAY_LEN(trnlnm_modes)) ;
      if (!item_ptr)
         exiterror( ERR_INCORRECT_CALL , 0 ) ;

      mode = item_ptr->item_code ;
   }
   else
      mode = PSL$C_USER ;

   /* fifth param is case sensitivity and whether to use cluster interlock */
   if (ptr) ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      attr = parse_flags( ptr->value, trnlnm_cases, ARRAY_LEN(trnlnm_cases),
                          0, NULL ) ;
   }

   /* sixth param is type of info to return */
   if (ptr) ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      /* Note: item_ptr is used later */
      item_ptr = item_info( ptr->value->value, ptr->value->len,
                            trnlnm_items, ARRAY_LEN(trnlnm_items)) ;
      if (!item_ptr)
         exiterror( ERR_INCORRECT_CALL , 0 ) ;

      item_type = trnlnm_item_types[item_ptr - &(trnlnm_items[0])] ;

      if (item_type == TYP_FLAG)
         item = LNM$_ATTRIBUTES ;
      else
         item = item_ptr->item_code ;
   }

   items[cnt].ile3$w_length = NAML$C_MAXRSS ;
   items[cnt].ile3$w_code = item ;
   items[cnt].ile3$ps_bufaddr = buffer ;
   items[cnt++].ile3$ps_retlen_addr = &length ;

   memset(&items[cnt], 0, sizeof(struct _ile3));

   rc = sys$trnlnm( &attr, &tabnam, &lognam, &mode, items ) ;

   if (rc == SS$_NOLOGNAM)
      return nullstringptr() ;

   if (rc != SS$_NORMAL)
   {
      vms_error( TSD, rc ) ;
      return nullstringptr() ;
   }

   switch (item_type) {
   case TYP_INT:
      result = Str_makeTSD( 12 ) ;
      snprintf( result->value, 12, "%d", *((int*)buffer) ) ;
      result->len = strlen( result->value ) ;
      assert( result->len < result->max ) ;
      return result ;

   case TYP_STR:
      return Str_ncreTSD( buffer, length ) ;

   case TYP_FLAG:
      /* Note: item_ptr must be non-NULL if item_type is other than TYP_STR */
      return boolean( TSD, *((int*)buffer) & item_ptr->item_code) ;

   case TYP_MODE:
      for (cnt=0; cnt<sizeof(trnlnm_modes)/sizeof(struct dvi_items_type);cnt++)
         if (trnlnm_modes[cnt].item_code == (*((unsigned char*)buffer)))
            return Str_creTSD( trnlnm_modes[cnt].name ) ;
      exiterror( ERR_SYSTEM_FAILURE , 0 ) ;
      break ;

   default:
      exiterror( ERR_SYSTEM_FAILURE , 0 ) ;
   }
}



streng *vms_f_logical( tsd_t *TSD, cparamboxptr parms )
{
   checkparam( parms, 1, 1, "VMS_F_LOGICAL" ) ;
   return vms_f_trnlnm( TSD, parms ) ;
}



static const struct dvi_items_type parse_types[] = {
   { "NO_CONCEAL",  NAM$M_NOCONCEAL },
   { "SYNTAX_ONLY", NAM$M_SYNCHK    },
} ;


#define PARSE_EVERYTHING 0x00
#define PARSE_DEVICE     0x01
#define PARSE_DIRECTORY  0x02
#define PARSE_NAME       0x04
#define PARSE_NODE       0x08
#define PARSE_TYPE       0x10
#define PARSE_VERSION    0x20

static const struct dvi_items_type parse_fields[] = {
   { "DEVICE",    PARSE_DEVICE    },
   { "DIRECTORY", PARSE_DIRECTORY },
   { "NAME",      PARSE_NAME      },
   { "NODE",      PARSE_NODE      },
   { "TYPE",      PARSE_TYPE      },
   { "VERSION",   PARSE_VERSION   },
} ;


streng *vms_f_parse( tsd_t *TSD, cparamboxptr parms )
{
   char expb[NAML$C_MAXRSS] ;
   unsigned int clen, rc, fields ;
   char *cptr ;
   const struct dvi_items_type *item ;
   cparamboxptr ptr ;
   struct FAB fab          = cc$rms_fab ;
   struct NAML naml        = cc$rms_naml ;
   struct NAML relnaml     = cc$rms_naml ;

   checkparam( parms, 1, 5, "VMS_F_PARSE" ) ;
   ptr = parms ;

   fab.fab$w_ifi = 0 ;        /* internal file index */
   fab.fab$v_ofp = 0 ;        /* output file parse */
   fab.fab$v_nam = 1 ;        /* use name block fields for open */
   fab.fab$l_naml = &naml ;

   fab.fab$l_fna = (char *)(-1) ;   /* use name block */
   naml.naml$l_long_filename = ptr->value->value ;
   naml.naml$l_long_filename_size = ptr->value->len ;

   naml.naml$l_long_expand = expb ;
   naml.naml$l_long_expand_alloc = sizeof(expb) ;

   ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      fab.fab$l_dna = (char *)(-1) ;   /* use name block */
      naml.naml$l_long_defname      = ptr->value->value ;
      naml.naml$l_long_defname_size = ptr->value->len ;
   }

   if (ptr) ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      relnaml.naml$l_long_result = ptr->value->value ;
      relnaml.naml$l_long_result_size = ptr->value->len ;
      relnaml.naml$l_long_result_alloc = ptr->value->len ;

      naml.naml$l_rlf_naml = &relnaml ;
   }

   if (ptr) ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      item = item_info( ptr->value->value, ptr->value->len,
                        parse_fields, ARRAY_LEN(parse_fields)) ;
      if (!item)
         exiterror( ERR_INCORRECT_CALL , 0 ) ;
      fields = item->item_code ;
   }
   else
      fields = PARSE_EVERYTHING ;

   if (ptr) ptr=ptr->next ;
   if (ptr && ptr->value)
   {
      item = item_info( ptr->value->value, ptr->value->len,
                        parse_types, ARRAY_LEN(parse_types)) ;
      if (!item)
         exiterror( ERR_INCORRECT_CALL , 0 ) ;

      naml.naml$b_nop |= item->item_code ;
   }

   rc = sys$parse( &fab ) ;

   if ((rc==RMS$_SYN) || (rc==RMS$_DEV) || (rc==RMS$_DNF) || (rc==RMS$_DIR) ||
       (rc==RMS$_NOD))
      return nullstringptr() ;

   if (rc != RMS$_NORMAL)
   {
      vms_error( TSD, rc ) ;
      return nullstringptr() ;
   }

   switch( fields )
   {
      case PARSE_EVERYTHING:
         cptr = naml.naml$l_long_expand ;
         clen = naml.naml$l_long_expand_size ;
         break ;

      case PARSE_DEVICE:
         cptr = naml.naml$l_long_dev ;
         clen = naml.naml$l_long_dev_size ;
         break ;

      case PARSE_DIRECTORY:
         cptr = naml.naml$l_long_dir ;
         clen = naml.naml$l_long_dir_size ;
         break ;

      case PARSE_NAME:
         cptr = naml.naml$l_long_name ;
         clen = naml.naml$l_long_name_size ;
         break ;

      case PARSE_NODE:
         cptr = naml.naml$l_long_node ;
         clen = naml.naml$l_long_node_size ;
         break ;

      case PARSE_TYPE:
         cptr = naml.naml$l_long_type ;
         clen = naml.naml$l_long_type_size ;
         break ;

      case PARSE_VERSION:
         cptr = naml.naml$l_long_ver ;
         clen = naml.naml$l_long_ver_size ;
         break ;

      default:
         exiterror( ERR_INTERPRETER_FAILURE, 1, __FILE__, __LINE__, "" )  ;
   }

   return Str_ncreTSD( cptr, clen ) ;
}


streng *vms_f_search( tsd_t *TSD, cparamboxptr parms )
{
   streng *name ;
   unsigned int context, rc, search ;
   struct fabptr *fptr ;
   vmf_tsd_t *vt;
   struct NAML *result_naml ;

   vt = TSD->vmf_tsd;
   checkparam( parms, 1, 2, "VMS_F_SEARCH" ) ;

   name = parms->value ;
   context = (parms->next && parms->next->value) ?
                      atopos(TSD, parms->next->value, "VMS_F_SEARCH", 2 ) : 0 ;

   search = (context/16) ;
   for (fptr=vt->fabptrs[search]; fptr && fptr->num!=context; fptr=fptr->next) ;
   if (!fptr)
   {
      fptr = MallocTSD( sizeof(struct fabptr)) ;
      fptr->num = context ;
      fptr->next = vt->fabptrs[search] ;
      vt->fabptrs[search] = fptr ;
      fptr->box = MallocTSD( sizeof(struct FAB)) ;
      memcpy( fptr->box, &cc$rms_fab, sizeof(struct FAB)) ;
      result_naml = MallocTSD( sizeof(struct NAML)) ;
      fptr->box->fab$l_naml = result_naml ;
      memcpy( result_naml, &cc$rms_naml, sizeof(struct NAML)) ;
      result_naml->naml$l_long_expand        = MallocTSD( NAML$C_MAXRSS ) ;
      result_naml->naml$l_long_expand_alloc  = NAML$C_MAXRSS ;
      result_naml->naml$l_long_result        = MallocTSD( NAML$C_MAXRSS ) ;
      result_naml->naml$l_long_result_alloc  = NAML$C_MAXRSS ;
      result_naml->naml$l_long_result_size   = 0 ;
      fptr->box->fab$l_fna = (char *)(-1) ;   /* use name block */
      fptr->box->fab$b_fns = 0 ;
   } else {
      result_naml = fptr->box->fab$l_naml ;  /* for easier access */
   }

   if (context==0 && ((name->len != result_naml->naml$l_long_filename_size) ||
         strncasecmp(name->value, result_naml->naml$l_long_filename, name->len ))) {
      result_naml->naml$l_long_result_size = 0 ;
   }

   if (result_naml->naml$l_long_result_size == 0)
   {
      fptr->name = Str_dupTSD( name ) ;
      result_naml->naml$l_long_filename       = fptr->name->value ;
      result_naml->naml$l_long_filename_size  = fptr->name->len ;
      fptr->box->fab$w_ifi = 0 ;

      rc = sys$parse( fptr->box ) ;

      if (rc != RMS$_NORMAL)
      {
         vms_error( TSD, rc ) ;
         return nullstringptr() ;
      }
   }

   rc = sys$search( fptr->box ) ;
   if (rc == RMS$_NMF)
      return nullstringptr() ;

   if (rc != RMS$_NORMAL)
   {
      vms_error( TSD, rc ) ;
      return nullstringptr() ;
   }

   return Str_ncreTSD( result_naml->naml$l_long_result,
                       result_naml->naml$l_long_result_size ) ;
}


streng *vms_f_type( tsd_t *TSD, cparamboxptr parms )
{
   checkparam( parms, 1, 1, "VMS_F_TYPE" ) ;
   return Str_creTSD(myisinteger( parms->value ) ? "INTEGER" : "STRING" ) ;
}


static streng *boolean( const tsd_t *TSD, const int param )
{
   return Str_creTSD( param ? "TRUE" : "FALSE" ) ;
}


static streng *date_time( const tsd_t *TSD, unsigned __int64 time )
{
   unsigned int rc ;
   int date_length ;

   /* TODO: we should pass a user context as second param for reentrancy. */
   rc = lib$get_maximum_date_length( &date_length ) ;

   if (rc == SS$_NORMAL) {
      char date_str[ date_length ] ;
      struct dsc$descriptor_s date_str_d = { date_length,
                                             DSC$K_DTYPE_T,
                                             DSC$K_CLASS_S,
                                             date_str };

      /* TODO: we should pass a user context as third param for reentrancy. */
      rc = lib$format_date_time( &date_str_d,
                                 &time, 0,
                                 &date_str_d.dsc$w_length) ;

      if (rc == SS$_NORMAL) {
         return Str_ncreTSD( date_str_d.dsc$a_pointer, date_str_d.dsc$w_length ) ;
      }
   }

   /* If we haven't returned, there was an error. */
   vms_error( TSD, rc ) ;
   return nullstringptr() ;
}


/* Indices into the array below. */
enum file_attr_item_index {
   FIL_AI = 0,
   FIL_ALQ,
   FIL_BDT,
   FIL_BI,
   FIL_BKS,
   FIL_BLS,
   FIL_CBT,
   FIL_CDT,
   FIL_CTG,
   FIL_DEQ,
   FIL_DID,
   FIL_DIRECTORY,
   FIL_DVI,
   FIL_EDT,
   FIL_EOF,
   FIL_ERASE,
   FIL_FFB,
   FIL_FID,
   /* TODO: add FIL_FLENGTH_HINT support */
   FIL_FSZ,
   FIL_GBC,
   FIL_GBC32,
   /* TODO: add FIL_GBCFLAGS support */
   FIL_GRP,
   FIL_JOURNAL_FILE,
   FIL_KNOWN,
   FIL_LOCKED,
   FIL_LRL,
   FIL_MBM,
   FIL_MOVE,
   FIL_MRN,
   FIL_MRS,
   FIL_NOA,
   FIL_NOBACKUP,
   FIL_NOK,
   FIL_ORG,
   FIL_PRESHELVED,
   FIL_PRO,
   FIL_PVN,
   FIL_RAT,
   FIL_RCK,
   FIL_RDT,
   FIL_RFM,
   FIL_RU,
   FIL_RVN,
   FIL_SHELVABLE,
   FIL_SHELVED,
   /* TODO: is FIL_STORED_SEMANTICS useful? */
   FIL_UIC,
   FIL_VERLIMIT,
   FIL_WCK,
} ;

/* type of extra XAB needed, and item code, for XABITM attributes */
enum file_attr_type {
   FIL_ATTR_FAB,                 /* only FAB needed for result */
   FIL_ATTR_NAML,                /* NAML needed (we get this anyway) */
   FIL_ATTR_XABDAT,              /* date and time XAB needed */
   FIL_ATTR_XABFHC,              /* file header characteristics */
   FIL_ATTR_XABPRO,              /* protection XAB needed */
   FIL_ATTR_XABSUM,              /* summary XAB needed */
/*   FIL_ATTR_ITEM_FLENGTH_HINT = XAB$_FILE_LENGTH_HINT, */ /* 65 */
   FIL_ATTR_ITEM_NOBACKUP     = XAB$_UCHAR_NOBACKUP,     /* 130 */
   FIL_ATTR_ITEM_LOCKED       = XAB$_UCHAR_LOCKED,       /* 135 */
   FIL_ATTR_ITEM_ERASE        = XAB$_UCHAR_ERASE,        /* 143 */
   FIL_ATTR_ITEM_NOMOVE       = XAB$_UCHAR_NOMOVE,       /* 144 */
   FIL_ATTR_ITEM_SHELVED      = XAB$_UCHAR_SHELVED,      /* 145 */
   FIL_ATTR_ITEM_NOSHELVABLE  = XAB$_UCHAR_NOSHELVABLE,  /* 146 */
   FIL_ATTR_ITEM_PRESHELVED   = XAB$_UCHAR_PRESHELVED,   /* 147 */
/*   FIL_ATTR_ITEM_STORED_SEM   = XAB$_STORED_SEMANTICS, */ /* 192 */
   FIL_ATTR_ITEM_GBC32        = XAB$_GBC32,              /* 260 */
/*   FIL_ATTR_ITEM_GBCFLAGS     = XAB$_GBCFLAGS,   */    /* 262 */
} ;

static const struct dvi_items_type file_attribs[] = {
   { "AI",                 FIL_ATTR_FAB               },
   { "ALQ",                FIL_ATTR_FAB               },
   { "BDT",                FIL_ATTR_XABDAT            },
   { "BI",                 FIL_ATTR_FAB               },
   { "BKS",                FIL_ATTR_FAB               },
   { "BLS",                FIL_ATTR_FAB               },
   { "CBT",                FIL_ATTR_FAB               },
   { "CDT",                FIL_ATTR_XABDAT            },
   { "CTG",                FIL_ATTR_FAB               },
   { "DEQ",                FIL_ATTR_FAB               },
   { "DID",                FIL_ATTR_NAML              },
   { "DIRECTORY",          FIL_ATTR_NAML              },
   { "DVI",                FIL_ATTR_NAML              },
   { "EDT",                FIL_ATTR_XABDAT            },
   { "EOF",                FIL_ATTR_XABFHC            },
   { "ERASE",              FIL_ATTR_ITEM_ERASE        },
   { "FFB",                FIL_ATTR_XABFHC            },
   { "FID",                FIL_ATTR_NAML              },
/*   { "FILE_LENGTH_HINT",   FIL_ATTR_ITEM_FLENGTH_HINT },  */
   { "FSZ",                FIL_ATTR_FAB               },
   { "GBC",                FIL_ATTR_XABFHC            },
   { "GBC32",              FIL_ATTR_ITEM_GBC32        },
/*   { "GBCFLAGS",           FIL_ATTR_ITEM_GBCFLAGS     },  */
   { "GRP",                FIL_ATTR_XABPRO            },
   { "JOURNAL_FILE",       FIL_ATTR_FAB               },
   { "KNOWN",              FIL_ATTR_FAB               },
   { "LOCKED",             FIL_ATTR_ITEM_LOCKED       },
   { "LRL",                FIL_ATTR_XABFHC            },
   { "MBM",                FIL_ATTR_XABPRO            },
   { "MOVE",               FIL_ATTR_ITEM_NOMOVE       },
   { "MRN",                FIL_ATTR_FAB               },
   { "MRS",                FIL_ATTR_FAB               },
   { "NOA",                FIL_ATTR_XABSUM            },
   { "NOBACKUP",           FIL_ATTR_ITEM_NOBACKUP     },
   { "NOK",                FIL_ATTR_XABSUM            },
   { "ORG",                FIL_ATTR_FAB               },
   { "PRESHELVED",         FIL_ATTR_ITEM_PRESHELVED   },
   { "PRO",                FIL_ATTR_XABPRO            },
   { "PVN",                FIL_ATTR_XABSUM            },
   { "RAT",                FIL_ATTR_FAB               },
   { "RCK",                FIL_ATTR_FAB               },
   { "RDT",                FIL_ATTR_XABDAT            },
   { "RFM",                FIL_ATTR_FAB               },
   { "RU",                 FIL_ATTR_FAB               },
   { "RVN",                FIL_ATTR_XABFHC            },
   { "SHELVABLE",          FIL_ATTR_ITEM_NOSHELVABLE  },
   { "SHELVED",            FIL_ATTR_ITEM_SHELVED      },
/*   { "STORED_SEMANTICS",   FIL_ATTR_ITEM_STORED_SEM   },  */
   { "UIC",                FIL_ATTR_XABPRO            },
   { "VERLIMIT",           FIL_ATTR_XABFHC            },
   { "WCK",                FIL_ATTR_FAB               },
} ;


streng *vms_f_file_attributes( tsd_t *TSD, cparamboxptr parms )
{
   const struct dvi_items_type *item ;
   unsigned int rc, result_word ;
   streng *res ;
   struct FAB fab = cc$rms_fab ;
   struct NAML naml = cc$rms_naml ;

   union {
      struct XABDAT xabdat ;
      struct XABFHC xabfhc ;
      XABITMDEF     xabitm ;
      struct XABPRO xabpro ;
      struct XABSUM xabsum ;
   } xab ;

   struct _ile3 xab_items[2] = {
      { sizeof( result_word ), 0, &result_word, NULL },
      { 0, 0, NULL, NULL }
   } ;

   checkparam( parms, 2, 2, "VMS_F_FILE_ATTRIBUTES" ) ;
   item = item_info( parms->next->value->value,
                     parms->next->value->len,
                     file_attribs, ARRAY_LEN(file_attribs)) ;

   /* get the index and the XAB to query, if any */
   int item_index = item - &(file_attribs[0]) ;
   enum file_attr_type attr_type = item->item_code ;

   fab.fab$l_naml = &naml ;

   naml.naml$l_long_filename        = parms->value->value ;
   naml.naml$l_long_filename_size   = parms->value->len ;

   switch (attr_type) {
   case FIL_ATTR_FAB:
   case FIL_ATTR_NAML:
      break;            /* no additional XABs needed */

   case FIL_ATTR_XABDAT:
      xab.xabdat = cc$rms_xabdat ;
      fab.fab$l_xab = &(xab.xabdat) ;
      break ;

   case FIL_ATTR_XABFHC:
      xab.xabfhc = cc$rms_xabfhc ;
      fab.fab$l_xab = &(xab.xabfhc) ;
      break ;

   case FIL_ATTR_XABPRO:
      xab.xabpro = cc$rms_xabpro ;
      fab.fab$l_xab = &(xab.xabpro) ;
      break ;

   case FIL_ATTR_XABSUM:
      xab.xabsum = cc$rms_xabsum ;
      fab.fab$l_xab = &(xab.xabsum) ;
      break ;

   case FIL_ATTR_ITEM_ERASE:
/*   case FIL_ATTR_ITEM_FLENGTH_HINT: */
   case FIL_ATTR_ITEM_GBC32:
/*   case FIL_ATTR_ITEM_GBCFLAGS: */
   case FIL_ATTR_ITEM_LOCKED:
   case FIL_ATTR_ITEM_NOBACKUP:
   case FIL_ATTR_ITEM_NOMOVE:
   case FIL_ATTR_ITEM_NOSHELVABLE:
   case FIL_ATTR_ITEM_PRESHELVED:
   case FIL_ATTR_ITEM_SHELVED:
/*   case FIL_ATTR_ITEM_STORED_SEM: */
      xab.xabitm.xab$b_cod = XAB$C_ITM ;
      xab.xabitm.xab$b_bln = XAB$K_ITMLEN ;
      xab.xabitm.xab$l_nxt = NULL ;
      xab.xabitm.xab$l_itemlist = xab_items ;
      xab.xabitm.xab$b_mode = XAB$K_SENSEMODE ;
      xab_items[0].ile3$w_code = attr_type ;
      fab.fab$l_xab = &(xab.xabitm) ;
      break ;

   default:
      exiterror( ERR_INTERPRETER_FAILURE, 1, __FILE__, __LINE__, "" ) ;
   }

   if (item_index == FIL_KNOWN)
   {
      /* This field is undocumented in 'The Grey Wall', I spent quite
       * some time trying to find this ... sigh. Also note that the
       * return code RMS$_KFF is an Digital internal code.
       */
      fab.fab$l_fop |= FAB$M_KFO ;
      fab.fab$l_ctx = 0 ;
#if 0
      naml.naml$b_nop |= NAML$M_NOCONCEAL ;
      naml.naml$l_long_expand       = temp_space ;
      naml.naml$l_long_expand_alloc = NAML$C_MAXRSS ;
#endif
   }

   rc = sys$open( &fab ) ;

   if (item_index == FIL_KNOWN)
   {
      if (rc==RMS$_NORMAL || rc==RMS$_KFF)
      {
         /* OK, we ought to check the rc from sys$close() ... */
         sys$close( &fab ) ;
         return boolean( TSD, fab.fab$l_ctx ) ;
      }
   }
   if (rc != RMS$_FNF)
   {
      if (rc != RMS$_NORMAL)
      {
         vms_error( TSD, rc ) ;
         return nullstringptr() ;
      }
   }
   else
      return nullstringptr() ;

   switch (item_index)
   {
      case FIL_AI:   res = boolean( TSD, fab.fab$v_ai ); break ;
      case FIL_ALQ:  res = int_to_streng( TSD, fab.fab$l_alq ); break ;
      case FIL_BDT:  res = date_time( TSD, xab.xabdat.xab$q_bdt ); break ;
      case FIL_BI:   res = boolean( TSD, fab.fab$v_bi ); break ;
      case FIL_BKS:  res = int_to_streng( TSD, fab.fab$b_bks ); break ;
      case FIL_BLS:  res = int_to_streng( TSD, fab.fab$w_bls ); break ;
      case FIL_CBT:  res = boolean( TSD, fab.fab$l_fop & FAB$M_CBT ); break ;
      case FIL_CDT:  res = date_time( TSD, xab.xabdat.xab$q_cdt ); break ;
      case FIL_CTG:  res = boolean( TSD, fab.fab$l_fop & FAB$M_CTG ); break ;
      case FIL_DEQ:  res = int_to_streng( TSD, fab.fab$w_deq ); break ;
      case FIL_DID:  res = internal_id( TSD, naml.naml$w_did ); break ;
      case FIL_DIRECTORY:  res = boolean( TSD, naml.naml$v_is_directory ); break ;
      case FIL_DVI:
         res = Str_ncreTSD( naml.naml$l_long_dev, naml.naml$l_long_dev_size );
         break ;
      case FIL_EDT:  res = date_time( TSD, xab.xabdat.xab$q_edt ); break ;
      case FIL_EOF:
         res = int_to_streng( TSD, xab.xabfhc.xab$l_ebk -
                                  (xab.xabfhc.xab$w_ffb == 0 )); break ;
      case FIL_FFB:     res = int_to_streng( TSD, xab.xabfhc.xab$w_ffb ); break ;
      case FIL_FID:     res = internal_id( TSD, naml.naml$w_fid ); break ;
/* TODO: case FIL_FILE_LENGTH_HINT:  */
      case FIL_FSZ:     res = int_to_streng( TSD, fab.fab$b_fsz ); break ;
      case FIL_GBC:     res = int_to_streng( TSD, xab.xabfhc.xab$w_gbc ); break ;
/* TODO: case FIL_GBCFLAGS:   */
      case FIL_GRP:     res = int_to_streng( TSD, xab.xabpro.xab$w_grp ); break ;
      case FIL_JOURNAL_FILE:  res = boolean( TSD, fab.fab$v_journal_file ); break ;
      case FIL_KNOWN: res = nullstringptr() ; /* must be nonexistent */
         break ;
      case FIL_LRL:     res = int_to_streng( TSD, xab.xabfhc.xab$w_lrl ); break ;
      case FIL_MBM:     res = int_to_streng( TSD, xab.xabpro.xab$w_mbm ); break ;
      case FIL_MRN:  res = int_to_streng( TSD, fab.fab$l_mrn ); break ;
      case FIL_MRS:  res = int_to_streng( TSD, fab.fab$w_mrs ); break ;
      case FIL_NOA:  res = int_to_streng( TSD, xab.xabsum.xab$b_noa ); break ;
      case FIL_NOK:  res = int_to_streng( TSD, xab.xabsum.xab$b_nok ); break ;
      case FIL_ORG:
         switch ( fab.fab$b_org )
         {
            case FAB$C_IDX: res = Str_creTSD( "IDX" ) ; break ;
            case FAB$C_REL: res = Str_creTSD( "REL" ) ; break ;
            case FAB$C_SEQ: res = Str_creTSD( "SEQ" ) ; break ;
            default: exiterror( ERR_INTERPRETER_FAILURE, 1, __FILE__, __LINE__, "" )  ;
         }
         break ;
      case FIL_PRO:  res = get_prot( TSD, xab.xabpro.xab$w_pro ); break ;
      case FIL_PVN:  res = int_to_streng( TSD, xab.xabsum.xab$w_pvn ); break ;
      case FIL_RAT:
         if (fab.fab$b_rat & FAB$M_BLK)
            res = nullstringptr() ;
         else if (fab.fab$b_rat & FAB$M_CR)
            res = Str_creTSD( "CR" ) ;
         else if (fab.fab$b_rat & FAB$M_FTN)
            res = Str_creTSD( "FTN" ) ;
         else if (fab.fab$b_rat & FAB$M_PRN)
            res = Str_creTSD( "PRN" ) ;
         else
            res = nullstringptr() ;
         break ;
      case FIL_RCK:  res = boolean( TSD, fab.fab$l_fop & FAB$M_RCK ); break ;
      case FIL_RDT:  res = date_time( TSD, xab.xabdat.xab$q_rdt ); break ;
      case FIL_RFM:
         switch ( fab.fab$b_rfm )
         {
            case FAB$C_VAR: res = Str_creTSD( "VAR" ) ; break ;
            case FAB$C_FIX: res = Str_creTSD( "FIX" ) ; break ;
            case FAB$C_VFC: res = Str_creTSD( "VFC" ) ; break ;
            case FAB$C_UDF: res = Str_creTSD( "UDF" ) ; break ;
            case FAB$C_STM: res = Str_creTSD( "STM" ) ; break ;
            case FAB$C_STMLF: res = Str_creTSD( "STMLF" ) ; break ;
            case FAB$C_STMCR: res = Str_creTSD( "STMCR" ) ; break ;
            default: exiterror( ERR_INTERPRETER_FAILURE, 1, __FILE__, __LINE__, "" )  ;
         }
         break ;
      case FIL_RU:   res = boolean( TSD, fab.fab$v_ru ); break ;
      case FIL_RVN:  res = int_to_streng( TSD, xab.xabdat.xab$w_rvn ); break ;
/* TODO: case FIL_STORED_SEMANTICS:   */
      case FIL_UIC:  res = get_uic( TSD, (const UICDEF *)&(xab.xabpro.xab$l_uic) ); break ;
      case FIL_VERLIMIT: res = int_to_streng( TSD, xab.xabfhc.xab$w_verlimit ); break ;
      case FIL_WCK:  res = boolean( TSD, fab.fab$l_fop & FAB$M_WCK ); break ;

      /* case that returns an inverted boolean item (NOMOVE) */
      case FIL_MOVE:    res = boolean( TSD, !(result_word ) ); break ;

      /* cases that return boolean items */
      case FIL_ERASE:
      case FIL_LOCKED:
      case FIL_NOBACKUP:
      case FIL_PRESHELVED:
      case FIL_SHELVABLE:
      case FIL_SHELVED:
         res = boolean( TSD, result_word ) ;
         break ;

      /* cases that return longword items */
      case FIL_GBC32:   res = int_to_streng( TSD, result_word ); break ;

      default:
         exiterror( ERR_INTERPRETER_FAILURE, 1, __FILE__, __LINE__, "" )  ;
   }

   if (rc == RMS$_NORMAL)
   {
      rc = sys$close( &fab ) ;
      if (rc != RMS$_NORMAL )
      {
         vms_error( TSD, rc ) ;
         return nullstringptr() ;
      }
   }
   return res ;
}


streng *vms_f_extract( tsd_t *TSD, cparamboxptr parms )
{
   int start, length ;
   streng *string ;

   checkparam( parms, 3, 3, "VMS_F_EXTRACT" ) ;
   start = atozpos( TSD, parms->value, "VMS_F_EXTRACT", 1 ) ;
   length = atozpos( TSD, (parms=parms->next)->value, "VMS_F_EXTRACT", 2 ) ;
   string = parms->next->value ;

   if (start>string->len)
      start = string->len ;

   if (length > string->len - start)
      length = (string->len - start) ;

   return Str_ncreTSD( string->value+start, length ) ;
}

streng *vms_f_element( tsd_t *TSD, cparamboxptr parms )
{
   int number, count ;
   streng *string, *result ;
   char delim, *cptr, *cend, *cmax ;

   checkparam( parms, 3, 3, "VMS_F_ELEMENT" ) ;

   number = atozpos( TSD, parms->value, "VMS_F_ELEMENT", 1 ) ;
   delim = getonechar( TSD, (parms=parms->next)->value, "VMS_F_ELEMENT", 2) ;
   string = parms->next->value ;

   cptr = string->value ;
   cend = cptr + string->len ;
   for (count=0;count<number && cptr<cend;)
      if (*(cptr++)==delim) count++ ;

   if (count<number)
   {
      result = Str_makeTSD( 1 ) ;
      result->len = 1 ;
      result->value[0] = delim ;
   }
   else
   {
      for (cmax=cptr; *cmax!=delim && cmax<cend; cmax++) ;
      result = Str_ncreTSD( cptr, (cmax - cptr) ) ;
   }

   return result ;
}


static streng *convert_bin( tsd_t *TSD, cparamboxptr parms, const int issigned, const char *bif )
{
   int start, length, obyte, obit, count, bit=0 ;
   streng *string, *result, *temp ;

   checkparam( parms, 3, 3, bif ) ;

   start = atozpos( TSD, parms->value, bif, 1 ) ;
   length = atozpos( TSD, parms->next->value, bif, 2 ) ;
   string = parms->next->next->value ;

   if (issigned)
   {
      start++ ;
      length-- ;
   }

   if ((start+length > string->len*8) || length<0)
      exiterror( ERR_INCORRECT_CALL , 0 ) ;

   temp = Str_makeTSD((start+length)/8 + 2) ;
   obyte = (start+length)/8 + 1 ;
   temp->len = obyte + 1 ;
   obit = 7 ;
   for (count=0; count<=obyte; temp->value[count++] = 0x00) ;

   for (count=start+length-1; count>=start; count--)
   {
      bit = (string->value[count/8] >> (7-(count%8))) & 1 ;
      temp->value[obyte] |= bit << (7-obit--) ;
      if (obit<0)
      {
         obit = 7 ;
         obyte-- ;
      }
   }

   if (issigned)
      bit = (string->value[count/8] >> (7-(count%8))) & 1 ;

   if (issigned && bit)
      for (;obyte>=0;)
      {
         temp->value[obyte] |= 1 << (7-obit--) ;
         if (obit<0)
         {
            obit = 7 ;
            obyte-- ;
         }
      }

   result = str_digitize( TSD, temp, 0, 1, bif, 0 ) ;
   FreeTSD( temp ) ;
   return result ;
}



streng *vms_f_cvui( tsd_t *TSD, cparamboxptr parms )
{
   return convert_bin( TSD, parms, 0, "VMS_F_CVUI" ) ;
}

streng *vms_f_cvsi( tsd_t *TSD, cparamboxptr parms )
{
   return convert_bin( TSD, parms, 1, "VMS_F_CVSI" ) ;
}


static const char *vms_weekdays[] = { "Monday", "Tuesday", "Wednesday",
                                      "Thursday", "Friday", "Saturday",
                                      "Sunday" } ;
static const char *vms_months[] = { "", "JAN", "FEB", "MAR", "APR", "MAY",
                                        "JUN", "JUL", "AUG", "SEP", "OCT",
                                        "NOV", "DEC" } ;

enum outs { absolute, comparison, delta } ;
enum funcs { year, month, day, hour, minute, second, hundredth,
               weekday, time_part, date_part, datetime } ;


static char *read_abs_time( char *ptr, char *end, unsigned short *times )
{
   int cnt, increment, rc ;
   char *tmp ;

   rc = sys$numtim( times, NULL ) ;

   if (ptr>=end) exiterror( ERR_INCORRECT_CALL, 0 ) ;
   if (*ptr=='-')
   {
      ptr++ ;
      goto abs_hours ;
   }

   if (*ptr=='+')
      return ptr ;

   if (isspace(*ptr))
      return ptr ;

   if (*ptr==':')
   {
      ptr++ ;
      goto abs_minutes ;
   }

   if (!isdigit(*ptr))
   {
      if (ptr+3>=end ) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      for (cnt=1; cnt<=12; cnt++)
         if (!strncasecmp(ptr, vms_months[cnt], 3))
         {
            ptr += 3 ;
            times[month] = cnt ;
            if (ptr>=end)
               return ptr ;
            else if (*ptr==':')
            {
               ptr++ ;
               goto abs_hours ;
            }
            else if (*ptr=='-')
            {
               ptr++ ;
               goto abs_years ;
            }
            else
               return ptr ;
         }
      exiterror( ERR_INCORRECT_CALL , 0 ) ;
   }
   else
   {
      for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      if (ptr>=end || isspace(*ptr) || *ptr==':')
      {
         if (ptr<end && *ptr==':') ptr++ ;
         if (cnt>23) exiterror( ERR_INCORRECT_CALL, 0 ) ;
         times[hour] = cnt ;
         goto abs_minutes ;
      }
      else if (*ptr=='-')
      {
         ptr++ ;
         times[day] = cnt ;
         goto abs_months ;
      }
      else
        return ptr ;
   }


   abs_months:
   if (ptr<end && isalpha(*ptr))
   {
      if (ptr+3>=end) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      for (cnt=1; cnt<=12; cnt++)
         if (!strncasecmp(ptr, vms_months[cnt], 3))
         {
            ptr += 3 ;
            times[month] = cnt ;

            if (ptr>=end)
               return ptr ;
            else if (*ptr==':')
            {
               ptr++ ;
               goto abs_hours ;
            }
            else if (*ptr=='-')
            {
               ptr++ ;
               goto abs_years ;
            }
            else
               return ptr ;
         }
      exiterror( ERR_INCORRECT_CALL , 0 ) ;
   }
   else if (ptr>=end || isspace(*ptr))
      return ptr ;
   else if (*ptr=='-')
   {
      ptr++ ;
      goto abs_years ;
   }
   else if (*ptr==':')
   {
      ptr++ ;
      goto abs_hours ;
   }
   else
      exiterror( ERR_INCORRECT_CALL , 0 ) ;


   abs_years:
   if (ptr<end && isdigit(*ptr))
   {
      for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      if (cnt>9999) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[year] = cnt ;
      if (ptr<end && *ptr==':')
      {
         ptr++ ;
         goto abs_hours ;
      }
      else
         return ptr ;
   }
   else if (ptr<end && *ptr==':')
   {
      ptr++ ;
      goto abs_hours ;
   }
   else
      return ptr ;


   abs_hours:
   if (ptr<end && isdigit(*ptr))
   {
      for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      if (cnt>23) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[hour] = cnt ;
      if (ptr<end && *ptr==':')
      {
         ptr++ ;
         goto abs_minutes ;
      }
      else
         return ptr ;
   }
   else if (ptr<end && *ptr==':')
   {
      ptr++ ;
      goto abs_minutes ;
   }
   else
      return ptr ;


   abs_minutes:
   if (ptr<end && isdigit(*ptr))
   {
      for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      if (cnt>59) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[minute] = cnt ;
      if (ptr<end && *ptr==':')
      {
         ptr++ ;
         goto abs_seconds ;
      }
      else
         return ptr ;
   }
   else if (ptr<end && *ptr==':')
   {
      ptr++ ;
      goto abs_seconds ;
   }
   else
      return ptr ;


   abs_seconds:
   if (ptr<end && isdigit(*ptr))
   {
      for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      if (cnt>59) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[second] = cnt ;
      if (ptr<end && *ptr=='.')
      {
         ptr++ ;
         goto abs_hundredths ;
      }
      else
         return ptr ;
   }
   else if (ptr<end && *ptr=='.')
   {
      ptr++ ;
      goto abs_hundredths ;
   }
   else
      return ptr ;


   abs_hundredths:
   if (ptr<end && isdigit(*ptr))
   {
      tmp = ptr ;
      for (cnt=0; ptr<end && ptr<tmp+2 && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      increment = (ptr<end && isdigit(*ptr) && (*ptr-'0'>=5)) ;
      for (;ptr<end && isdigit(*ptr); ptr++) ;
      times[hundredth] = cnt + increment ;
      return ptr ;
   }
   else
      return ptr ;
}


static char *read_delta_time( char *ptr, char *end, unsigned short *times )
{
   int cnt, increment ;
   char *tmp ;

   for (cnt=0; cnt<7; times[cnt++]=0) ;

   if (ptr>=end) exiterror( ERR_INCORRECT_CALL, 0 ) ;
   if (*ptr=='-')
   {
      ptr++ ;
      goto delta_hours ;
   }

   if (*ptr==':')
   {
      ptr++ ;
      goto delta_minutes ;
   }

   if (!isdigit( *ptr )) exiterror( ERR_INCORRECT_CALL, 0 ) ;
   for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
      cnt = cnt*10 + *ptr-'0' ;

   if (ptr>=end || isspace(*ptr) || *ptr==':')
   {
      if (ptr<end && *ptr==':') ptr++ ;
      if (cnt>23) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[hour] = cnt ;
      goto delta_minutes ;
   }
   else
   {
      if (*ptr!='-') exiterror( ERR_INCORRECT_CALL, 0 ) ;
      ptr++ ;
      if (cnt>9999) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[day] = cnt ;
      goto delta_hours ;
   }

   delta_hours:
   if (ptr<end && isdigit(*ptr))
   {
      for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      if (cnt>23) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[hour] = cnt ;
      if (ptr<end && *ptr==':')
      {
         ptr++ ;
         goto delta_minutes ;
      }
      else
         return ptr ;
   }
   else if (ptr<end && *ptr==':')
   {
      ptr++ ;
      goto delta_minutes ;
   }
   else
      return ptr ;


   delta_minutes:
   if (ptr<end && isdigit(*ptr))
   {
      for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      if (cnt>59) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[minute] = cnt ;
      if (ptr<end && *ptr==':')
      {
         ptr++ ;
         goto delta_seconds ;
      }
      else
         return ptr ;
   }
   else if (ptr<end && *ptr==':')
   {
      ptr++ ;
      goto delta_seconds ;
   }
   else
      return ptr ;


   delta_seconds:
   if (ptr<end && isdigit(*ptr))
   {
      for (cnt=0; ptr<end && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      if (cnt>59) exiterror( ERR_INCORRECT_CALL, 0 ) ;
      times[second] = cnt ;
      if (ptr<end && *ptr=='.')
      {
         ptr++ ;
         goto delta_hundredths ;
      }
      else
         return ptr ;
   }
   else if (ptr<end && *ptr=='.')
   {
      ptr++ ;
      goto delta_hundredths ;
   }
   else
      return ptr ;


   delta_hundredths:
   if (ptr<end && isdigit(*ptr))
   {
      tmp = ptr ;
      for (cnt=0; ptr<end && ptr<tmp+2 && isdigit(*ptr); ptr++)
         cnt = cnt*10 + *ptr-'0' ;

      increment = (ptr<end && isdigit(*ptr) && (*ptr-'0'>=5)) ;
      for (;ptr<end && isdigit(*ptr); ptr++) ;
      times[hundredth] = cnt + increment ;
      return ptr ;
   }
   else
      return ptr ;
}


streng *vms_f_cvtime( tsd_t *TSD, cparamboxptr parms )
{
   streng *item=NULL, *input=NULL, *output=NULL, *result ;
   int rc, cnt, abs=0 ;
   unsigned short times[7] ;
   unsigned short timearray[7] ;
   GENERIC_64 btime ;
   char *cptr, *cend, *ctmp, *cptr2 ;
   enum funcs func ;
   enum outs out ;

   checkparam( parms, 0, 3, "VMS_F_CVTIME" ) ;
   func = datetime ;
   out = comparison ;

   input = parms->value ;
   if (parms->next)
   {
      output = parms->next->value ;
      if (parms->next->next)
         item = parms->next->next->value ;
   }

   if (item)
   {
      for (cnt=0; cnt<item->len; cnt++)
         item->value[cnt] = toupper(item->value[cnt]) ;

      if (item->len==4 && !strncasecmp(item->value, "YEAR", 4))
         func = year ;
      else if (item->len==5 && !strncasecmp(item->value, "MONTH", 5))
         func = month ;
      else if (item->len==8 && !strncasecmp(item->value, "DATETIME", 8))
         func = datetime ;
      else if (item->len==3 && !strncasecmp(item->value, "DAY", 3))
         func = day ;
      else if (item->len==4 && !strncasecmp(item->value, "DATE", 4))
         func = date_part ;
      else if (item->len==4 && !strncasecmp(item->value, "TIME", 4))
         func = time_part ;
      else if (item->len==4 && !strncasecmp(item->value, "HOUR", 4))
         func = hour ;
      else if (item->len==6 && !strncasecmp(item->value, "SECOND", 6))
         func = second ;
      else if (item->len==6 && !strncasecmp(item->value, "MINUTE", 6))
         func = minute ;
      else if (item->len==9 && !strncasecmp(item->value, "HUNDREDTH", 9))
         func = hundredth ;
      else if (item->len==7 && !strncasecmp(item->value, "WEEKDAY", 7))
         func = weekday ;
      else
         exiterror( ERR_INCORRECT_CALL , 0 ) ;
   }

   if (output)
   {
      for (cnt=0; cnt<output->len; cnt++)
         output->value[cnt] = toupper(output->value[cnt]) ;

      if (output->len==5 && !strncasecmp(output->value, "DELTA", 5))
         out = delta ;
      else if (output->len==10 && !strncasecmp(output->value, "COMPARISON", 10))
         abs = 0 ;
      else if (output->len==8 && !strncasecmp(output->value, "ABSOLUTE", 8))
         abs = 1 ;
      else
         exiterror( ERR_INCORRECT_CALL , 0 ) ;
   }

   if (out==delta)
      if (func==year  || func==month || func==weekday)
         exiterror( ERR_INCORRECT_CALL , 0 ) ;

   if (input)
   {
      unsigned __int64 atime, dtime, xtime ;
      unsigned short ttimes[7] = {0,0,0,0,0,0,1} ;
      unsigned int rc2, increment ;

      lib$cvt_vectim( ttimes, &xtime ) ;
      cptr = input->value ;
      cend = cptr + input->len ;

      for (ctmp=cptr;ctmp<cend;ctmp++)
         *ctmp = toupper(*ctmp) ;

      for (;isspace(*cptr);cptr++) ; /* strip leading spaces */
      if (out!=delta)
      {
         if (cptr<cend && *cptr!='-')
         {
            cptr = read_abs_time( cptr, cend, times ) ;
            if ((increment=(times[hundredth]==100)))
               times[hundredth] -= 1 ; ;

            rc = lib$cvt_vectim( times, &btime.gen64$q_quadword ) ;
            if (increment)
            {
               lib$add_times( &xtime, &btime.gen64$q_quadword, &dtime ) ;
               btime.gen64$q_quadword = dtime ;
            }
         }
         else
         {
            rc = sys$gettim( &btime ) ;
         }

         if (cptr<cend && (*cptr=='-' || *cptr=='+'))
         {
            char oper = *cptr ;
            cptr2 = read_delta_time( ++cptr, cend, times ) ;
            if ((increment=(times[6]==100)))
               times[6] -= 1 ;

            rc2 = lib$cvt_vectim( times, &dtime ) ;
            if (increment)
            {
               lib$add_times( &dtime, &xtime, &atime ) ;
               dtime = atime ;
            }

            if (oper=='+')
               rc = lib$add_times( &btime.gen64$q_quadword, &dtime, &atime ) ;
            else
               rc = lib$sub_times( &btime.gen64$q_quadword, &dtime, &atime ) ;

            btime.gen64$q_quadword = atime ;
         }
      }
      else
      {
         cptr = read_delta_time( cptr, cend, times ) ;
         if ((increment=(times[6]==100)))
            times[6] -= 1 ;

         rc = lib$cvt_vectim( times, &btime.gen64$q_quadword ) ;
         if (increment)
         {
            lib$add_times( &xtime, &btime.gen64$q_quadword, &atime ) ;
            btime.gen64$q_quadword = atime ;
         }
      }
   }
   else
      rc = sys$gettim( &btime ) ;

   if (rc!=SS$_NORMAL && rc!=LIB$_NORMAL)
   {
      vms_error( TSD, rc ) ;
      return nullstringptr() ;
   }

   rc = sys$numtim( timearray, &btime ) ;
   if (rc!=SS$_NORMAL)
   {
      vms_error( TSD, rc ) ;
      return nullstringptr() ;
   }

   switch (func)
   {
      case year:
         result = Str_makeTSD( 5 ) ;
         snprintf( result->value, 5, ((abs) ? "%04d" : "%d"), timearray[func]);
         result->len = strlen( result->value ) ;
         break ;

      case hour:
      case minute:
      case second:
      case hundredth:
         abs = 0 ;
      case day:
         result = Str_makeTSD( 3 ) ;
         snprintf( result->value, 3, ((abs) ? "%d" : "%02d"), timearray[func]);
         result->len = strlen( result->value ) ;
         break ;

      case month:
         if (abs)
            result = Str_creTSD( vms_months[ func ]) ;
         else
         {
            result = Str_makeTSD( 3 ) ;
            snprintf( result->value, 3, "%02d", timearray[month]) ;
            result->len = 2 ;
         }
         break ;

      case time_part:
         result = Str_makeTSD( 12 ) ;
         snprintf(result->value, 12, "%02d:%02d:%02d.%02d", timearray[hour],
              timearray[minute], timearray[second], timearray[hundredth]) ;
         result->len = 11 ;
         break ;

      case date_part:
         result = Str_makeTSD( 12 ) ;
         if (out==delta)
            snprintf( result->value, 12, "%d", timearray[day] ) ;
         else if (abs)
            snprintf( result->value, 12, "%d-%s-%d", timearray[day],
                vms_months[timearray[month]], timearray[year] ) ;
         else
            snprintf( result->value, 12, "%04d-%02d-%02d", timearray[year],
                timearray[month], timearray[day] ) ;

         result->len = strlen( result->value ) ;
         break ;

      case datetime:
         result = Str_makeTSD( 24 ) ;
         if (out==delta)
            snprintf( result->value, 24, "%d %02d:%02d:%02d.%02d",
                   timearray[day], timearray[hour], timearray[minute],
                   timearray[second], timearray[hundredth] ) ;
         else if (abs)
            snprintf( result->value, 24, "%d-%s-%d %02d:%02d:%02d.%02d",
                   timearray[day], vms_months[timearray[month]],
                   timearray[year], timearray[hour], timearray[minute],
                   timearray[second], timearray[hundredth] ) ;
         else
            snprintf( result->value, 24, "%04d-%02d-%02d %02d:%02d:%02d.%02d",
                   timearray[year], timearray[month], timearray[day],
                   timearray[hour], timearray[minute], timearray[second],
                   timearray[hundredth] ) ;
         result->len = strlen( result->value ) ;
         break ;

      case weekday:
      {
         unsigned int op=LIB$K_DAY_OF_WEEK, res ;
         rc = lib$cvt_from_internal_time( &op, &res, &btime ) ;
         if (rc!=LIB$_NORMAL)
         {
            vms_error( TSD, rc ) ;
            return nullstringptr() ;
         }
         result = Str_creTSD( vms_weekdays[res-1] ) ;
         break ;
      }

      default: exiterror( ERR_INTERPRETER_FAILURE, 1, __FILE__, __LINE__, "" )  ;
   }

   return result ;
}


streng *vms_f_fao( tsd_t *TSD, cparamboxptr parms )
{
   void *prmlst[30] = {NULL} ;
   int i, cnt, paran, rc, pcnt=0, icnt=0 ;
   int int_list[30], dcnt=0, xper ;
   struct dsc$descriptor_s dscs[15] ;
   cparamboxptr p ;
   char buffer[512], *cstart, *cptr, *cend ;
   $DESCRIPTOR( ctrl, "" ) ;
   $DESCRIPTOR( outbuf, buffer ) ;
   unsigned short outlen ;

   if (parms->value==NULL)
      exiterror( ERR_INCORRECT_CALL , 0 ) ;

   ctrl.dsc$a_pointer = parms->value->value ;
   ctrl.dsc$w_length = parms->value->len ;

   cptr = cstart = parms->value->value ;
   cend = cptr + parms->value->len ;

   p = parms->next ;

   for (cptr=cstart; cptr<cend; cptr++)
   {
      if (*cptr!='!') continue ;

      if (*(++cptr)=='#')
      {
         cptr++ ;
         if (!p || !p->value)
            exiterror( ERR_INCORRECT_CALL , 0 ) ;

         cnt = atopos( TSD, p->value, "VMS_F_FAO", pcnt ) ;
         prmlst[pcnt++] = int_list + icnt ;
         int_list[icnt++] = cnt ;
         p = p->next ;
      }
      else if (!isdigit(*cptr))
         cnt = 1 ;
      else
         for (cnt=0;cptr<cend && isdigit(*cptr); cptr++)
            cnt = cnt*10 + *cptr-'0' ;

      paran = 0 ;
      if (cptr<cend && *cptr=='(')
      {
         paran = 1 ;
         cptr++ ;
         if (*cptr=='#')
         {
            if (!p || !p->value)
               exiterror( ERR_INCORRECT_CALL , 0 ) ;

            prmlst[pcnt++] = int_list + icnt ;
            int_list[icnt++] = atopos( TSD, p->value, "VMS_F_FAO", 0 ) ;
            p = p->next ;
         }
         else
            for (;cptr<cend && isdigit(*cptr); cptr++ ) ;
      }

      if (cptr<cend)
      {
         xper = toupper(*cptr) ;
         if (xper=='O' || xper=='X' || xper=='Z' || xper=='U' || xper=='S')
         {
            cptr++ ;
            xper = toupper(*cptr) ;
            if (xper!='B' && xper!='W' && xper!='L')
               exiterror( ERR_INCORRECT_CALL , 0 ) ;

            for (i=0; i<cnt; i++)
            {
               if (!p || !p->value)
                  exiterror( ERR_INCORRECT_CALL , 0 ) ;

               prmlst[pcnt++] = (void *)myatol( TSD, p->value ) ;
               p = p->next ;
            }
         }
         else if (toupper(*cptr)=='A')
         {
            cptr++ ;
            if (cptr<cend && toupper(*cptr)!='S')
               exiterror( ERR_INCORRECT_CALL , 0 ) ;

            for (i=0; i<cnt; i++ )
            {
               if (!p || !p->value)
                  exiterror( ERR_INCORRECT_CALL , 0 ) ;

               dscs[dcnt].dsc$b_class = DSC$K_CLASS_S ;
               dscs[dcnt].dsc$b_dtype = DSC$K_DTYPE_T ;
               dscs[dcnt].dsc$a_pointer = p->value->value ;
               dscs[dcnt].dsc$w_length = p->value->len ;
               prmlst[pcnt++] = &(dscs[dcnt++]) ;
               p = p->next ;
            }
         }
      }
      else
         exiterror( ERR_INCORRECT_CALL , 0 ) ;

      if (paran)
         if (cptr<cend-1 && *(++cptr)!=')')
             exiterror( ERR_INCORRECT_CALL , 0 ) ;
   }

   rc = sys$faol( &ctrl, &outlen, &outbuf, prmlst ) ;
   if (rc!=SS$_NORMAL)
   {
      vms_error( TSD, rc ) ;
/*      return nullstringptr() ; */
   }

   return Str_ncreTSD( buffer, outlen ) ;
}
