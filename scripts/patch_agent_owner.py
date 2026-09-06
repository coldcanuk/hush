import sys

with open('hush-c/src/hush_agent.c', 'r') as f:
    content = f.read()

funcs = """
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
    hush_agent_owner_t *own = hush_agent_owner_find(channel);
    if (own == NULL)
        own = hush_agent_owner_alloc(channel);
    if (own == NULL)
        return;
    
    // In this basic version, if we're unsure or no owner, the first agent mentioned becomes owner.
    // In a real network, this would trigger a broadcast, but all agents run locally here.
    if (own->owner_hex[0] == '\\0' || own->unsure) {
        own->unsure = 0;
        hush_agent_robot_t bot;
        if (hush_agent_lookup_robot(&bot, launch, mention) && bot.hex != NULL) {
            hush_agent_copy(own->owner_hex, sizeof(own->owner_hex), bot.hex);
            // Broadcast NIP-77 29007 to establish owner
            hush_agent_emit(HUSH_CEVENT_MENTION, NULL, NULL, bot.hex, "election_won");
        }
    }
}
"""

# Insert before hush_agent_handle_mention implementation
target = "static void hush_agent_handle_mention(hush_store_t *store,"
content = content.replace(target, funcs + "\n" + target)

with open('hush-c/src/hush_agent.c', 'w') as f:
    f.write(content)

