import sys

with open('hush-c/src/hush_agent.c', 'r') as f:
    content = f.read()

# Add struct definition
structs = """
#define HUSH_AGENT_OWNER_MAX 16
#define HUSH_AGENT_KIND_OWNER 29007
typedef struct {
    char channel[64];
    char owner_hex[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    int unsure;
} hush_agent_owner_t;
static hush_agent_owner_t g_owners[HUSH_AGENT_OWNER_MAX];
"""
content = content.replace("static unsigned g_id_seq;", structs + "\nstatic unsigned g_id_seq;")

# Add logic
funcs = """
static void hush_agent_emit(const char *type, const char *channel, const char *id, const char *p1, const char *p2);

static hush_agent_owner_t *hush_agent_owner_find(const char *channel)
{
    size_t i;
    for (i = 0; i < HUSH_AGENT_OWNER_MAX; i++) {
        if (g_owners[i].channel[0] != '\\0' && strcmp(g_owners[i].channel, channel) == 0)
            return &g_owners[i];
    }
    return NULL;
}

static hush_agent_owner_t *hush_agent_owner_alloc(const char *channel)
{
    size_t i;
    for (i = 0; i < HUSH_AGENT_OWNER_MAX; i++) {
        if (g_owners[i].channel[0] == '\\0') {
            hush_agent_copy(g_owners[i].channel, sizeof(g_owners[i].channel), channel);
            return &g_owners[i];
        }
    }
    return NULL;
}

static void hush_agent_establish_owner(hush_store_t *store, const hush_launch_t *launch, const hush_event_t *ev, const char *channel, const char *mention)
{
    (void)store;
    (void)ev;
    hush_agent_owner_t *own = hush_agent_owner_find(channel);
    if (own == NULL)
        own = hush_agent_owner_alloc(channel);
    if (own == NULL)
        return;
    
    // In this basic version, if we're unsure or no owner, the first agent mentioned becomes owner.
    if (own->owner_hex[0] == '\\0' || own->unsure) {
        own->unsure = 0;
        hush_agent_robot_t bot;
        if (hush_agent_lookup_robot(&bot, launch, mention) && bot.hex != NULL) {
            hush_agent_copy(own->owner_hex, sizeof(own->owner_hex), bot.hex);
            hush_agent_emit(HUSH_CEVENT_MENTION, channel, NULL, bot.hex, "election_won");
        }
    }
}
"""

# Insert only before the implementation. We find it by matching the exact parameter layout with a newline.
target_impl = """static void hush_agent_handle_mention(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev,
                                      const char *mention)
{"""
content = content.replace(target_impl, funcs + "\n" + target_impl)

target_hook = """    if (hush_agent_robot_busy(&bot, ev))
        return;"""
replacement_hook = """    if (hush_agent_robot_busy(&bot, ev))
        return;
    
    char chan[64];
    hush_agent_event_channel(chan, sizeof(chan), ev);
    if (chan[0] != '\\0') {
        hush_agent_establish_owner(store, launch, ev, chan, mention);
    }
"""
content = content.replace(target_hook, replacement_hook)

with open('hush-c/src/hush_agent.c', 'w') as f:
    f.write(content)
