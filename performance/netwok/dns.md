K!-- performance/network/dns.md -->
# DNS (Domain Name System)
DNS is one of the most fundamental Internet services, Its core job is to resolve domain names to IP addresses (and back).
Beyond human-friendly names, DNalso underpins dynamic service discovery and
**Global Server Load Balancing (GSLB)*: DNS can hand back the IP closest to the user, and when a backend's IP changes, clients keep using the same name.
## Domain name structure
Domain names are globally unique and hierarchical. Taking time.geekbang.org' :
-   'org" - top-level domain (TLD)
-   'geekbang
-   second-level domain
-   "time" - third-level domain
The service that turns names into IPs is DNS; the servers are **name servers** and the wire protocol is the **DNS protocol. Resolution walks the hierarchy (from the root down) until an answer is obtained.
## Recursive vs. iterative queries
The original notes glossed over an important distinction:
-  **Recursive query,** The client asks a resolver for a final answer and the resolver is obligated to return a complete result. If it lacks the data, the resolver itself queries other servers and returns the final answer to the client. Typically used between a stub client and its configured resolver (e-g- the LAN or ISP resolver).
-   **Iterative query.**The server does not return the final answer; instead it returns a referral to another name server that is closer to the answer. The client then queries that server, repeating until it gets the answer.Typically used between a recursive resolver and the authoritative servers (root → TLD → authoritative).
## Configuring DNS on Linux
Besides an IP address, a host needs a resolver:
***bash
$ cat /etc/resolv.conf nameserver 114-114.124.114
**Note (modern systems):** on Ubuntu with systemd-resolved, "/etc/resolv.conf" is usually a symlink to a stub resolver at 127.0.0.53°, and the real upstreams are managed by resolvectl. Query current state with resolvectl status.
## Resource records
DNS stores data as resource records; common types:
1.
*  *д** - maps a name to an IPv4 address (**AAAA** for IPv6).
*  *NAME** - an alias from one name to another.
3.
**NS** - the authoritative name server for a zone.
4.
***x** - mail exchanger; **pTR** - reverse (IP → name).
## Looking up records
### nslookup
•*bash nslookup time.geekbang.org
# name server and port
Server:
114.194.114.114
Address:
114.114,114.144#53
# non-authoritative answer (served from cache)
Non-authoritative answer;
Name:
time.geekbang.org
Address: 39.106.233.17
# dig +trace - watching the recursion
"dig's +trace performs the Iterative walk itself, showing each referral:
•*bash
* +trace: follow the delegation from the root
# +nodnssec: disable DNSSEC records in output
$ dig +trace +nodnssec time.geekbang.org
; <> DiG 9.11.3-1ubuntu1.3-Ubuntu <> +trace +nodnssec time.geekbang.org
:; global options: +cmd
322086
IN
322086
IN
322086
IN
322886
IN
322086
IN
322086
IN
NS
NS
NS
NS
NS
NS
m.root-servers.net. a.root-servers.net. i.root-servers.net. d.root-servers.net. g.root-servers.net.
1.root-servers.net.

