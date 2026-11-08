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
int beam_etf_encode_ref(char *buf, int *index, const BeamEtfRef *p)
{
  char *s = buf + *index;
  int i;

  (*index) += 1 + 2; /* skip to node atom */
  if (beam_etf_internal_encode_atom_len_as(buf, index, p->node, strlen(p->node), BEAM_ETF_COMPAT_UTF8,
			    BEAM_ETF_COMPAT_LATIN1|BEAM_ETF_COMPAT_UTF8) < 0) {
      return -1;
  }

  /* Always encode as an extended reference; all participating parties
     are now expected to be able to decode extended references. */
  if (buf) {
	  put8(s, BEAM_ETF_NEWER_REFERENCE_EXT);

	  /* first, number of integers */
	  put16be(s, p->len);

	  /* then the nodename */
	  s = buf + *index;

	  /* now the integers */
          put32be(s, p->creation);
	  for (i = 0; i < p->len; i++)
	      put32be(s,p->n[i]);
  }

  *index += p->len*4 + 4;
  return 0;
}

