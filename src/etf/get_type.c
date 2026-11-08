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
#include "etf_internal.h"
#include "putget.h"

/* report type identifier from the start of the buffer */
/* for types with meaningful length attributes, return the length too.
   In other cases, return length 0 */

int beam_etf_get_type(const char *buf, const int *index, int *type, int *len)
{
  const char *s = buf + *index;

  *type = get8(s);
  *len = 0;
  
  switch (*type) {
  case BEAM_ETF_SMALL_ATOM_EXT:
  case BEAM_ETF_SMALL_ATOM_UTF8_EXT:
    *type = BEAM_ETF_ATOM_EXT;
     EI_FALLTHROUGH();
  case BEAM_ETF_SMALL_TUPLE_EXT:
    *len = get8(s);
    break;

  case BEAM_ETF_ATOM_UTF8_EXT:
    *type = BEAM_ETF_ATOM_EXT;
    EI_FALLTHROUGH();
  case BEAM_ETF_ATOM_EXT:
  case BEAM_ETF_STRING_EXT:
    *len = get16be(s);
    break;

  case BEAM_ETF_FLOAT_EXT:
  case BEAM_ETF_NEW_FLOAT_EXT:
    *type = BEAM_ETF_FLOAT_EXT;
    break;

  case BEAM_ETF_LARGE_TUPLE_EXT:
  case BEAM_ETF_LIST_EXT:
  case BEAM_ETF_MAP_EXT:
  case BEAM_ETF_BINARY_EXT:
  case BEAM_ETF_BIT_BINARY_EXT:
    *len = get32be(s);
    break;

  case BEAM_ETF_SMALL_BIG_EXT:
    *len = get8(s); /* #digit_bytes */
    break;

  case BEAM_ETF_LARGE_BIG_EXT:
    *len = get32be(s); /* #digit_bytes */
    break;

  case BEAM_ETF_NEW_PID_EXT:
      *type = BEAM_ETF_PID_EXT;
      break;
  case BEAM_ETF_V4_PORT_EXT:
  case BEAM_ETF_NEW_PORT_EXT:
      *type = BEAM_ETF_PORT_EXT;
      break;
  case BEAM_ETF_NEWER_REFERENCE_EXT:
      *type = BEAM_ETF_NEW_REFERENCE_EXT;
      break;
  }

  /* leave index unchanged */
  return 0;
}


