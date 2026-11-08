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
int beam_etf_decode_ref(const char *buf, int *index, BeamEtfRef *p)
{
  const char *s = buf + *index;
  const char *s0 = s;
  int count, i;
  const char tag = get8(s);

  switch (tag) {
    case BEAM_ETF_REFERENCE_EXT:
      if (p) {
	  if (get_atom(&s, p->node, NULL) < 0) return -1;
	  p->n[0] = get32be(s);
	  p->len = 1;
	  p->creation = get8(s) & 0x03;
      }
      else {
	  if (get_atom(&s, NULL, NULL) < 0) return -1;
	  s += 5;
      }

      *index += s-s0;

      return 0;
      break;

  case BEAM_ETF_NEW_REFERENCE_EXT:
  case BEAM_ETF_NEWER_REFERENCE_EXT:

      /* first the integer count */
      count = get16be(s);

      if (count > sizeof(p->n)/sizeof(p->n[0]))
          return -1; /* Not enough space in struct... */

      if (p) {
	  p->len = count;
	  if (get_atom(&s, p->node, NULL) < 0) return -1;
          if (tag == BEAM_ETF_NEW_REFERENCE_EXT)
              p->creation = get8(s) & 0x03;
          else
              p->creation = get32be(s);
      }
      else {
	  if (get_atom(&s, NULL, NULL) < 0) return -1;
	  s += (tag == BEAM_ETF_NEW_REFERENCE_EXT ? 1 : 4);
      }

      /* finally the id integers */
      if (p) {
        for (i = 0; i < count && i < sizeof(p->n)/sizeof(p->n[0]); i++) {
	  p->n[i] = get32be(s);
	}
        for (; i < sizeof(p->n)/sizeof(p->n[0]); i++)
          p->n[i] = 0;
      }
      else s += 4 * count;

      *index += s-s0;

      return 0;
      break;

    default:
      return -1;
  }
}

