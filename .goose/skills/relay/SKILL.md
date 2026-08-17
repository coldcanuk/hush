---
name: relay
description: "Run and test the hush-relay (C11 poll server + Nostr line protocol)."
---
# relay

## Start
./hush-relay 10555   # or after make in hush-c/

## Test with raw Nostr lines (netcat)
printf '["EVENT",{"id":"deadbeef...","pubkey":"00..","kind":1,"created_at":1720000000,"content":"hi","sig":"00.."}]\n' | nc -q 1 localhost 10555

## Simple REQ
printf '["REQ","sub1",{"kinds":[1]}]\n' | nc -q 1 localhost 10555

Expect EVENT frames or EOSE on stdout of relay (logs).
