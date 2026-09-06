import sys

with open('hush-c/src/hush_agent.c', 'r') as f:
    content = f.read()

target = """    if (hush_agent_robot_busy(&bot, ev))
        return;"""

replacement = """    if (hush_agent_robot_busy(&bot, ev))
        return;
    
    // NIP-77 Agent Conversation Ownership Election
    char chan[64];
    hush_agent_event_channel(chan, sizeof(chan), ev);
    if (chan[0] != '\\0') {
        hush_agent_establish_owner(store, launch, ev, chan, mention);
    }
"""

content = content.replace(target, replacement)

with open('hush-c/src/hush_agent.c', 'w') as f:
    f.write(content)
