---
name: connect-timeout
internalName: connectTimeout
type: unsigned long
default: 0
---
The timeout (in seconds) for establishing connections in the
binary cache substituter. It corresponds to `curl`’s
`--connect-timeout` option. A value of 0 means no limit.
