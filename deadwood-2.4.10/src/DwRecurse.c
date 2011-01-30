/* Copyright (c) 2009 Sam Trenholme
 *
 * TERMS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * This software is provided 'as is' with no guarantees of correctness or
 * fitness for purpose.
 */

/* DwRecurse.c: Used for functions and framework so Deadwood has full
 * recursive DNS support */

#include "DwStr.h"
#include "DwStr_functions.h"
#include "DwDnsStr.h"
#include "DwHash.h"
#include "DwMararc.h"
extern int32_t key_n[];

/* See if two strings pointing to dname objects are the
 * same.  p1: pointer to the first string; p2: pointer to the second
 * string; pmax: Maximum possible value for either string
 * 1 if they are, 0 if they are not, -1 on error */

int dwx_dname_issame(uint8_t *p1, uint8_t *p2, uint8_t *pmax1,
                uint8_t *pmax2) {
        int len = 0, counter = 0;

        if(*p1 != *p2) {
                return 0;
        }

        for(counter = 0; counter < 260; counter++) {
                len = *p1;
                if(len > 63 || len < 0) {
                        return -1; /* Invalid length */
                }
                if(len == 0) {
                        return 1; /* Same */
                }

                for(;len >= 0 && counter < 260; len--) {
                        p1++;
                        p2++;
                        counter++;
                        if(p1 > pmax1 || p2 > pmax2) {
                                return -1;
                        }
                        if(*p1 != *p2) {
                                return 0;
                        }
                }
        }
        return -1;
}

/* See if two domain names embedded in a Deadwood string object are the
 * same.  1 if they are, 0 if they are not, -1 on error */
int dwx_dname_issame_dw(dw_str *in, uint32_t offset1, uint32_t offset2) {
        uint8_t *p1 = 0, *p2 = 0, *pmax = 0;

        if(dw_assert_sanity(in) == -1 ||
                        offset1 > in->len || offset2 > in->len) {
                return -1;
        }

        p1 = in->str + offset1;
        p2 = in->str + offset2;
        pmax = in->str + in->len;

        return dwx_dname_issame(p1,p2,pmax,pmax);

}

/* See if two domain name objects embedded in different Deadwood string
 * objects are the same.  1 if they are, 0 if they are not, -1 on error */
int dwx_dname_issame_2dw(dw_str *in1, uint32_t offset1, dw_str *in2,
                        uint32_t offset2) {
        uint8_t *p1 = 0, *p2 = 0, *pmax1 = 0, *pmax2 = 0;

        if(dw_assert_sanity(in1) == -1 || dw_assert_sanity(in2) == -1 ||
                        offset1 > in1->len || offset2 > in2->len) {
                return -1;
        }

        p1 = in1->str + offset1;
        p2 = in2->str + offset2;
        pmax1 = in1->str + in1->len;
        pmax2 = in2->str + in2->len;

        return dwx_dname_issame(p1,p2,pmax1,pmax2);

}

/* A complete DNS response is one where:
 *
 * * If the only response is in the NS area, the response is a SOA response
 *   (not there)
 *
 * * If there is an AN area, either the first response is not a CNAME, or
 *   if it is a CNAME, there is a subsequent response that is not a CNAME,
 *   as long as it is a proper CNAME chain.  Note that this is a complete
 *   record if they asked for a CNAME.
 *
 * An incomplete DNS response is one where:
 *
 * * There is a NS section with NS referrals.  Sometimes, the AR
 *   section will have A and AAAA records for the NS records (glue).
 *
 * * In the AN area there is a CNAME record, but no (what they asked for)
 *   record, or if there is, it's not the same name they asked for.
 *
 */

/* */
