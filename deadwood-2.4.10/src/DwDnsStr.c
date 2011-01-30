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

#include "DwStr.h"
#include "DwStr_functions.h"
#include "DwDnsStr.h"
#include "DwHash.h"
#include "DwMararc.h"
extern int32_t key_n[];

/* Create a blank dns_string object, where the dns packet has size SIZE
 * Input: The number of DNS answers, NS records, and additional records
 * this string will have pointers to.
 * Output: The newly created blank dns_string object
 */

dns_string *dwc_init_dns_str(int32_t ancount, int32_t nscount,
                int32_t arcount) {
        dns_string *out = 0;

        out = malloc(sizeof(dns_string));
        if(out == 0 || ancount < 0 || nscount < 0 || arcount < 0) {
                return 0;
        }
        out->an = malloc((ancount * sizeof(uint16_t) * 2) + 2);
        out->ns = malloc((nscount * sizeof(uint16_t) * 2) + 2);
        out->ar = malloc((arcount * sizeof(uint16_t) * 2) + 2);
        out->packet = 0;
        out->ancount = ancount;
        out->nscount = nscount;
        out->arcount = arcount;
        out->type = 0;
        if(out->an == 0 || out->ns == 0 || out->ar == 0) {
                goto catch_dwc_init_dns_str;
        }
        return out;

catch_dwc_init_dns_str:
        if(out->packet != 0) {
                dw_destroy(out->packet);
        }
        if(out->an != 0) {
                free(out->an);
        }
        if(out->ns != 0) {
                free(out->ns);
        }
        if(out->ar != 0) {
                free(out->ar);
        }
        return 0;
}

/* Destroy an already created dns_string object */
void dwc_zap_dns_str(dns_string *zap) {
        if(zap == 0) {
                return;
        }
        if(zap->packet != 0) {
                dw_destroy(zap->packet);
        }
        if(zap->an != 0) {
                free(zap->an);
        }
        if(zap->ns != 0) {
                free(zap->ns);
        }
        if(zap->ar != 0) {
                free(zap->ar);
        }
        free(zap);
}

/* Place offsets for a given DNS record part in a dns_string */
int32_t dwc_place_offsets(dw_str *in, int32_t offset, int32_t count,
                uint16_t *array) {

        if(dw_assert_sanity(in) == -1) {
                return -1;
        }

        while(count > 0) {
                count--;
                offset -= 2;
                if(offset < 0) {
                        return -1;
                }
                array[(count * 2) + 1] = dw_fetch_u16(in,offset);
                offset -= 2;
                if(offset < 0) {
                        return -1;
                }
                array[count * 2] = dw_fetch_u16(in,offset);
        }
        return offset;

}

/* Convert an uncompressed string in to a newly created dns_string object */
dns_string *dwc_make_dns_str(dw_str *in) {
        dns_string *out = 0;
        int32_t offset = 0;
        uint8_t type = 0;
        int32_t ancount = 0, nscount = 0, arcount = 0;

        if(dw_assert_sanity(in) == -1) {
                goto catch_dwc_make_dns_str;
        }
        offset = in->len - 1;
        if(offset < 7) {
                goto catch_dwc_make_dns_str;
        }
        type = (uint8_t)*(in->str + offset);
        offset -= 2;
        arcount = dw_fetch_u16(in,offset);
        offset -= 2;
        nscount = dw_fetch_u16(in,offset);
        offset -= 2;
        ancount = dw_fetch_u16(in,offset);
        out = dwc_init_dns_str(ancount,nscount,arcount);
        if(out == 0) {
                goto catch_dwc_make_dns_str;
        }
        out->packet = dw_copy(in);
        out->type = type;

        offset = dwc_place_offsets(in,offset,arcount,out->ar);
        if(offset < 0) {
                goto catch_dwc_make_dns_str;
        }
        offset = dwc_place_offsets(in,offset,nscount,out->ns);
        if(offset < 0) {
                goto catch_dwc_make_dns_str;
        }
        offset = dwc_place_offsets(in,offset,ancount,out->an);
        if(offset < 0) {
                goto catch_dwc_make_dns_str;
        }

        return out;

catch_dwc_make_dns_str:
        if(out != 0) {
                dwc_zap_dns_str(out);
        }
        return 0;
}

#ifndef UNUSED

