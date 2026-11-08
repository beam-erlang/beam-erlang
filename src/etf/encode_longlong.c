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

#define abs(p) (((p)<0) ? -(p) : p)

/* long -> erl_integer */
/* note that this is the only place where data is stored Little Endian */

/*
 * For some 64 bit operations on some operating systems code
 * compiled with GNU cc depends on "libgcc" for some 64 bit
 * operations missing in hardware (or because of gcc bugs).
 * If user code was linked together with the ei lib
 * using other linkers than GNU ld this may cause problems.
 * We moved beam_etf_x_encode_longlong() here from "ei_x_encode.c" 
 * to limit this problem to users that actually use the ei
 * longlong operations, not all ei_x users.
 */
int beam_etf_x_encode_longlong(BeamEtfBuffer* x, BeamEtfLongLong n)
{
    int i = x->index;
    beam_etf_encode_longlong(NULL, &i, n);
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_longlong(x->buff, &x->index, n);
}

#ifdef EI_64BIT
int beam_etf_encode_long(char *buf, int *index, long p)
{
    return beam_etf_encode_longlong(buf, index, p);
}
#endif

int beam_etf_encode_longlong(char *buf, int *index, BeamEtfLongLong p)
{
    char *s = buf + *index;
    char *s0 = s;

    if ((p < 256) && (p >= 0)) {
	if (!buf) s += 2;
	else {
	    put8(s,BEAM_ETF_SMALL_INTEGER_EXT);
	    put8(s,(p & 0xff));
	}
    } else if ((p <= ERL_MAX) && (p >= ERL_MIN)) {
	/* FIXME: Non optimal, could use (p <= LONG_MAX) && (p >= LONG_MIN)
	   and skip next case */
	if (!buf) s += 5;
	else {
	    put8(s,BEAM_ETF_INTEGER_EXT);
	    put32be(s,p);
	}
    } else {
	/* We know 28-64 bits needed, i.e four to eight bytes  */
	BeamEtfULongLong up = abs(p); /* FIXME name uabs(x) not to confuse with abs */
	if (buf) {
	    char *arityp;
	    int arity = 0;

	    put8(s,BEAM_ETF_SMALL_BIG_EXT);
	    arityp = s++;	/* fill in later */
	    put8(s, p < 0);            /* save sign separately */
	    while (up) {
		*s++ = up & 0xff; /* take lowest byte */
		up >>= 8;	  /* shift unsigned */
		arity++;
	    }
	    put8(arityp,arity);
	} else {
	    s += 3;		/* Type, arity and sign */
	    while (up) {
		s++;		/* take lowest byte */
		up >>= 8;	/* shift unsigned */
	    }
	}
    }

    *index += s-s0; 

    return 0; 
}

