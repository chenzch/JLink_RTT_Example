/*********************************************************************
*                    SEGGER Microcontroller GmbH                     *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*            (c) 1995 - 2021 SEGGER Microcontroller GmbH             *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************
*                                                                    *
*       SEGGER RTT * Real Time Transfer for embedded targets         *
*                                                                    *
**********************************************************************
*                                                                    *
* All rights reserved.                                               *
*                                                                    *
* SEGGER strongly recommends to not make any changes                 *
* to or modify the source code of this software in order to stay     *
* compatible with the RTT protocol and J-Link.                       *
*                                                                    *
* Redistribution and use in source and binary forms, with or         *
* without modification, are permitted provided that the following    *
* condition is met:                                                  *
*                                                                    *
* o Redistributions of source code must retain the above copyright   *
*   notice, this condition and the following disclaimer.             *
*                                                                    *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
* DISCLAIMED. IN NO EVENT SHALL SEGGER Microcontroller BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR           *
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  *
* OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;    *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF      *
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT          *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE  *
* USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH   *
* DAMAGE.                                                            *
*                                                                    *
**********************************************************************
*                                                                    *
*       RTT version: 8.56a                                           *
*                                                                    *
**********************************************************************

---------------------------END-OF-HEADER------------------------------
File    : SEGGER_RTT.c
Purpose : Implementation of SEGGER real-time transfer (RTT) which
          allows real-time communication on targets which support
          debugger memory accesses while the CPU is running.
Revision: $Rev: 29668 $

Additional information:
          Type "int" is assumed to be 32-bits in size
          H->T    Host to target communication
          T->H    Target to host communication

          RTT channel 0 is always present and reserved for Terminal usage.
          Name is fixed to "Terminal"

          Effective buffer size: SizeOfBuffer - 1

          WrOff == RdOff:       Buffer is empty
          WrOff == (RdOff - 1): Buffer is full
          WrOff >  RdOff:       Free space includes wrap-around
          WrOff <  RdOff:       Used space includes wrap-around
          (WrOff == (SizeOfBuffer - 1)) && (RdOff == 0):
                                Buffer full and wrap-around after next byte


----------------------------------------------------------------------
*/

#include "SEGGER_RTT.h"

#include <string.h>                 // for memcpy

/*********************************************************************
*
*       Configuration, default values
*
**********************************************************************
*/

#ifndef   BUFFER_SIZE_UP
  #define BUFFER_SIZE_UP                                  1024  // Size of the buffer for terminal output of target, up to host
#endif

#ifndef   BUFFER_SIZE_DOWN
  #define BUFFER_SIZE_DOWN                                16    // Size of the buffer for terminal input to target from host (Usually keyboard input)
#endif

#ifndef   SEGGER_RTT_MAX_NUM_UP_BUFFERS
  #define SEGGER_RTT_MAX_NUM_UP_BUFFERS                    2    // Number of up-buffers (T->H) available on this target
#endif

#ifndef   SEGGER_RTT_MAX_NUM_DOWN_BUFFERS
  #define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS                  2    // Number of down-buffers (H->T) available on this target
#endif

#ifndef   SEGGER_RTT_MODE_DEFAULT
  #define SEGGER_RTT_MODE_DEFAULT                         SEGGER_RTT_MODE_NO_BLOCK_SKIP
#endif

#ifndef   SEGGER_RTT_LOCK
  #define SEGGER_RTT_LOCK()
#endif

#ifndef   SEGGER_RTT_UNLOCK
  #define SEGGER_RTT_UNLOCK()
#endif

#ifndef   STRLEN
  #define STRLEN(a)                                       strlen((a))
#endif

#ifndef   STRCPY
  #define STRCPY(pDest, pSrc)                             strcpy((pDest), (pSrc))
#endif

#ifndef   SEGGER_RTT_MEMCPY_USE_BYTELOOP
  #define SEGGER_RTT_MEMCPY_USE_BYTELOOP                  0
#endif

