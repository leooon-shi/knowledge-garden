y aiagnosing_intermittent_network_latency.ma.
Ki-- performance/network/diagnosing_intermittent_network_Latency.md ->
# Diagnosing Intermittent Network Latency
A practical guide to finding and root-causing problems that appear only
*occasionally* - the hardest class of production issue. The canonical example: a service whose requests normally finish within 1 second but sometimes take 5-6 seconds, with no obvious trigger.
This guide is written for two audiences at once:
-  **If you are newer to this**, follow the numbered phases in order; each one tells you "what* to do and why*.
-  **If you are experienced**, use the decision frameworks and the "pitfal1s" callouts as a checklist; skip to the phase you are stuck on.
The hardest part of an intermittent problem is not analyzing one bad transaction- it is*catching it at al1** when it is rare, and then **precisely attributing** the delay to the right layer and the right hop. The core principle throughout:
*  *Make the rare, fuzzy symptom measurable. ** Convert "sometimes slow" into a numeric threshold on a per-transaction metric, then let filtering and sorting surface the few bad transactions out of thousands. A low-frequency problem then
*   becomes a deterministic search instead of a waiting game.
## Overview: the diagnostic method
Intermittent issues reward a disciplined loop, not ad-hoc poking:
1.    **Characterize** - quantify frequency, period, blast radius, and what "slow" means numerically.
2.    **Capture - get the evidence without drowning in it (right place, right duration, right trigger).
3.    **Localize®* - narrow from "the system is slow" to a specific layer (app / transport / network) and a specific hop.
4.    *Attribute** - prove where* the time goes with timestamps, not hunches.
5.    **Confirm & fix** - reproduce the attribution at a second vantage point, remediate, and verify the metric moved.
Each phase below maps to one of these. The worked example (a 6.5-second server-side stall behind an HTTP POST) runs through Phases 3-5 to make the method concrete.
## Phase 1- Characterize the problem
##* 1.1 Turn the complaint into numbers
Before any capture, answer two questions explicitly:
1.    *What counts as "slow"/"stuttering"?** (Which operation, seen by whom?)
2.    **How slow is slow?** Pick a threshold. As a rule of thumb, an HTTP page response over 3-4 seconds is a serious problem; under 1 second is ideal.
This converts a fuzzy complaint into a concrete search condition such as
*packets belonging to HTTP transactions whose response time exceeds 3 seconds. ** Everything downstream depends on having this number.
### 1.2 Estimate frequency, period, and scope
-   **Frequency / period** - how often does it recur, and is there a pattern (top of the hour, during backups, at a traffic peak, after hours of uptime? This sizes your capture window; a few events per hour needs a window long enough to contain several occurrences.
-  **Scope /blastradius** - one user or all, one endpoint or many, one host or the whole cluster, one direction or both. Scope immediately narrows the suspect list (a single-client issue is rarely the server's core network).
*  **pitfall (junior):**starting a 10-second capture for a problem that happens
*   twice an hour, You will almost always catch nothing, Size the window to the
*   frequency estimate, or use a trigger (below),
##* 1.3 Prefer signals over blind capture
If you have monitoring (Grafana/Prometheus, APM traces, RUM), use it first; dashboards and **alerts** tell you *when* the problem is happening so you capture on demand instead of speculatively, Latency histograms (p50/p95/p99/p999) are especially valuable - an intermittent problem 11ves in the **ta11** (p99/p999), which averages hide completely.
## Phase 2 - Capture the evidence
### 2.1 Where to capture
Capture as close as possible to where the symptom 1s reported, and ideally at
**more than one point** along the path (client, proxy/LB, server). Single-point capture is often enough to *Localize* but rarely enough to confirm* which hop owns the delay ( see phase 5).

### 2.2 How long, and how not to fill the disk
Size the duration from the Phase 1 frequency estimate. For rare events, use a
*  *ring / rolling capture** - fixed-size files that overwrite the oldest - so you can run for hours and stop once the symptom is seen:
*  **bash
# topdump: files files x MB ring, snaplen full, filter to the service port
tepdump -1 any -w cap_%Y%m%d_*H*M%S.pcap -G 3600 -C 100 -W 50 \
-s e 'tcp port 8082'
# dumpcap (ships with Wireshark) equivalent ring buffer
dumpcap -1 any -b filesize:100000 -b files:50 -w cap.pcapng -f'tcp port 8082'
#** 2.3 Scope the capture with a BPF filter
A capture filter (BPF) keeps the trace small and relevant. Narrow by host/port to the service in question; avoid over-filtering so tightly that you miss the resets, retransmits, or IMP that often explain the symptom.
*  **pitfall (both):** capturing with a snaplen that truncates payload when you
*   later need to match requests by content, or filtering out ICMP/RST that would
*   have been the smoking gun. When in doubt, capture full frames and filter during
*   analysis*, not capture.
##* 2.4 Keep clocks trustworthy
Multi-point capture is only comparable if the clocks agree. Ensure NTP/PTP 1s synced on all capture hosts; otherwise a "delay" may be clock skew between two traces, not real latency.
SPE
## Phase 3- Localize: find the slow transactions among many
Goal: reduce thousands of transactions to the few that violate your threshold.
### 3.1 Baseline a normal transaction
Inspect the TCP stream of a randomly chosen (healthy) HTTP transaction to learn what "good" looks like. Wireshark tip: the two horizontal arrows on the left (one right, one left) mark the HTTP request and response packets; the Time column delta between them is the HTTP duration. In the example, a sampled transaction spent ~115 ms (between packets 642 and 641) - well under the 3 s threshold, i.e. healthy.
### 3.2 Rank by a per-transaction metric (Wireshark)
Use two Wireshark features together - *display filters"* and **custom columns**.
**The 'http.time" metric.** Select an HTTP response packet; the details pane shows "[Time since request: X seconds]®. This is an analysis attribute Wireshark computes, not a packet field. Its filter is "http.time":
-   "http.time" as a filter displays **all HTTP responses** (Wireshark attaches the attribute to the response packet).
-   plain *http shows both requests and responses.
*  *Custom column + sort,** In the HTTP details pane, right-click
*  *Time since request → Apply as Column**. A "http.time" column appears. Click its header to sort descending; the slowest transactions rise to the top. In the example, two transactions took 5-6 seconds while all others were under 1 second - the problem transactions are now isolated.
*   **Generalize the technique;** this "filter + custom column + sort" pattern
*   works for any analysis field, not just "http.time" - e.g. tcp.analysis.ack_rtt"
*   (ACK round-trip time),
•tep, time_delta (time since the previous frame in the
› stream), or "dns.time", Pick the metric that matches the layer you suspect.
#** 3.3 The same ranking from the command line (tshark)
*tshark extracts the metric headlessly - essential for batch-processing many rolling-capture files:
•**bash
#list HTTP durations (drop the blank 1ines produced by non-HTTP packets) tshark r <file› -T fields -e http,time | grep -ving
To pull every packet of the TCP stream containing the single slowest transaction:
••bash
tshark -r <file> -T fields -e frame.number -e http.time -e tcp.stream |
I sort -k2 -r | head -1 | awk •{print $3}' |
1 xargs -n 1 -I () tshark -r ‹file› -y "tcp.stream eq ()"
-   -T fields -e frame.number -e http.time -e tcp.stream" - emit frame number, HTTP duration, and TCP stream number.
-   sort -k2 -r | head -1 | awk '{print $3}*• - sort by duration descending, take the top, extract its stream number.

- *-y "tcp.stream eq ()" - display all packets of that stream (the () is the Stream number from "awk",fed by
"xargs").
This yields the same packets as **Follow → TCP Stream** in the GUI. The command-line form wins when you must: collapse many GUI clicks into one pipe, batch-process many capture files, or copy/paste text output into a ticket.
## Phase 4- Attribute: place the delay with timestamps
Once a slow transaction is isolated, right-click **Follow → TCP Stream** and read the *Time** column deltas to see exactly where the seconds went.
In the example, the wo+ seconds sat between packets 576 and 647:
1.    **576** - the server (listening on port 8082) ACKs the client's POST request.Then a pause of ~6.5 seconds.
2.    **647** - the server sends the HTTP response headers (status code + headers visible in the details pane).
*  *648** - the client ACKs the response headers.
*  *649** - the server sends the response body; the response is now complete (648 + 649), so Wireshark labels packet 649 •HTTP/1.1 200 O*.
*  *650** - the client ACKs the body and carries a FIN to close the connection.
### 4.1 Read the signature
The server took ~6.5 seconds after receiving the request* to reply. This shows two things clearly:
1.    **No packet loss or retransmission** on the network (the request was ACKed immediately; the gap is pure waiting).
2.    The **server's processing time** is over 6 seconds - the delay is
**server-side**, not network-side.
### 4.2 A decision framework: which layer owns the delay?
Use the packet-level evidence to classify, rather than guessing:
I observation in the trace | Most likely owner | Next step |
ا--|---|---ا
1 Request Acked fast, long gap *before* the response | **Server application** (compute, DB, lock, GC, downstream call) | App logs, APM/profiler on the server I
1 Retransmissions, duplicate ACKS, SACK, gaps | **Network** (loss/congestion) | See tcp_wireshark_troubleshooting-md
Case 1 l
I Receiver window pinned to / "Window Full" | **Receiver#* (slow consumer) | tcp_wireshark_troubleshooting.md Cases
2-31
| RST ending the transaction | **An endpoint or a middlebox** aborting | •tcp_wireshark_troubleshooting.md" Case 4 |
I Delay only in DNS / TLS handshake frames | **Name resolution / TS setup** | Measure •dns.time; inspect handshake RTT| I Large, steady base latency on every transaction | **path RTT / routing** | •traceroute / mtr, path NTU |
*   **Pitfall (senior, too):** declaring "it's the server" from a one-sided
*   capture. A proxy or LB between client and server can inject the gap; the
*   client-side trace alone cannot tell the difference. Confirm in Phase 5.
### 4.3 Complementary tools by layer
Packet capture localizes the delay; these confirm the root cause:
- **Path / reachability:** ping (base RTT, loss), "mtr /traceroute (per-hop latency and loss),
*tracepath (path MTU).
-   **Socket / host state:** *ss -tin (per-socket RTT, cnd, retransmits),"netstat -s / "nstat" (stack counters: resets, overflows, drops).
-   **Kernel-level, low overhead for production:** •tcpdump / dumpcap® for capture; eBPF tools - "tepretrans',
"teplife",
"tepconnlat", "runqlat", "biolatency"
(bec/bpftrace) - to attribute latency to retransmits, connection setup, run-queue, or disk without full packet capture,
-**Application:** structured logs with per-request timing, APM/distributed tracing (the server-side span would show the 6.5s directly), profilers/flame graphs for CPU, GC logs, DB slow-query logs,
#* Phase 5 - Confirm and fix
##*5.1 Capture at two points to remove ambiguity
To rule out an intermediate hop, capture **simultaneously at the client and the server** and compare the same transaction:
-   If **both** traces show the ~6.5 s gap between the request ACK and the response, the delay is confirmed **inside the server application**,
-   If the traces **differ** (e.g. the server replies quickly in its own trace but the client sees the gap), a hop in between - proxy, LB, firewall - 1s adding the latency.
With synced clocks (Phase 2.4), aligning the two captures on the same packets 1s straightforward.
### 5.2 Remediate against the attributed cause

Fix where the evidence points, not where it is convenient:
-   **Server-side compute/wait** (this example): profile the handler; the 6.5 s 1s usually a blocking downstream call, lock contention, a slow query, or a GC pause. Add timeouts, make the slow dependency async, or fix the query/index.
-  **Network loss:** address the policer/congestion (see the TCP troubleshooting companion doc).
-   **Receiver slowness:**increase the consumer's read rate or receive buffer.
*Middlebox RST/ idle timeout: align idle timeouts across client, proxy, and upstream; enable keep-alives.
### 5.3 Verify the metric moved
Re-run the exact measurement from Phase 3 after the fix. The slow transactions should disappear from the top of the 'http.time" sort, and the tail latency (p99/p999) on your dashboards should drop. If the number does not move, the attribution was wrong - return to Phase 4. "Looks fixed" is not fixed; the metric is.
## Checklist (quick reference)
-   [ ] Defined "slow" as a **number** (e.g. HTTP > 3 s).
-   [] Estimated **frequency/period**; sized the capture window (or used an alert).
-   ] Captured at the **right points)*, full frames, with a **ring buffer*» for rare events, ""clocks synced"*.Ranked transactions by a per-transaction metric (°http.time" or tshark) to isolate the few bad ones.[ ] Followed the slow stream and **attributed the delay to a layer#* using thetimestamp signature and the decision table.
I] Confirmed with a **second-vantage capture** and/or layer-specific tools.
- [ ] Remediated the attributed cause and *verified the metric moved**.
## See also
-  [tcp_wireshark_troubleshooting-md](tcp wireshark troubleshooting.nd) - worked Wireshark cases for network-side causes (policer loss, receiver window, RST)-
-  [tcp_congestion_control. md](tcp_congestion control.nd) - the congestion-control theory behind loss-driven slowdowns.

