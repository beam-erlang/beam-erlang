/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 1998-2025. All Rights Reserved.
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
int beam_etf_internal_decode_atom_as(const char *buf, int *index, char* p, int destlen,
		      BeamEtfCompatCharEncoding want_enc,
		      BeamEtfCompatCharEncoding* was_encp,
		      BeamEtfCompatCharEncoding* res_encp)
{
    const char *s = buf + *index;
    const char *s0 = s;
    int len;
    BeamEtfCompatCharEncoding got_enc;

    switch (get8(s)) {
    case BEAM_ETF_ATOM_EXT:
	len = get16be(s);
	got_enc = BEAM_ETF_COMPAT_LATIN1;
	break;
    case BEAM_ETF_SMALL_ATOM_EXT:
	len = get8(s);
	got_enc = BEAM_ETF_COMPAT_LATIN1;
	break;
    case BEAM_ETF_ATOM_UTF8_EXT:
	len = get16be(s);
	got_enc = BEAM_ETF_COMPAT_UTF8;
	break;
    case BEAM_ETF_SMALL_ATOM_UTF8_EXT:
	len = get8(s);
	got_enc = BEAM_ETF_COMPAT_UTF8;
	break;
    default:
	return -1;
    }

    if ((want_enc & got_enc) || want_enc == BEAM_ETF_COMPAT_ASCII) {
	int i, found_non_ascii = 0;
	if (len >= destlen)
	    return -1;
	for (i=0; i<len; i++) {
	    if (s[i] & 0x80) found_non_ascii = 1;
	    if (p) p[i] = s[i];
	}
	if (p) p[len] = 0;
	if (want_enc == BEAM_ETF_COMPAT_ASCII && found_non_ascii) {
	    return -1;
	}
	if (res_encp) {
	    *res_encp = found_non_ascii ? got_enc : BEAM_ETF_COMPAT_ASCII;
	}
    }
    else {
	int plen = (got_enc == BEAM_ETF_COMPAT_LATIN1) ?
	  beam_etf_internal_latin1_to_utf8(p, s, len, destlen-1, res_encp) :
	  beam_etf_internal_utf8_to_latin1(p, s, len, destlen-1, res_encp);
	if (plen < 0) return -1;
	if (p) p[plen] = 0;
    }
    if (was_encp) {
	*was_encp = got_enc;
    }

    s += len;
    *index += s-s0;
    return 0;
}	



#ifdef HAVE_UNALIGNED_WORD_ACCESS

#if SIZEOF_VOID_P == SIZEOF_LONG
typedef unsigned long AsciiWord;
#elif SIZEOF_VOID_P == SIZEOF_LONG_LONG
typedef unsigned long long AsciiWord;
#else
#  error "Unknown word type"
#endif

#if SIZEOF_VOID_P == 4
#  define ASCII_CHECK_MASK ((AsciiWord)0x80808080U)
#elif SIZEOF_VOID_P == 8
#  define ASCII_CHECK_MASK ((AsciiWord)0x8080808080808080U)
#endif

static int ascii_fast_track(char* dst, const char* src, int slen, int destlen)
{
    const AsciiWord* src_word = (AsciiWord*) src;
    const AsciiWord* const src_word_end = src_word + (slen / sizeof(AsciiWord));

    if (destlen < slen)
        return 0;

    if (dst) {
        AsciiWord* dst_word = (AsciiWord*)dst;

        while (src_word < src_word_end) {
            if ((*src_word & ASCII_CHECK_MASK) != 0)
                break;
            *dst_word++ = *src_word++;
        }
    }
    else {
        while (src_word < src_word_end) {
            if ((*src_word & ASCII_CHECK_MASK) != 0)
                break;
            src_word++;
        }
    }
    return (char*)src_word - src;
}
#endif /* HAVE_UNALIGNED_WORD_ACCESS */

int beam_etf_internal_utf8_to_latin1(char* dst, const char* src, int slen, int destlen,
		   BeamEtfCompatCharEncoding* res_encp)
{
    const char* const dst_start = dst;
    const char* const dst_end = dst + destlen;
    int found_non_ascii = 0;

#ifdef HAVE_UNALIGNED_WORD_ACCESS
    {
        int aft = ascii_fast_track(dst, src, slen, destlen);
        src += aft;
        slen -= aft;
        dst += aft;
    }
#endif

    while (slen > 0) {
	if (dst >= dst_end) return -1;
	if ((src[0] & 0x80) == 0) {
	    if (dst_start) {
		*dst = *src;
	    }
	    ++dst;
	    ++src;
	    --slen;
	}
	else if (slen > 1 &&
		 (src[0] & 0xFE) == 0xC2 &&
		 (src[1] & 0xC0) == 0x80) {
	    if (dst_start) {
		*dst = (char) ((src[0] << 6) | (src[1] & 0x3F));
	    }
	    ++dst;
	    src += 2;
	    slen -= 2;
	    found_non_ascii = 1;
	}
	else return -1;
    }
    if (res_encp) {
	*res_encp = found_non_ascii ? BEAM_ETF_COMPAT_LATIN1 : BEAM_ETF_COMPAT_ASCII;
    }
    return dst - dst_start;
}

int beam_etf_internal_latin1_to_utf8(char* dst, const char* src, int slen, int destlen,
		   BeamEtfCompatCharEncoding* res_encp)
{
    const char* const src_end = src + slen;
    const char* const dst_start = dst;
    const char* const dst_end = dst + destlen;
    int found_non_ascii = 0;

#ifdef HAVE_UNALIGNED_WORD_ACCESS
    {
        int aft = ascii_fast_track(dst, src, slen, destlen);
        dst += aft;
        src += aft;
    }
#endif

    while (src < src_end) {
	if (dst >= dst_end) return -1;
	if ((src[0] & 0x80) == 0) {
	    if (dst_start) {
		*dst = *src;
	    }
	    ++dst;
	}
	else {
	    if (dst_start) {
		unsigned char ch = *src;
		dst[0] = 0xC0 | (ch >> 6);
		dst[1] = 0x80 | (ch & 0x3F);
	    }
	    dst += 2;
	    found_non_ascii = 1;
	}
	++src;
    }
    if (res_encp) {
	*res_encp = found_non_ascii ? BEAM_ETF_COMPAT_UTF8 : BEAM_ETF_COMPAT_ASCII;
    }
    return dst - dst_start;
}



int beam_etf_internal_internal_get_atom(const char** bufp, char* p,
			 BeamEtfCompatCharEncoding* was_encp)
{
    int ix = 0;
    if (beam_etf_internal_decode_atom_as(*bufp, &ix, p, BEAM_ETF_MAXATOMLEN_UTF8, BEAM_ETF_COMPAT_UTF8, was_encp, NULL) < 0)
	return -1;
    *bufp += ix;
    return 0;
}