#ifndef   SEGGER_RTT_MEMCPY
  #ifdef  MEMCPY
    #define SEGGER_RTT_MEMCPY(pDest, pSrc, NumBytes)      MEMCPY((pDest), (pSrc), (NumBytes))
  #else
    #define SEGGER_RTT_MEMCPY(pDest, pSrc, NumBytes)      memcpy((pDest), (pSrc), (NumBytes))
  #endif
#endif

#ifndef   MIN
  #define MIN(a, b)                                       (((a) < (b)) ? (a) : (b))
#endif

#ifndef   MAX
  #define MAX(a, b)                                       (((a) > (b)) ? (a) : (b))
#endif

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************/
// ".jlink_rtt_buffer"
#define SEGGER_RTT_BUFFER_SECTION ".dtcm_bss"

#if (defined __ICCARM__) || (defined __ICCRX__)
  #define RTT_PRAGMA(P) _Pragma(#P)
#endif

#if defined (SEGGER_RTT_BUFFER_SECTION)
  #if ((defined __GNUC__) || (defined __clang__))
    #define SEGGER_RTT_PUT_SECTION(Var, Section) __attribute__ ((section (Section))) Var
  #elif (defined __ICCARM__) || (defined __ICCRX__)
#define SEGGER_RTT_PUT_SECTION(Var, Section) RTT_PRAGMA(location=Section) \
                                        Var
  #elif (defined __CC_ARM)
    #define SEGGER_RTT_PUT_SECTION(Var, Section) __attribute__ ((section (Section)))  Var
  #else
    #error "Section placement not supported for this compiler."
  #endif
#else
  #define SEGGER_RTT_PUT_SECTION(Var, Section) Var
#endif

#if defined(SEGGER_RTT_BUFFER_SECTION)
  #define SEGGER_RTT_PUT_BUFFER_SECTION(Var) SEGGER_RTT_PUT_SECTION(Var, SEGGER_RTT_BUFFER_SECTION)
#else
  #define SEGGER_RTT_PUT_BUFFER_SECTION(Var) Var
#endif

/*********************************************************************
*
*       Static data
*
**********************************************************************/

//
// RTT Control Block and allocate buffers for channel 0
//
#pragma GCC section text ".itcm_text"

SEGGER_RTT_PUT_BUFFER_SECTION(SEGGER_RTT_CB _SEGGER_RTT);
SEGGER_RTT_PUT_BUFFER_SECTION(static char _acUpBuffer  [BUFFER_SIZE_UP]);
SEGGER_RTT_PUT_BUFFER_SECTION(static char _acDownBuffer[BUFFER_SIZE_DOWN]);


/*********************************************************************
*
*       Static functions
*
**********************************************************************
*/

#if (SEGGER_RTT_MODE_DEFAULT == SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL)
/*********************************************************************
*
*       _WriteBlocking()
*
*  Function description
*    Stores a specified number of characters in SEGGER RTT ring buffer
*    and updates the associated write pointer which is periodically
*    read by the host.
*    The caller is responsible for managing the write chunk sizes as
*    _WriteBlocking() will block until all data has been posted successfully.
*
*  Parameters
*    pRing        Ring buffer to post to.
*    pBuffer      Pointer to character array. Does not need to point to a \0 terminated string.
*    NumBytes     Number of bytes to be stored in the SEGGER RTT control block.
*
*  Return value
*    >= 0 - Number of bytes written into buffer.
*/
static unsigned _WriteBlocking(SEGGER_RTT_BUFFER_UP* pRing, const char* pBuffer, unsigned NumBytes) {
  unsigned NumBytesToWrite;
  unsigned NumBytesWritten;
  unsigned RdOff;
  unsigned WrOff;
  volatile char* pDst;
  //
  // Write data to buffer and handle wrap-around if necessary
  //
  NumBytesWritten = 0u;
  WrOff = pRing->WrOff;
  do {
    RdOff = pRing->RdOff;                         // May be changed by host (debug probe) in the meantime
    if (RdOff > WrOff) {
      NumBytesToWrite = RdOff - WrOff - 1u;
    } else {
      NumBytesToWrite = pRing->SizeOfBuffer - (WrOff - RdOff + 1u);
    }
    NumBytesToWrite = MIN(NumBytesToWrite, (pRing->SizeOfBuffer - WrOff));      // Number of bytes that can be written until buffer wrap-around
    NumBytesToWrite = MIN(NumBytesToWrite, NumBytes);
    pDst = (pRing->pBuffer + WrOff) - SEGGER_RTT_OFFSET;
#if SEGGER_RTT_MEMCPY_USE_BYTELOOP
    NumBytesWritten += NumBytesToWrite;
    NumBytes        -= NumBytesToWrite;
    WrOff           += NumBytesToWrite;
    while (NumBytesToWrite--) {
      *pDst++ = *pBuffer++;
    };
#else
    SEGGER_RTT_MEMCPY((void*)pDst, pBuffer, NumBytesToWrite);
    NumBytesWritten += NumBytesToWrite;
    pBuffer         += NumBytesToWrite;
    NumBytes        -= NumBytesToWrite;
    WrOff           += NumBytesToWrite;
#endif
    if (WrOff == pRing->SizeOfBuffer) {
      WrOff = 0u;
    }
    RTT__DMB();                     // Force data write to be complete before writing the <WrOff>, in case CPU is allowed to change the order of memory accesses
    pRing->WrOff = WrOff;
  } while (NumBytes);
  return NumBytesWritten;
}
#endif

