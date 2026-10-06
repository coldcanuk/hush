/* api_identity.c: owns identity, profile, member, and vibe routes. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_auth.h"
#include "hush_http_internal.h"
#include "hush_mem.h"
#include "hush_pass.h"
#include "hush_identity.h"

/* Rotates the join token and returns the new plaintext exactly once. */
static hush_status_t hush_http_serve_vibe_rotate(int fd);

/* Imports an nsec and wipes the stack copy on every return. */
static hush_status_t hush_http_import_nsec(int fd, const char *body);
/* Derives npub from nsec without logging in; wipes secrets. */
static hush_status_t hush_http_preview_nsec(int fd, const char *body);

int hush_http_want_save_pass(const char *body)
{
    char flag[8];

    if (!hush_http_json_field(body, "save_pass", flag, sizeof(flag)))
        return 1;
    if (strcmp(flag, "false") == 0 || strcmp(flag, "0") == 0)
        return 0;
    return 1;
}

hush_status_t hush_http_serve_identity(int fd, const char *body)
{
    char action[32];

    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "action", action, sizeof(action)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (strcmp(action, "create") == 0)
        return hush_http_reply_session(fd, hush_launch_create_identity(hush_http_launch()));
    if (strcmp(action, "import") == 0)
        return hush_http_import_nsec(fd, body);
    if (strcmp(action, "preview") == 0)
        return hush_http_preview_nsec(fd, body);
    if (strcmp(action, "ack_backup") == 0)
        return hush_http_reply_session(fd,
                                       hush_launch_ack_backup(hush_http_launch(),
                                                              hush_http_want_save_pass(body)));
    if (strcmp(action, "logout") == 0)
        return hush_http_reply_session(fd, hush_launch_logout(hush_http_launch()));
    return hush_http_reply_session(fd, HUSH_ERR_PARSE);
}

hush_status_t hush_http_serve_profile(int fd, const char *body)
{
    hush_roster_profile_t profile = {0};
    char approval[HUSH_ROSTER_NAME_MAX] = {0};
    hush_status_t st = HUSH_OK;

    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    /* #279: the approval setting is posted on its own, so it never clears
     * the name fields below (a body without them would). A body naming the
     * key in compact form ("approval_mode":) takes this branch; an empty,
     * non-string or space-after-colon value stays "" and is refused. The
     * flat reader does not see "approval_mode" : (space before the colon,
     * #289), so such a body falls through to the profile save below (#288). */
    if (hush_http_json_has_key(body, "approval_mode")) {
        (void)hush_http_json_field(body, "approval_mode", approval, sizeof(approval));
        st = hush_launch_set_approval(hush_http_launch(), approval);
        return hush_http_reply_refused(fd, st,
                                       st == HUSH_ERR_PARSE ? HUSH_HTTP_APPROVAL_WHY : NULL);
    }
    (void)hush_http_json_field(body, "first_name", profile.first_name,
                          sizeof(profile.first_name));
    (void)hush_http_json_field(body, "last_name", profile.last_name,
                          sizeof(profile.last_name));
    (void)hush_http_json_field(body, "email", profile.email, sizeof(profile.email));
    (void)hush_http_json_field(body, "organization", profile.organization,
                          sizeof(profile.organization));
    (void)hush_http_json_field(body, "theme", profile.theme, sizeof(profile.theme));
    (void)hush_http_json_field(body, "picture", profile.picture,
                          sizeof(profile.picture));
    return hush_http_reply_session(fd,
                                   hush_launch_set_profile(hush_http_launch(), &profile));
}

hush_status_t hush_http_serve_member(int fd, const char *body)
{
    char key[HUSH_IDENTITY_NPUB_MAX];
    char name[HUSH_ROSTER_NAME_MAX];

    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "npub", key, sizeof(key)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        memcpy(name, "human", 6);
    return hush_http_reply_session(fd,
                                   hush_launch_add_member(hush_http_launch(), key, name));
}


hush_status_t hush_http_serve_vibe(int fd, const char *body,
                                          hush_store_t *store)
{
    char name[HUSH_LAUNCH_NAME_MAX];
    char about[HUSH_LAUNCH_ABOUT_MAX];
    char vis[16];
    char action[16];
    int is_public = 1;

    if (hush_http_launch() == NULL || body == NULL || store == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (hush_http_json_field(body, "action", action, sizeof(action)) &&
        strcmp(action, "rotate_token") == 0)
        return hush_http_serve_vibe_rotate(fd);
    if (hush_http_json_field(body, "visibility", vis, sizeof(vis)) &&
        strcmp(vis, "private") == 0)
        is_public = 0;
    if (hush_http_launch()->has_vibe &&
        !hush_http_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd,
                                       hush_launch_set_vibe_visibility(hush_http_launch(),
                                                                       is_public));
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        memcpy(name, "local hive", 11);
    if (!hush_http_json_field(body, "about", about, sizeof(about)))
        about[0] = '\0';
    if (hush_launch_create_vibe(hush_http_launch(), store, name, about) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_CRYPTO);
    return hush_http_reply_session(fd,
                                   hush_launch_set_vibe_visibility(hush_http_launch(),
                                                                   is_public));
}

/* Imports an nsec and wipes the stack copy on every return. */
static hush_status_t hush_http_import_nsec(int fd, const char *body)
{
    char secret[HUSH_IDENTITY_NSEC_MAX] = {0};
    hush_status_t st;

    assert(body != NULL);
    assert(hush_http_launch() != NULL);
    if (!hush_http_json_field(body, "nsec", secret, sizeof(secret))) {
        hush_secure_zero(secret, sizeof(secret));
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    }
    st = hush_launch_import_identity(hush_http_launch(), secret);
    hush_secure_zero(secret, sizeof(secret));
    return hush_http_reply_session(fd, st);
}

/* Preview: derive the matching npub without changing login state. */
static hush_status_t hush_http_preview_nsec(int fd, const char *body)
{
    char secret[HUSH_IDENTITY_NSEC_MAX] = {0};
    hush_identity_t id;
    hush_status_t st;
    char out[HUSH_IDENTITY_NPUB_MAX + 48];
    int n;

    assert(body != NULL);
    memset(&id, 0, sizeof(id));
    if (!hush_http_json_field(body, "nsec", secret, sizeof(secret))) {
        hush_secure_zero(secret, sizeof(secret));
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    }
    st = hush_identity_import(&id, secret);
    hush_secure_zero(secret, sizeof(secret));
    if (st != HUSH_OK) {
        hush_identity_clear(&id);
        return hush_http_reply_session(fd, st);
    }
    n = snprintf(out, sizeof(out),
                 "{\"ok\":true,\"npub\":\"%s\"}\n", id.npub);
    hush_identity_clear(&id);
    if (n < 0 || (size_t)n >= sizeof(out))
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", out, (size_t)n);
    return HUSH_OK;
}

/* Rotates the join token and returns the new plaintext exactly once. */
static hush_status_t hush_http_serve_vibe_rotate(int fd)
{
    char out[HUSH_LAUNCH_NAME_MAX + 48];
    hush_status_t rotated;
    int n;

    assert(hush_http_launch() != NULL);
    rotated = hush_launch_rotate_token(hush_http_launch());
    if (rotated != HUSH_OK)
        return hush_http_reply_session(fd, rotated);
    n = snprintf(out, sizeof(out),
                 "{\"ok\":true,\"action\":\"rotate_token\",\"join_token\":\"%s\"}\n",
                 hush_http_launch()->vibe_token);
    if (n <= 0 || (size_t)n >= sizeof(out))
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", out, (size_t)n);
    return HUSH_OK;
}

