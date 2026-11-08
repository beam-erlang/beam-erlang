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

int beam_etf_compat_encode_atom_len_as(char *buf, int *index, const char *p, int len,
    BeamEtfCompatCharEncoding from, BeamEtfCompatCharEncoding to)
{ return beam_etf_internal_encode_atom_len_as(buf, index, p, len, from, to); }

int beam_etf_compat_decode_atom_as(const char *buf, int *index, char *p, int capacity,
    BeamEtfCompatCharEncoding want, BeamEtfCompatCharEncoding *was, BeamEtfCompatCharEncoding *result)
{ return beam_etf_internal_decode_atom_as(buf, index, p, capacity, want, was, result); }

int beam_etf_compat_encode_big(char *buf, int *index, BeamEtfCompatBig *big)
{ return beam_etf_internal_encode_big(buf, index, big); }

int beam_etf_compat_decode_big(const char *buf, int *index, BeamEtfCompatBig *big)
{ return beam_etf_internal_decode_big(buf, index, big); }

int beam_etf_compat_x_fix_buff(BeamEtfBuffer *buffer, int needed, int extra)
{ return beam_etf_internal_x_fix_buff(buffer, needed, extra); }
