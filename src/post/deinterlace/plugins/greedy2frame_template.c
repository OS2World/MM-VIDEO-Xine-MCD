/////////////////////////////////////////////////////////////////////////////
// $Id: greedy2frame_template.c,v 1.1 2003/06/22 17:30:03 miguelfreitas Exp $
/////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2000 John Adcock, Tom Barry, Steve Grimm  All rights reserved.
// port copyright (c) 2003 Miguel Freitas
/////////////////////////////////////////////////////////////////////////////
//
//  This file is subject to the terms of the GNU General Public License as
//  published by the Free Software Foundation.  A copy of this license is
//  included with this software distribution in the file COPYING.  If you
//  do not have a copy, you may obtain a copy by writing to the Free
//  Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details
/////////////////////////////////////////////////////////////////////////////
// CVS Log
//
// $Log: greedy2frame_template.c,v $
// Revision 1.1  2003/06/22 17:30:03  miguelfreitas
// use our own port of greedy2frame (tvtime port is currently broken)
//
// Revision 1.8  2001/11/23 17:18:54  adcockj
// Fixed silly and/or confusion
//
// Revision 1.7  2001/11/22 22:27:00  adcockj
// Bug Fixes
//
// Revision 1.6  2001/11/21 15:21:40  adcockj
// Renamed DEINTERLACE_INFO to TDeinterlaceInfo in line with standards
// Changed TDeinterlaceInfo structure to have history of pictures.
//
// Revision 1.5  2001/07/31 06:48:33  adcockj
// Fixed index bug spotted by Peter Gubanov
//
// Revision 1.4  2001/07/13 16:13:33  adcockj
// Added CVS tags and removed tabs
//
/////////////////////////////////////////////////////////////////////////////

// This is the implementation of the Greedy 2-frame deinterlace algorithm described in
// DI_Greedy2Frame.c.  It's in a separate file so we can compile variants for different
// CPU types; most of the code is the same in the different variants.


///////////////////////////////////////////////////////////////////////////////
// Field 1 | Field 2 | Field 3 | Field 4 |
//   T0    |         |    T1   |         | 
//         |   M0    |         |    M1   | 
//   B0    |         |    B1   |         | 
//


// debugging feature
// output the value of mm4 at this point which is pink where we will weave
// and green were we are going to bob
// uncomment next line to see this
//#define CHECK_BOBWEAVE

#if !defined(MASKS_DEFINED)
#define MASKS_DEFINED
  static const int64_t YMask    = 0x00ff00ff00ff00ff;
  static const int64_t Mask = 0x7f7f7f7f7f7f7f7f;
  static const int64_t DwordOne = 0x0000000100000001;    
  static const int64_t DwordTwo = 0x0000000200000002;    
  static int64_t qwGreedyTwoFrameThreshold;
#endif

#if defined(IS_SSE)
static void DeinterlaceGreedy2Frame_SSE(uint8_t *output, int outstride, 
                                 deinterlace_frame_data_t *data,
                                 int bottom_field, int width, int height )
#elif defined(IS_3DNOW)
static void DeinterlaceGreedy2Frame_3DNOW(uint8_t *output, int outstride,
                                   deinterlace_frame_data_t *data,
                                   int bottom_field, int width, int height )
#else
static void DeinterlaceGreedy2Frame_MMX(uint8_t *output, int outstride,
                                 deinterlace_frame_data_t *data,
                                 int bottom_field, int width, int height )
