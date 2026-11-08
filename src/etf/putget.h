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
 *

 */
#ifndef _PUTGET_H
#define _PUTGET_H

#define put8(s,n) do { \
  (s)[0] = (char)((n) & 0xff); \
  (s) += 1; \
} while (0) 
	    
#define put16le(s,n) do { \
  (s)[0] = (n) & 0xff;  \
  (s)[1] = ((n) >>  8) & 0xff; \
  (s) += 2; \
} while (0) \
     
#define put32le(s,n) do { \
  (s)[0] = (n) & 0xff;  \
  (s)[1] = ((n) >>  8) & 0xff; \
  (s)[2] = ((n) >>  16) & 0xff; \
  (s)[3] = ((n) >>  24) & 0xff; \
  (s) += 4; \
} while (0)
	    
#define put16be(s,n) do { \
  (s)[0] = ((n) >>  8) & 0xff; \
  (s)[1] = (n) & 0xff; \
  (s) += 2; \
} while (0)
     
#define put32be(s,n) do {  \
  (s)[0] = ((n) >>  24) & 0xff; \
  (s)[1] = ((n) >>  16) & 0xff; \
  (s)[2] = ((n) >>  8) & 0xff;  \
  (s)[3] = (n) & 0xff; \
  (s) += 4; \
} while (0)

#define put64be(s,n) do {  \
  (s)[0] = ((n) >>  56) & 0xff; \
  (s)[1] = ((n) >>  48) & 0xff; \
  (s)[2] = ((n) >>  40) & 0xff; \
  (s)[3] = ((n) >>  32) & 0xff; \
  (s)[4] = ((n) >>  24) & 0xff; \
  (s)[5] = ((n) >>  16) & 0xff; \
  (s)[6] = ((n) >>  8)  & 0xff; \
  (s)[7] = (n) & 0xff; \
  (s) += 8; \
} while (0)

#define get8(s) \
     ((s) += 1, \
      ((unsigned char *)(s))[-1] & 0xff)
     
#define get16le(s) \
     ((s) += 2, \
      (((((unsigned char *)(s))[-1] << 8) | \
	((unsigned char *)(s))[-2])) & 0xffff)
     
#define get32le(s) \
     ((s) += 4, \
      ((((unsigned char *)(s))[-1] << 24) | \
       (((unsigned char *)(s))[-2] << 16) | \
       (((unsigned char *)(s))[-3] << 8) | \
       ((unsigned char *)(s))[-4]))

#define get16be(s) \
     ((s) += 2, \
      (((((unsigned char *)(s))[-2] << 8) | \
	((unsigned char *)(s))[-1])) & 0xffff) 
     
#define get32be(s) \
     ((s) += 4, \
      ((((unsigned char *)(s))[-4] << 24) | \
       (((unsigned char *)(s))[-3] << 16) | \
       (((unsigned char *)(s))[-2] << 8) | \
       ((unsigned char *)(s))[-1]))
     
#define get64be(s) \
     ((s) += 8, \
      (((BeamEtfULongLong)((unsigned char *)(s))[-8] << 56) | \
       ((BeamEtfULongLong)((unsigned char *)(s))[-7] << 48) | \
       ((BeamEtfULongLong)((unsigned char *)(s))[-6] << 40) | \
       ((BeamEtfULongLong)((unsigned char *)(s))[-5] << 32) | \
       ((BeamEtfULongLong)((unsigned char *)(s))[-4] << 24) | \
       ((BeamEtfULongLong)((unsigned char *)(s))[-3] << 16) | \
       ((BeamEtfULongLong)((unsigned char *)(s))[-2] << 8)  | \
        (BeamEtfULongLong)((unsigned char *)(s))[-1]))

BEAM_ETF_PRIVATE int beam_etf_internal_utf8_to_latin1(char* dst, const char* src, int slen, int destlen, BeamEtfCompatCharEncoding* res_encp);
BEAM_ETF_PRIVATE int beam_etf_internal_latin1_to_utf8(char* dst, const char* src, int slen, int destlen, BeamEtfCompatCharEncoding* res_encp);
BEAM_ETF_PRIVATE int beam_etf_internal_internal_get_atom(const char** bufp, char* p, BeamEtfCompatCharEncoding*);
BEAM_ETF_PRIVATE int beam_etf_internal_internal_put_atom(char** bufp, const char* p, int slen, BeamEtfCompatCharEncoding);
#define get_atom beam_etf_internal_internal_get_atom
#define put_atom beam_etf_internal_internal_put_atom

typedef union float_ext {
    double d;
    BeamEtfULongLong val;
} FloatExt;

#endif /* _PUTGET_H */