#if ((SEGGER_RTT_MODE_DEFAULT == SEGGER_RTT_MODE_NO_BLOCK_SKIP) || (SEGGER_RTT_MODE_DEFAULT == SEGGER_RTT_MODE_NO_BLOCK_TRIM))

/*********************************************************************
*
*       _WriteNoCheck()
*
*  Function description
*    Stores a specified number of characters in SEGGER RTT ring buffer
*    and updates the associated write pointer which is periodically
*    read by the host.
*    It is callers responsibility to make sure data actually fits in buffer.
*
*  Parameters
*    pRing        Ring buffer to post to.
*    pBuffer      Pointer to character array. Does not need to point to a \0 terminated string.
*    NumBytes     Number of bytes to be stored in the SEGGER RTT control block.
*
*  Notes
*    (1) If there might not be enough space in the "Up"-buffer, call _WriteBlocking
*/
static void _WriteNoCheck(SEGGER_RTT_BUFFER_UP* pRing, const char* pData, unsigned NumBytes) {
  unsigned NumBytesAtOnce;
  unsigned WrOff;
  unsigned Rem;
  volatile char* pDst;

  WrOff = pRing->WrOff;
  Rem = pRing->SizeOfBuffer - WrOff;
  if (Rem > NumBytes) {
    //
    // All data fits before wrap around
    //
    pDst = (pRing->pBuffer + WrOff) - SEGGER_RTT_OFFSET;
#if SEGGER_RTT_MEMCPY_USE_BYTELOOP
    WrOff += NumBytes;
    while (NumBytes--) {
      *pDst++ = *pData++;
    };
    RTT__DMB();                     // Force data write to be complete before writing the <WrOff>, in case CPU is allowed to change the order of memory accesses
    pRing->WrOff = WrOff;
#else
    SEGGER_RTT_MEMCPY((void*)pDst, pData, NumBytes);
    RTT__DMB();                     // Force data write to be complete before writing the <WrOff>, in case CPU is allowed to change the order of memory accesses
    pRing->WrOff = WrOff + NumBytes;
#endif
  } else {
    //
    // We reach the end of the buffer, so need to wrap around
    //
#if SEGGER_RTT_MEMCPY_USE_BYTELOOP
    pDst = (pRing->pBuffer + WrOff) - SEGGER_RTT_OFFSET;
    NumBytesAtOnce = Rem;
    while (NumBytesAtOnce--) {
      *pDst++ = *pData++;
    };
    pDst = pRing->pBuffer - SEGGER_RTT_OFFSET;
    NumBytesAtOnce = NumBytes - Rem;
    while (NumBytesAtOnce--) {
      *pDst++ = *pData++;
    };
    RTT__DMB();                     // Force data write to be complete before writing the <WrOff>, in case CPU is allowed to change the order of memory accesses
    pRing->WrOff = NumBytes - Rem;
#else
    NumBytesAtOnce = Rem;
    pDst = (pRing->pBuffer + WrOff) - SEGGER_RTT_OFFSET;
    SEGGER_RTT_MEMCPY((void*)pDst, pData, NumBytesAtOnce);
    NumBytesAtOnce = NumBytes - Rem;
    pDst = pRing->pBuffer - SEGGER_RTT_OFFSET;
    SEGGER_RTT_MEMCPY((void*)pDst, pData + Rem, NumBytesAtOnce);
    RTT__DMB();                     // Force data write to be complete before writing the <WrOff>, in case CPU is allowed to change the order of memory accesses
    pRing->WrOff = NumBytesAtOnce;
#endif
  }
}

