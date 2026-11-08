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
int beam_etf_decode_pid(const char *buf, int *index, BeamEtfPid *p)
{
  const char *s = buf + *index;
  const char *s0 = s;
  const char tag = get8(s);

  if (tag != BEAM_ETF_PID_EXT && tag != BEAM_ETF_NEW_PID_EXT) return -1;

  if (p) {
    if (get_atom(&s, p->node, NULL) < 0) return -1;
    p->num = get32be(s);
    p->serial = get32be(s);
    if (tag == BEAM_ETF_PID_EXT)
        p->creation = get8(s) & 0x03; /* 2 bits */
    else
        p->creation = get32be(s); /* 32 bits */
  }
  else {
      if (get_atom(&s, NULL, NULL) < 0) return -1;
      s+= (tag == BEAM_ETF_PID_EXT ? 9 : 12);
  }

  *index += s-s0;

  return 0;
}