#endif
{
#ifdef ARCH_X86
    int Line;
    int stride = width * 2;
    register uint8_t* M1;
    register uint8_t* M0;
    register uint8_t* T0;
    register uint8_t* T1;
    register uint8_t* B1;
    register uint8_t* B0;
    uint8_t* Dest = output;
    register uint8_t* Dest2;
    register int count;
    uint32_t Pitch = stride*2;
    uint32_t LineLength = stride;
    uint32_t PitchRest = Pitch - (LineLength >> 3)*8;

    qwGreedyTwoFrameThreshold = GreedyTwoFrameThreshold;
    qwGreedyTwoFrameThreshold += (GreedyTwoFrameThreshold2 << 8);
    qwGreedyTwoFrameThreshold += (qwGreedyTwoFrameThreshold << 48) +
                                (qwGreedyTwoFrameThreshold << 32) + 
                                (qwGreedyTwoFrameThreshold << 16);


    if( bottom_field ) {
        M1 = data->f0 + stride;
        T1 = data->f0;
        B1 = T1 + Pitch;
        M0 = data->f1 + stride;
        T0 = data->f1;
        B0 = T0 + Pitch;
    } else {
        M1 = data->f0 + Pitch;
        T1 = data->f1 + stride;
        B1 = T1 + Pitch;
        M0 = data->f1 + Pitch;
        T0 = data->f2 + stride;
        B0 = T0 + Pitch;

        xine_fast_memcpy(Dest, M1, LineLength);
        Dest += outstride;
    }

    for (Line = 0; Line < (height / 2) - 1; ++Line)
    {
        // Always use the most recent data verbatim.  By definition it's correct (it'd
        // be shown on an interlaced display) and our job is to fill in the spaces
        // between the new lines.
        xine_fast_memcpy(Dest, T1, stride);
        Dest += outstride;
        Dest2 = Dest;

        count = LineLength >> 3;
        do {
          asm volatile(
            // Figure out what to do with the scanline above the one we just copied.
            // See above for a description of the algorithm.

            ".align 8 \n\t"
            "movq Mask, %%mm6			\n\t"

            "movq %0, %%mm1			\n\t"     // T1
            "movq %1, %%mm0			\n\t"     // M1
            "movq %2, %%mm3			\n\t"     // B1
            "movq %3, %%mm2			\n\t"     // M0
            : /* no output */
            : "m" (*T1), "m" (*M1), 
              "m" (*B1), "m" (*M0) );
          

          asm volatile(
            // Figure out what to do with the scanline above the one we just copied.
            // See above for a description of the algorithm.

            // Average T1 and B1 so we can do interpolated bobbing if we bob onto T1.
            "movq %%mm3, %%mm7			\n\t"                   // mm7 = B1

#if defined(IS_SSE)
            "pavgb %%mm1, %%mm7			\n\t"
#elif defined(IS_3DNOW)
            "pavgusb %%mm1, %%mm7			\n\t"
#else

            "movq %%mm1, %%mm5			\n\t"                   // mm5 = T1
            "psrlw $1, %%mm7			\n\t"                    // mm7 = B1 / 2
            "pand %%mm6, %%mm7			\n\t"                   // mask off lower bits
            "psrlw $1, %%mm5			\n\t"                    // mm5 = T1 / 2
            "pand %%mm6, %%mm5			\n\t"                   // mask off lower bits
            "paddw %%mm5, %%mm7			\n\t"                  // mm7 = (T1 + B1) / 2
#endif

            // calculate |M1-M0| put result in mm4 need to keep mm0 intact
            // if we have a good processor then make mm0 the average of M1 and M0
            // which should make weave look better when there is small amounts of
            // movement
#if defined(IS_SSE)
            "movq    %%mm0, %%mm4			\n\t"
            "movq    %%mm2, %%mm5			\n\t"
            "psubusb %%mm2, %%mm4			\n\t"
            "psubusb %%mm0, %%mm5			\n\t"
            "por     %%mm5, %%mm4			\n\t"
            "psrlw   $1, %%mm4			\n\t"
            "pavgb   %%mm2, %%mm0			\n\t"
            "pand    %%mm6, %%mm4			\n\t"
#elif defined(IS_3DNOW)
            "movq    %%mm0, %%mm4			\n\t"
            "movq    %%mm2, %%mm5			\n\t"
            "psubusb %%mm2, %%mm4			\n\t"
            "psubusb %%mm0, %%mm5			\n\t"
            "por     %%mm5, %%mm4			\n\t"
            "psrlw   $1, %%mm4			\n\t"
            "pavgusb %%mm2, %%mm0			\n\t"
            "pand    %%mm6, %%mm4			\n\t"
#else
            "movq    %%mm0, %%mm4			\n\t"
            "psubusb %%mm2, %%mm4			\n\t"
            "psubusb %%mm0, %%mm2			\n\t"
            "por     %%mm2, %%mm4			\n\t"
            "psrlw   $1, %%mm4			\n\t"
            "pand    %%mm6, %%mm4			\n\t"
#endif

            // if |M1-M0| > Threshold we want dword worth of twos
            "pcmpgtb qwGreedyTwoFrameThreshold, %%mm4			\n\t"
            "pand    Mask, %%mm4			\n\t"               // get rid of any sign bit
            "pcmpgtd DwordOne, %%mm4			\n\t"           // do we want to bob
            "pandn   DwordTwo, %%mm4			\n\t"

            "movq    %1, %%mm2			\n\t"     // mm2 = T0

            // calculate |T1-T0| put result in mm5
            "movq    %%mm2, %%mm5			\n\t"
            "psubusb %%mm1, %%mm5			\n\t"
            "psubusb %%mm2, %%mm1			\n\t"
            "por     %%mm1, %%mm5			\n\t"
            "psrlw   $1, %%mm5			\n\t"
            "pand    %%mm6, %%mm5			\n\t"

            // if |T1-T0| > Threshold we want dword worth of ones
            "pcmpgtb qwGreedyTwoFrameThreshold, %%mm5			\n\t"
            "pand    %%mm6, %%mm5			\n\t"                // get rid of any sign bit

            "pcmpgtd DwordOne, %%mm5			\n\t"           
            "pandn   DwordOne, %%mm5			\n\t"
            "paddd   %%mm5, %%mm4			\n\t"

            "movq    %2, %%mm2			\n\t"     // B0

            // calculate |B1-B0| put result in mm5
            "movq    %%mm2, %%mm5			\n\t"
            "psubusb %%mm3, %%mm5			\n\t"
            "psubusb %%mm2, %%mm3			\n\t"
            "por     %%mm3, %%mm5			\n\t"
            "psrlw   $1, %%mm5			\n\t"
            "pand    %%mm6, %%mm5			\n\t"

            // if |B1-B0| > Threshold we want dword worth of ones
            "pcmpgtb qwGreedyTwoFrameThreshold, %%mm5			\n\t"
            "pand    %%mm6, %%mm5			\n\t"                // get rid of any sign bit
            "pcmpgtd DwordOne, %%mm5			\n\t"
            "pandn   DwordOne, %%mm5			\n\t"
            "paddd   %%mm5, %%mm4			\n\t"

            "pcmpgtd DwordTwo, %%mm4			\n\t"

// debugging feature
// output the value of mm4 at this point which is pink where we will weave
// and green were we are going to bob
#ifdef CHECK_BOBWEAVE
#ifdef IS_SSE
            "movntq %%mm4, %0			\n\t"
#else
            "movq %%mm4, %0			\n\t"
#endif
#else

            "movq    %%mm4, %%mm5			\n\t"
             // mm4 now is 1 where we want to weave and 0 where we want to bob
            "pand    %%mm0, %%mm4			\n\t"                
            "pandn   %%mm7, %%mm5			\n\t"                
            "por     %%mm5, %%mm4			\n\t"                
#ifdef IS_SSE
            "movntq %%mm4, %0			\n\t"
#else
            "movq %%mm4, %0			\n\t"
#endif
#endif

          : "=m" (*Dest2)
          : "m" (*T0), "m" (*B0) );

          // Advance to the next set of pixels.
          T1 += 8;
          M1 += 8;
          B1 += 8;
          M0 += 8;
          T0 += 8;
          B0 += 8;
          Dest2 += 8;

        } while( --count );

        Dest += outstride;

        M1 += PitchRest;
        T1 += PitchRest;
        B1 += PitchRest;
        M0 += PitchRest;
        T0 += PitchRest;
        B0 += PitchRest;
    }

#ifdef IS_SSE
    asm("sfence\n\t");
#endif

    if( bottom_field )
    {
        xine_fast_memcpy(Dest, T1, stride);
        Dest += outstride;
        xine_fast_memcpy(Dest, M1, stride);
    }
    else
    {
        xine_fast_memcpy(Dest, T1, stride); 
    }
    
    // clear out the MMX registers ready for doing floating point
    // again
    asm("emms\n\t");
#endif
}

