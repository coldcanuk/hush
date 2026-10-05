/* tests/fake_curl_vault.h: control the no-network libcurl stub. */

#ifndef FAKE_CURL_VAULT_H
#define FAKE_CURL_VAULT_H

void fake_curl_set_reply(long code, const char *body);
void fake_curl_fail_perform(void);
void fake_curl_reset_slist_stats(void);
int fake_curl_slist_saw_live_token(void);
int fake_curl_slist_freed_token_header(void);
int fake_curl_slist_token_hdr_wiped(void);

#endif
