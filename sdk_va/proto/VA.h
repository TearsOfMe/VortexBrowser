#ifndef PROTO_VA_H
#define PROTO_VA_H

/*
**	$Id$
**
**	Prototype/inline/pragma header file combo
**
**	Copyright (C) 2019 by A-EON Technology Limited, all rights reserved
*/

#ifndef EXEC_LIBRARIES_H
#include <exec/libraries.h>
#endif
#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif
#ifndef EXPANSION_EXPANSION_H
#include <expansion/expansion.h>
#endif
#ifndef GRAPHICS_RASTPORT_H
#include <graphics/rastport.h>
#endif
#ifndef INTUITION_SCREENS_H
#include <intuition/screens.h>
#endif
#ifndef VA_VA_H
#include <va/va.h>
#endif
#ifndef VA_VA_AMIGAOS_H
#include <va/va_amigaos.h>
#endif

/****************************************************************************/

#ifndef __NOLIBBASE__
 #ifndef __USE_BASETYPE__
  extern struct Library * VABase;
 #else
  extern struct VABase * VABase;
 #endif /* __USE_BASETYPE__ */
#endif /* __NOLIBBASE__ */

/****************************************************************************/

#ifdef __amigaos4__
 #include <interfaces/VA.h>
 #ifdef __USE_INLINE__
  #include <inline4/VA.h>
 #endif /* __USE_INLINE__ */
 #ifndef CLIB_VA_PROTOS_H
  #define CLIB_VA_PROTOS_H 1
 #endif /* CLIB_VA_PROTOS_H */
 #ifndef __NOGLOBALIFACE__
    extern struct VAIFace *IVA;
#endif /* __NOGLOBALIFACE__ */
#else /* __amigaos4__ */
 #ifndef CLIB_VA_PROTOS_H
  #include <clib/VA_protos.h>
 #endif /* CLIB_VA_PROTOS_H */
 #if defined(__GNUC__)
  #ifndef __PPC__
   #include <inline/VA.h>
  #else /* __PPC__ */
   #include <ppcinline/VA.h>
  #endif /* __PPC__ */
 #elif defined(__VBCC__)
  #ifndef __PPC__
   #include <inline/VA_protos.h>
  #endif /* __PPC__ */
 #else /* __GNUC__ */
  #include <pragmas/VA_pragmas.h>
 #endif /* __GNUC__ */
#endif /* __amigaos4__ */

/****************************************************************************/

#endif /* PROTO_VA_H */