/*********************************************************************
*
*       _GetAvailWriteSpace()
*
*  Function description
*    Returns the number of bytes that can be written to the ring
*    buffer without blocking.
*
*  Parameters
*    pRing        Ring buffer to check.
*
*  Return value
*    Number of bytes that are free in the buffer.
*/
static unsigned _GetAvailWriteSpace(SEGGER_RTT_BUFFER_UP* pRing) {
  unsigned RdOff;
  unsigned WrOff;
  unsigned r;
  //
  // Avoid warnings regarding volatile access order.  It's not a problem
  // in this case, but dampen compiler enthusiasm.
  //
  RdOff = pRing->RdOff;
  WrOff = pRing->WrOff;
  if (RdOff <= WrOff) {
    r = pRing->SizeOfBuffer - 1u - WrOff + RdOff;
  } else {
    r = RdOff - WrOff - 1u;
  }
  return r;
}

#endif

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/
/*********************************************************************
*
*       SEGGER_RTT_WriteNoLock
*
*  Function description
*    Stores a specified number of characters in SEGGER RTT
*    control block which is then read by the host.
*    SEGGER_RTT_WriteNoLock does not lock the application.
*
*  Parameters
*    pBuffer      Pointer to character array. Does not need to point to a \0 terminated string.
*    NumBytes     Number of bytes to be stored in the SEGGER RTT control block.
*
*  Return value
*    Number of bytes which have been stored in the "Up"-buffer.
*
*  Notes
*    (1) Data is stored according to buffer flags.
*    (2) For performance reasons this function does not call Init()
*        and may only be called after RTT has been initialized.
*        Either by calling SEGGER_RTT_Init() or calling another RTT API function first.
*/
unsigned SEGGER_RTT_WriteNoLock(const void* pBuffer, unsigned NumBytes) {
#if ((SEGGER_RTT_MODE_DEFAULT == SEGGER_RTT_MODE_NO_BLOCK_SKIP) || (SEGGER_RTT_MODE_DEFAULT == SEGGER_RTT_MODE_NO_BLOCK_TRIM))
  unsigned              Avail;
#endif
  unsigned              Status;
  const char*           pData;
  SEGGER_RTT_BUFFER_UP* pRing;
  //
  // Get "to-host" ring buffer.
  //
  pData = (const char *)pBuffer;
  pRing = (SEGGER_RTT_BUFFER_UP*)((uintptr_t)&_SEGGER_RTT.aUp[0]);  // Access uncached to make sure we see changes made by the J-Link side and all of our changes go into HW directly
  //
  // How we output depends upon the mode...
  //
#if (SEGGER_RTT_MODE_DEFAULT == SEGGER_RTT_MODE_NO_BLOCK_SKIP)
  //
  // If we are in skip mode and there is no space for the whole
  // of this output, don't bother.
  //
  Avail = _GetAvailWriteSpace(pRing);
  if (Avail < NumBytes) {
    Status = 0u;
  } else {
    Status = NumBytes;
    _WriteNoCheck(pRing, pData, NumBytes);
  }
#elif (SEGGER_RTT_MODE_DEFAULT == SEGGER_RTT_MODE_NO_BLOCK_TRIM)
  //
  // If we are in trim mode, trim to what we can output without blocking.
  //
  Avail = _GetAvailWriteSpace(pRing);
  Status = Avail < NumBytes ? Avail : NumBytes;
  _WriteNoCheck(pRing, pData, Status);
#elif (SEGGER_RTT_MODE_DEFAULT == SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL)
  //
  // If we are in blocking mode, output everything.
  //
  Status = _WriteBlocking(pRing, pData, NumBytes);
#else
  Status = 0u;
#endif
  //
  // Finish up.
  //
  return Status;
}

