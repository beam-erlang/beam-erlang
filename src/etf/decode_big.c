/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2002-2025. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */
#include "etf_internal.h"
#include "putget.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

int beam_etf_internal_decode_big(const char *buf, int *index, BeamEtfCompatBig *b) {
    unsigned int digit_bytes;
    const unsigned char *s = (unsigned char*) buf + *index;
    const unsigned char *s0 = s;

    switch ( get8(s) ) {
	case BEAM_ETF_SMALL_BIG_EXT:
	digit_bytes = get8(s);
	break;
    case BEAM_ETF_LARGE_BIG_EXT:
	digit_bytes = get32be(s);
	break;
    default:
	return -1;
    }
    if ( b ) {
	unsigned short *dt = b->digits;
	unsigned int n = (digit_bytes+1)/2;
	int i;

	if ( digit_bytes != b->arity ) {
	    return -1;
	}

	b->is_neg = get8(s);
	  
	for (i = 0; i < n; ++i) {
	    dt[i] = s[i*2];
	    if ((i*2 + 1) < digit_bytes) {
		dt[i] |= ((unsigned short) s[(i*2)+1]) << 8;
	    }
	}
    } else {
	s++; /* skip sign byte */
    }

    s += digit_bytes;

    *index += s-s0; 

    return 0; 
}