/* Convert a DNS string object back in to an uncompressed string as stored
 * in the Deadwood cache */

dw_str *dwc_convert_dns_str(dns_string *in) {
        int32_t len = 0, counter = 0;
        dw_str *out = 0;

        if(in == 0) {
                goto catch_dwc_convert_dns_str;
        }
        if(in->packet == 0 || in->an == 0 || in->ns == 0 || in->ar == 0) {
                goto catch_dwc_convert_dns_str;
        }

        len = in->packet->len;
        len += (in->ancount + in->nscount + in->arcount) * 4;
        len += 8;
        out = dw_create(len);
        dw_append(in->packet,out);
        for(counter = 0; counter < in->ancount * 2; counter += 2) {
                dw_push_u16(in->an[counter],out);
                dw_push_u16(in->an[counter + 1],out);
        }
        for(counter = 0; counter < in->nscount * 2; counter += 2) {
                dw_push_u16(in->ns[counter],out);
                dw_push_u16(in->ns[counter + 1],out);
        }
        for(counter = 0; counter < in->nscount * 2; counter += 2) {
                dw_push_u16(in->ar[counter],out);
                dw_push_u16(in->ar[counter + 1],out);
        }

        dw_push_u16(in->ancount,out);
        dw_push_u16(in->nscount,out);
        dw_push_u16(in->arcount,out);
        dw_addchar(in->type,out);
        return out;

catch_dwc_convert_dns_str:
        if(out != 0) {
                dw_destroy(out);
        }
        return 0;
}

#endif /* UNUSED */

/* Given a list of records pointers in a DNS string, and the DNS packet,
 * return the record type for the number they want.  Return 65393 (see
 * doc/internals/RR.Allocation for a description of how these RR numbers
 * are reserved for Deadwood's internal use; 65393 is "error") on error */
uint16_t dwc_get_type(dw_str *packet, uint16_t *list, int32_t max,
                int32_t offset) {
        int32_t where = 0,val = 0;

        if(packet == 0 || list == 0) {
                goto catch_dwc_get_type;
        }
        if(offset < 0 || offset > max) {
                goto catch_dwc_get_type;
        }

        where = *(list + offset * 2 + 1);
        if(where < 0 || where > packet->len) {
                goto catch_dwc_get_type;
        }

        val = dw_fetch_u16(packet,where);

        if(val < 0 || (val >= 65392 && val <= 65407) || val >= 65536) {
                goto catch_dwc_get_type;
        }

        return val;

catch_dwc_get_type:
        return 65393;
}

/* Determine where the first non-CNAME record to rotate is */
int dwc_rr_find(dns_string *look) {
        int rr = 0;
        uint16_t type = 5; /* CNAME */

        /* Iterate through the RRs in the answer until we got one that isn't
         * a CNAME */
        for(rr = 0; rr < look->ancount && type == 5; rr++) {
                type = dwc_get_type(look->packet, look->an, look->ancount, rr);
                if(type == 65393) { /* Error */
                        return -1;
                }
        }
        if(rr > look->ancount) {
                return -1;
        }
        rr--;

        return rr;
}

/* Rotate DNS records in a compressed DNS string */

int dwc_rr_rotate(dw_str *in) {
        dns_string *look = 0;
        int ret = -1, rr = 0;
        int32_t start = 0, pivot = 0, end = 0;

        if(in == 0) {
                goto catch_dwc_rr_rotate;
        }

        look = dwc_make_dns_str(in);
        if(look == 0 || look->an == 0) {
                goto catch_dwc_rr_rotate;
        }

        rr = dwc_rr_find(look);
        if(rr == -1 || rr + 1 >= look->ancount) {
                goto catch_dwc_rr_rotate;
        }
        start = *(look->an + (rr * 2));
        pivot = *(look->an + ((rr + 1) * 2));
        if(look->nscount == 0 && look->arcount == 0) {
                end = in->len - 7 - (look->ancount * 4);
                if(end < 0) {
                        goto catch_dwc_rr_rotate;
                }
        } else if(look->nscount > 0) {
                end = *(look->ns);
        } else if(look->arcount > 0) {
                end = *(look->ar);
        } else {
                goto catch_dwc_rr_rotate;
        }
        if(dw_rotate(in,start,pivot,end) == -1) {
                goto catch_dwc_rr_rotate;
        }
        ret = 1;

catch_dwc_rr_rotate:
        if(look != 0) {
                dwc_zap_dns_str(look);
        }
        return ret;
}

