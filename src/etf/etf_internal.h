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
#ifndef BEAM_ETF_INTERNAL_H
#define BEAM_ETF_INTERNAL_H
#include "namespace.h"
#include <beam/sdk/etf.h>
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#define ERL_MAX ((1 << 27)-1)
#define ERL_MIN -(1 << 27)
#define EI_FALLTHROUGH() ((void)0)
#define EI_UNDEF(V,I) V = I
#define ASSERT(Cnd) ((void)0)
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef signed char int8;
typedef signed short int16;
typedef signed int int32;
#if defined(__GNUC__)
#define BEAM_ETF_PRIVATE __attribute__((visibility("hidden")))
#else
#define BEAM_ETF_PRIVATE
#endif
BEAM_ETF_PRIVATE int beam_etf_internal_encode_atom_len_as(char *buf, int *index, const char *p, int len,
    BeamEtfCompatCharEncoding from, BeamEtfCompatCharEncoding to);
BEAM_ETF_PRIVATE int beam_etf_internal_decode_atom_as(const char *buf, int *index, char *p, int capacity,
    BeamEtfCompatCharEncoding want, BeamEtfCompatCharEncoding *was, BeamEtfCompatCharEncoding *result);
BEAM_ETF_PRIVATE int beam_etf_internal_encode_big(char *buf, int *index, BeamEtfCompatBig *big);
BEAM_ETF_PRIVATE int beam_etf_internal_decode_big(const char *buf, int *index, BeamEtfCompatBig *big);
BEAM_ETF_PRIVATE int beam_etf_internal_x_fix_buff(BeamEtfBuffer *buffer, int needed, int extra);

#endif
