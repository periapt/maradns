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
#include "DwHash.h"
#include "DwDict.h"

typedef struct {
        dw_str *packet;
        uint16_t *an; /* Answers */
        uint16_t *ns; /* Name server answers */
        uint16_t *ar; /* Additional answers */
        int32_t ancount;
        int32_t nscount;
        int32_t arcount;
        uint8_t type;
} dns_string;

/* Public methods that create and destroy "DNS strings" */

/* Convert an uncompressed string in to a newly created dns_string object */
dns_string *dwc_make_dns_str(dw_str *in);

/* Destroy an already created dns_string object */
void dwc_zap_dns_str(dns_string *zap);

/* Public methods for processing the DNS cache */

/* Process an entry in the cache: Perform RR rotation, TTL aging, etc. */
void dwc_process(dw_hash *cache, dw_str *query, uint8_t action);

/* Public method for IP blacklist management */

/* See if an IP in our answer is blacklisted.  1 if it is, 0 if it's not or
 * we got an error */
int has_bad_ip(dw_str *answer, dwd_dict *blacklist_hash);