/* Age the TTLs for a given entry in the cache */
int dwc_ttl_age(dw_str *fetch, int32_t ttl) {
        dns_string *look = 0;
        int ret = -1;
        int counter = 0;
        uint16_t left = 0, right = 0;

        if(ttl == -1) {
                goto catch_dwc_ttl_age;
        }

        look = dwc_make_dns_str(fetch);

        if(look == 0 || look->an == 0) {
                goto catch_dwc_ttl_age;
        }

        if(ttl < 5) {
                ttl = 5;
        }

        left = ttl >> 16;
        left &= 0x7fff;
        right = ttl & 0xffff;

        for(counter = 0; counter < look->ancount; counter++) {
                dw_put_u16(fetch,left,look->an[(counter * 2) + 1] + 4);
                dw_put_u16(fetch,right,look->an[(counter * 2) + 1] + 6);
        }

        ret = 1;

catch_dwc_ttl_age:
        if(look != 0) {
                dwc_zap_dns_str(look);
        }
        return ret;

}

/* Process an entry in the cache: Perform RR rotation, TTL aging, etc. */
void dwc_process(dw_hash *cache, dw_str *query, uint8_t action) {
        dw_str *fetch = 0;
        int32_t ttl = 0;

        /* TTL aging */
        if(key_n[DWM_N_ttl_age] == 1 && (action & 0x02) == 2) {
                fetch = dwh_get(cache,query,0,1);
                if(fetch == 0) {
                        goto catch_dwc_process;
                }
                ttl = dwh_get_ttl(cache,query);
                if(ttl == -1) {
                        goto catch_dwc_process;
                }
                if(dwc_ttl_age(fetch,ttl) == -1) {
                        goto catch_dwc_process;
                }
                dwh_add(cache,query,fetch,ttl,1);
                dw_destroy(fetch);
                fetch = 0;
        }

        /* RR rotation */
        if(key_n[DWM_N_max_ar_chain] == 1 && (action & 0x01) == 1) {
                fetch = dwh_get(cache,query,0,1);
                if(fetch == 0) {
                        goto catch_dwc_process;
                }
                if(dwc_rr_rotate(fetch) == -1) {
                        goto catch_dwc_process;
                }
                ttl = dwh_get_ttl(cache,query);
                if(ttl == -1) {
                        goto catch_dwc_process;
                }
                dwh_add(cache,query,fetch,ttl,1);
        }

catch_dwc_process:
        if(fetch != 0) {
                dw_destroy(fetch);
        }
        return;
}

/* See if an IP in our answer is blacklisted.  1 if it is, 0 if it's not or
 * we got an error */
int has_bad_ip(dw_str *answer, dwd_dict *blacklist_hash) {
        dns_string *look = 0;
        dw_str *ip = 0;
        int counter = 0;
        int32_t type = 0;

        if(answer == 0 || blacklist_hash == 0) {
                goto catch_has_bad_ip;
        }

        look = dwc_make_dns_str(answer);
        if(look == 0 || look->an == 0) {
                goto catch_has_bad_ip;
        }

        for(counter = 0; counter < look->ancount; counter++) {
                type = dw_fetch_u16(answer,look->an[(counter * 2) + 1]);
                if(type == 1 /* A record */ ) {
                        ip = dw_substr(answer,
                                look->an[(counter * 2) + 1] + 10,4,1);
                        if(dwd_fetch(blacklist_hash,ip) != 0) {
                                dwc_zap_dns_str(look);
                                dw_destroy(ip);
                                return 1;
                        }
                        dw_destroy(ip);
                        ip = 0;
                } else if(type == 28 /* AAAA record */) {
                        ip = dw_substr(answer,
                                look->an[(counter * 2) + 1] + 10,16,1);
                        if(dwd_fetch(blacklist_hash,ip) != 0) {
                                dwc_zap_dns_str(look);
                                dw_destroy(ip);
                                return 1;
                        }
                        dw_destroy(ip);
                        ip = 0;
                }

        }

catch_has_bad_ip:
        if(look != 0) {
                dwc_zap_dns_str(look);
        }
        return 0;

}


