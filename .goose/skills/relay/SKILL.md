---
name: relay
description: "Run and test the hush-relay (C11 poll server + Nostr line protocol + chat UI)."
---
# relay

## Start
./hush-relay --no-open 10555   # or after make in hush-c/
# Chat UI: http://127.0.0.1:10555/

## Test with raw Nostr lines (netcat)
printf '["EVENT",{"id":"deadbeef...","pubkey":"00..","kind":1,"created_at":1720000000,"content":"hi","sig":"00.."}]\n' | nc -q 1 localhost 10555

## Simple REQ
printf '["REQ","sub1",{"kinds":[1]}]\n' | nc -q 1 localhost 10555

Expect `["OK", ...]` after EVENT, then EVENT frames + `["EOSE", ...]` after REQ.