/*********************************************************************
*
*       SEGGER_RTT_Write
*
*  Function description
*    Stores a specified number of characters in SEGGER RTT
*    control block which is then read by the host.
*
*  Parameters
*    pBuffer      Pointer to character array. Does not need to point to a \0 terminated string.
*    NumBytes     Number of bytes to be stored in the SEGGER RTT control block.
*
*  Return value
*    Number of bytes which have been stored in the "Up"-buffer.
*
*  Notes
*    (1) Data is stored according to buffer flags.
*/
unsigned SEGGER_RTT_Write(const void* pBuffer, unsigned NumBytes) {
  unsigned Status;

  SEGGER_RTT_LOCK();
  Status = SEGGER_RTT_WriteNoLock(pBuffer, NumBytes);  // Call the non-locking write function
  SEGGER_RTT_UNLOCK();
  return Status;
}

/*********************************************************************
*
*       SEGGER_RTT_ConfigUpBuffer
*
*  Function description
*    Run-time configuration of a specific up-buffer (T->H).
*    Buffer to be configured is specified by index.
*    This includes: Buffer address, size, name, flags, ...
*
*  Parameters
*
*  Return value
*
*  Additional information
*    Buffer 0 is configured on compile-time.
*/
void SEGGER_RTT_ConfigUpBuffer() {
	volatile SEGGER_RTT_CB* p;   // Volatile to make sure that compiler cannot change the order of accesses to the control block
	//
	// Initialize control block
	//
	p                    = (volatile SEGGER_RTT_CB*)((uintptr_t)&_SEGGER_RTT);  // Access control block uncached so that nothing in the cache ever becomes dirty and all changes are visible in HW directly
	memset((SEGGER_RTT_CB*)p, 0, sizeof(_SEGGER_RTT));         // Make sure that the RTT CB is always zero initialized.
	p->MaxNumUpBuffers   = SEGGER_RTT_MAX_NUM_UP_BUFFERS;
	p->MaxNumDownBuffers = SEGGER_RTT_MAX_NUM_DOWN_BUFFERS;
	//
	// Initialize up buffer 0
	//
	p->aUp[0].sName         = "Terminal";
	p->aUp[0].pBuffer       = &_acUpBuffer[SEGGER_RTT_OFFSET];
	p->aUp[0].SizeOfBuffer  = BUFFER_SIZE_UP;
	p->aUp[0].RdOff         = 0u;
	p->aUp[0].WrOff         = 0u;
	p->aUp[0].Flags         = SEGGER_RTT_MODE_DEFAULT;
	//
	// Initialize down buffer 0
	//
	p->aDown[0].sName         = p->aUp[0].sName;
	p->aDown[0].pBuffer       = &_acDownBuffer[SEGGER_RTT_OFFSET];
	p->aDown[0].SizeOfBuffer  = BUFFER_SIZE_DOWN;
	p->aDown[0].RdOff         = 0u;
	p->aDown[0].WrOff         = 0u;
	p->aDown[0].Flags         = SEGGER_RTT_MODE_DEFAULT;
	//
	// Finish initialization of the control block.
	// Copy Id string backwards to make sure that "SEGGER RTT" is not found in initializer memory (usually flash),
	// as this would cause J-Link to "find" the control block at a wrong address.
	//
	RTT__DMB();                       // Force order of memory accesses for cores that may perform out-of-order memory accesses
	strcpy((char *)&p->acID[0], "SEGGER RTT");
	RTT__DMB();                       // Force order of memory accesses for cores that may perform out-of-order memory accesses
}

#pragma GCC section text 
/*************************** End of file ****************************/
