/*****************************************************************************
 * algo_phosphor.c : Phosphor algorithm for the VLC deinterlacer
 *****************************************************************************
 * Copyright (C) 2011 the VideoLAN team
 * $Id$
 *
 * Author: Juha Jeronen <juha.jeronen@jyu.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
#   include "config.h"
#endif

#ifdef CAN_COMPILE_MMXEXT
#   include "mmx.h"
#endif

#include <stdint.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_cpu.h>
#include <vlc_picture.h>
#include <vlc_filter.h>

#include "deinterlace.h" /* filter_sys_t */
#include "helpers.h"     /* ComposeFrame() */

#include "algo_phosphor.h"

/*****************************************************************************
 * Internal functions
 *****************************************************************************/

/**
 * Internal helper function: dims (darkens) the given field
 * of the given picture.
 *
 * This is used for simulating CRT light output decay in RenderPhosphor().
 *
 * The strength "1" is recommended. It's a matter of taste,
 * so it's parametrized.
 *
 * Note on chroma formats:
 *   - If input is 4:2:2, all planes are processed.
 *   - If input is 4:2:0, only the luma plane is processed, because both fields
 *     have the same chroma. This will distort colours, especially for high
 *     filter strengths, especially for pixels whose U and/or V values are
 *     far away from the origin (which is at 128 in uint8 format).
 *
 * @param p_dst Input/output picture. Will be modified in-place.
 * @param i_field Darken which field? 0 = top, 1 = bottom.
 * @param i_strength Strength of effect: 1, 2 or 3 (division by 2, 4 or 8).
 * @see RenderPhosphor()
 * @see ComposeFrame()
 */
static void DarkenField( picture_t *p_dst, const int i_field,
                                           const int i_strength )
{
    assert( p_dst != NULL );
    assert( i_field == 0 || i_field == 1 );
    assert( i_strength >= 1 && i_strength <= 3 );

    /* Bitwise ANDing with this clears the i_strength highest bits
       of each byte */
#ifdef CAN_COMPILE_MMXEXT
    unsigned u_cpu = vlc_CPU();
    uint64_t i_strength_u64 = i_strength; /* for MMX version (needs to know
                                             number of bits) */
#endif
    const uint8_t  remove_high_u8 = 0xFF >> i_strength;
    const uint64_t remove_high_u64 = remove_high_u8 *
                                            INT64_C(0x0101010101010101);

    /* Process luma.

       For luma, the operation is just a shift + bitwise AND, so we vectorize
       even in the C version.

       There is an MMX version, too, because it performs about twice faster.
    */
    int i_plane = Y_PLANE;
    uint8_t *p_out, *p_out_end;
    int w = p_dst->p[i_plane].i_visible_pitch;
    p_out = p_dst->p[i_plane].p_pixels;
    p_out_end = p_out + p_dst->p[i_plane].i_pitch
                      * p_dst->p[i_plane].i_visible_lines;

    /* skip first line for bottom field */
    if( i_field == 1 )
        p_out += p_dst->p[i_plane].i_pitch;

    int wm8 = w % 8;   /* remainder */
    int w8  = w - wm8; /* part of width that is divisible by 8 */
    for( ; p_out < p_out_end ; p_out += 2*p_dst->p[i_plane].i_pitch )
    {
        uint64_t *po = (uint64_t *)p_out;
        int x = 0;

#ifdef CAN_COMPILE_MMXEXT
        if( u_cpu & CPU_CAPABILITY_MMXEXT )
        {
            movq_m2r( i_strength_u64,  mm1 );
            movq_m2r( remove_high_u64, mm2 );
            for( ; x < w8; x += 8 )
            {
                movq_m2r( (*po), mm0 );

                psrlq_r2r( mm1, mm0 );
                pand_r2r(  mm2, mm0 );

                movq_r2m( mm0, (*po++) );
            }
        }
        else
        {
#endif
            for( ; x < w8; x += 8, ++po )
                (*po) = ( ((*po) >> i_strength) & remove_high_u64 );
#ifdef CAN_COMPILE_MMXEXT
        }
#endif

        /* handle the width remainder */
        uint8_t *po_temp = (uint8_t *)po;
        for( ; x < w; ++x, ++po_temp )
            (*po_temp) = ( ((*po_temp) >> i_strength) & remove_high_u8 );
    }

    /* Process chroma if the field chromas are independent.

       The origin (black) is at YUV = (0, 128, 128) in the uint8 format.
       The chroma processing is a bit more complicated than luma,
       and needs MMX for vectorization.
    */
    if( p_dst->format.i_chroma == VLC_CODEC_I422  ||
        p_dst->format.i_chroma == VLC_CODEC_J422 )
    {
        for( i_plane = 0 ; i_plane < p_dst->i_planes ; i_plane++ )
        {
            if( i_plane == Y_PLANE )
                continue; /* luma already handled */

            int w = p_dst->p[i_plane].i_visible_pitch;
#ifdef CAN_COMPILE_MMXEXT
            int wm8 = w % 8;   /* remainder */
            int w8  = w - wm8; /* part of width that is divisible by 8 */
#endif
            p_out = p_dst->p[i_plane].p_pixels;
            p_out_end = p_out + p_dst->p[i_plane].i_pitch
                              * p_dst->p[i_plane].i_visible_lines;

            /* skip first line for bottom field */
            if( i_field == 1 )
                p_out += p_dst->p[i_plane].i_pitch;

            for( ; p_out < p_out_end ; p_out += 2*p_dst->p[i_plane].i_pitch )
            {
                int x = 0;

#ifdef CAN_COMPILE_MMXEXT
                /* See also easy-to-read C version below. */
                if( u_cpu & CPU_CAPABILITY_MMXEXT )
                {
                    static const mmx_t b128 = { .uq = 0x8080808080808080ULL };
                    movq_m2r( b128, mm5 );
                    movq_m2r( i_strength_u64,  mm6 );
                    movq_m2r( remove_high_u64, mm7 );

                    uint64_t *po = (uint64_t *)p_out;
                    for( ; x < w8; x += 8 )
                    {
                        movq_m2r( (*po), mm0 );

                        movq_r2r( mm5, mm2 ); /* 128 */
                        movq_r2r( mm0, mm1 ); /* copy of data */
                        psubusb_r2r( mm2, mm1 ); /* mm1 = max(data - 128, 0) */
                        psubusb_r2r( mm0, mm2 ); /* mm2 = max(128 - data, 0) */

                        /* >> i_strength */
                        psrlq_r2r( mm6, mm1 );
                        psrlq_r2r( mm6, mm2 );
                        pand_r2r(  mm7, mm1 );
                        pand_r2r(  mm7, mm2 );

                        /* collect results from pos./neg. parts */
                        psubb_r2r( mm2, mm1 );
                        paddb_r2r( mm5, mm1 );

                        movq_r2m( mm1, (*po++) );
                    }
                }
#endif

                /* C version - handle the width remainder
                   (or everything if no MMX) */
                uint8_t *po = p_out;
                for( ; x < w; ++x, ++po )
                    (*po) = 128 + ( ((*po) - 128) / (1 << i_strength) );
            } /* for p_out... */
        } /* for i_plane... */
    } /* if b_i422 */

#ifdef CAN_COMPILE_MMXEXT
    if( u_cpu & CPU_CAPABILITY_MMXEXT )
        emms();
#endif
}

