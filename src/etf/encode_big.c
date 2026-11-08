/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2009-2025. All Rights Reserved.
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
int beam_etf_internal_encode_big(char *buf, int *index, BeamEtfCompatBig* big) {
    unsigned char *s = (unsigned char *)buf + *index;
    unsigned char *s0 = s;
    unsigned int digit_bytes = big->arity;
    unsigned int n = (digit_bytes+1)/2;

    if (digit_bytes < 256) {
	if (buf) {
	    put8(s, BEAM_ETF_SMALL_BIG_EXT);
	    put8(s, digit_bytes);
	} else {
	    s += 2;
	}
    } else {
	if (buf) {
	    put8(s, BEAM_ETF_LARGE_BIG_EXT);
	    put32be(s, digit_bytes);
	} else {
	    s += 5;
	}
    }

    if (buf) {
	int i;
	unsigned char hi, lo;
	unsigned short *dt = big->digits;
	put8(s, big->is_neg);

	for (i = 0; i < n; ++i) {
	    
	    hi = (unsigned char) (dt[i] >> 8);
	    lo = (unsigned char) (dt[i]);

	    s[i*2] = lo;
	    if ((i*2 + 1) < digit_bytes) {
		s[i*2 + 1] = hi;
	    }
	}

    } else {
	s ++; /* skip sign bit */
    }

    s += digit_bytes;

    *index += s-s0;

    return 0;
}