322086
IN
322086
IN
322086
IN
322086
IN
322086
IN
322086
IN
322086
IN
NS
NS
NS
NS
NS
NS
NS
c.root-servers.net. b.root-servers.net. h.root-servers.net. e.root-servers.net. k.root-servers.net. j.root-servers.net.
；；
f.root-servers.net.
Received 239 bytes from 114.114.114.114#53(114.114.114.114) in 1340 ms
org.
172800
IN
NS
org.
172800
ae.org.afilias-nst.info.
org.
172800
IN
NS
IN
a2.org.afilias-nst.info.
org.
172808
NS
be.org.afilias-nst.org.
org.
172808
IN
NS
b2.org.afilias-nst.org.
IN
org.
NS
172808
IN
ce.org.afilias-nst.info.
NS
de.org.afilias-nst.org.
:: Received 448 bytes from 198.97.190.53#53(h.root-servers.net) in 708 ms
geekbang.org.
86400
IN
geekbang.org.
86400
IN
NS
NS
dns9.hichina.com.
;; Received 96
dns1e.hichina.com.
bytes from 199.19.54.1#53(be.org.afilias-nst.org) in 1833 ms
time-geekbang-org.
600 IN A 39.106.233.176
;: Received 62 bytes from 140.205.41.16#53(dns10.hichina.com) in 4 ms
The output has four parts:
TLD org..
1.    Pick one org. NS (be.org.afilias-nst.org") and query the NS servers for geekbang.org.'.
2.    From geekbang.org. s NS (®dns10.hichina.com"), query the final A record for time.geekbang.org. •
## Static mappings: /etc/hosts
•**bash
$ cat /etc/hosts
127.⑧.⑧.1
localhost localhost.localdomain
3:1
localhost6 localhost6.localdomain6
192.168.0.100 domain.com
On an internal network you can also run a private DNS server to resolve internal names,
usually with one or more upstream resolvers for external names.
## DNS caching with dnsmasq
Enabling a local cache takes extra configuration; the simplest option is
* dnsmasa, one of the most common DNS caches (and often a DHCP server too). It is easy to install and configure and fast enough for almost all applications.
•**bash
/ /etc/init.d/dnsmasqstart
* Starting DNS forwarder and DHCP server dnsmasq
[ OK ]
Point the resolver at dnsmasq (here "127.0.0.1°) and run nslookup" repeatedly:
•**bash
I /etc/resolv.conf
/ time nslookup time.geekbang.org
Server:
127.⑨.⑧.1
Address: 127.0.0.1#53
Non-authoritative answer;
Name:
time.geekbang.org
Address: 39.106.233.176
real user sys
Om0,4925
em6.0075
em0.006s
/ time nslookup time.geekbang.org
Server:
127，⑨.⑨.1
Address: 127.0.9.1#53
Non-authoritative answer:
Name:
time.geekbang.org
Address: 39.106.233.176
real user
0m0.011s
0m0.008s em0.003s
I
The first query takes ~492 ms (cold), the second ~11 ms (served from cache).
**Modern alternative:** • systemd-resolved already caches: you can inspect cache



Cach
stats via resolvectl statistics and flush with resolvectl flush-caches • dnsmasa remains useful for split-horizon and DHCP integration.
## Common DNS optimizations
1.    **Cache results.** The most effective single technique. Mind TTL expiry - once a record expires you must re-query, which is acceptable for most apps.
2.    **Prefetch.** Browsers resolve link domains in the background before the user clicks, caching the result.
3.    **HTTPDNS.** Mobile apps often resolve via HTTP(S) to a known endpoint, bypassing on-path resolvers to defeat DNS hijacking.
4.    **GSLB (DNS-based global load balancing).** Provides load balancing and HA, and can return the geographically closest IP.
## Troubleshooting workflow
1.    Separate resolution from connectivity: •dig / nslookup for resolution, ping curl for reachability.
2.    If nslookup succeeds but "ping ‹name›" fails with "unknown host", the problem is usually NSS/*/etc/nsswitch.conf or /etc/hosts" ordering, not the DNS server - applications resolve through the C library (NSS), not by calling the DNS server directly (see the reference below).
3.    Check which resolver is actually used: resolvectl status, •cat /etc/resolv.conf •
4.    Time the lookup: •dig +stats, or •time nslookup .
5.    Watch for TTL- / tiny TTLs causing excessive queries; add a sache.
6.    Capture port-53 traffic with tcpdump -nn port 53° to see queries/responses.
## Pitfalls
- **nslookup bypasses NSS.** It queries the DNS server directly, so it can succeed while "ping / curl (which go through glibc NSS) fail. Always confirm
*/etc/nsswitch.conf" hosts:* ordering.
-   Very low TTLs can turn DNS into a latency hotspot under load."systemd-resolved"'s stub at "127.0.0.53" can mask the real upstream - don't assume /etc/resolv.conf shows the true server.
-   UDP DNS responses over 512 B may fall back to TCP; ensure port 53/TCP is open.
## Likely interview questions
-   Recursive vs, iterative queries - who does which?
-   Why can 'nslookup" succeed while "ping" fails for the same name?
-   How does DNS enable GSL, and what are its limits (caching/TTL)?
-   Walk through "dig trace from root to authoritative answer.
-   How does "systemd-resolved" change where lookups actually go?
-   A record v5, CNAME vS, NS - what does each return?
## References
-   "nslookup oK but ping fall" - glibc NSS resolution internals: <https://plantegg.github.1o/2019/01/09/nslookup-OK-but-pins-fall/>
-  Go DNS resolution flow;<http://blog.bruceding.com/516.html>