/*****************************************************************************
 * Public functions
 *****************************************************************************/

/* See header for function doc. */
int RenderPhosphor( filter_t *p_filter,
                    picture_t *p_dst,
                    int i_order, int i_field )
{
    assert( p_filter != NULL );
    assert( p_dst != NULL );
    assert( i_order >= 0 && i_order <= 2 ); /* 2 = soft field repeat */
    assert( i_field == 0 || i_field == 1 );

    filter_sys_t *p_sys = p_filter->p_sys;

    /* Last two input frames */
    picture_t *p_in  = p_sys->pp_history[HISTORY_SIZE-1];
    picture_t *p_old = p_sys->pp_history[HISTORY_SIZE-2];

    /* Use the same input picture as "old" at the first frame after startup */
    if( !p_old )
        p_old = p_in;

    /* If the history mechanism has failed, we can't do anything. */
    if( !p_in )
        return VLC_EGENERIC;

    assert( p_old != NULL );
    assert( p_in != NULL );

    /* Decide sources for top & bottom fields of output. */
    picture_t *p_in_top    = p_in;
    picture_t *p_in_bottom = p_in;
    /* For the first output field this frame,
       grab "old" field from previous frame. */
    if( i_order == 0 )
    {
        if( i_field == 0 ) /* rendering top field */
            p_in_bottom = p_old;
        else /* i_field == 1, rendering bottom field */
            p_in_top = p_old;
    }

    compose_chroma_t cc = CC_ALTLINE; /* initialize to prevent compiler warning */
    switch( p_sys->phosphor.i_chroma_for_420 )
    {
        case PC_BLEND:
            cc = CC_MERGE;
            break;
        case PC_LATEST:
            if( i_field == 0 )
                cc = CC_SOURCE_TOP;
            else /* i_field == 1 */
                cc = CC_SOURCE_BOTTOM;
            break;
        case PC_ALTLINE:
            cc = CC_ALTLINE;
            break;
        case PC_UPCONVERT:
            cc = CC_UPCONVERT;
            break;
        default:
            /* The above are the only possibilities, if there are no bugs. */
            assert(0);
            break;
    }

    ComposeFrame( p_filter, p_dst, p_in_top, p_in_bottom, cc );

    /* Simulate phosphor light output decay for the old field.

       The dimmer can also be switched off in the configuration, but that is
       more of a technical curiosity or an educational toy for advanced users
       than a useful deinterlacer mode (although it does make telecined
       material look slightly better than without any filtering).

       In most use cases the dimmer is used.
    */
    if( p_sys->phosphor.i_dimmer_strength > 0 )
        DarkenField( p_dst, !i_field, p_sys->phosphor.i_dimmer_strength );

    return VLC_SUCCESS;
}
