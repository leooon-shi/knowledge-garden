<!-- pertormance/network/tcp_wireshark_troubleshooting.md -->

# TCP Troubleshooting with Wireshark (Case Studies)

Three real cases that diagnose TCP throughput problems with Wireshark, plus the analysis techniques used. For the underlying theory, see Itcp_congestion_control. md](tcp_congestion control.nd).
A useful mental model throughout: **transmission speed = window / round-trip
time**, where "window" means different things in different situations. Let Cw be the sender's own congestion window and RN the peer's receive window:
-   **RW < C** → the effective window is RV. The peer's receive window is smaller than the sender's congestion window, which usually means little or no congestion occurred (so cund grew large). The speed celling is set by the peer's receive window.
-  **RW> C**→ the effective window is CW. The peer's receive window exceeds the sender's congestion window, which usually means congestion was encountered, so cund adapted downward and became relatively small.
#* Case 1: Diagnosing slow TCP transfer speed (SCP over a dedicated line throttled to ~280 KB/s)
*  *Symptom.** Copying a file with SCP from a public cloud host to a local machine is too slow to meet the dedicated-line standard -throughput hovers at roughly 100-200 KB/s, with a few brief stretches around 230 KB/s. CPU, disk, and the application are all idle, so the bottleneck is in the network path, not the endpoints.
*  *Why this is a good "transmission speed" case.** Speed is governed by
*  speed = window / RTT* (see the mental model at the top of this document). When a
transfer is slow, exactly one of three things is true, and the goal of the analysis is to decide whích:
1.    The **RTT is large** (long path / queuing delay) - speed is latency-bound.
2.    The *window is small because the receiver advertises a small receive window**(RH < CH) - speed is receiver-bound (see Case 3 for that pattern).
3.    The *window is small because the sender's congestion window keeps collapsing on loss** (RW > CW, cnd adapts downward) - speed is loss/congestion-bound.
This case turns out to be #3: something in the path is dropping packets, the sender reads that as congestion, and congestion control holds cwnd - and therefore throughput - down.
## Step 1- Confirm the speed and the direction
Before anything else, confirm the measured rate and which side is the sender.
This is a client (Shanghai) pulling from an SSH/SCP server (Beijing), so the bulk data flows **server(port 22) → client**, and the client's packets are mostly pure ACKS. Getting the direction right matters for every graph below; analyzing the ACK direction instead would show the wrong stream.
In Wireshark, **Statistics → Conversations→ TCP** gives per-flow byte counts and duration for a quick sanity check of the rate. Alternatively, **Statistics → I/0 Graph** with the Y axis in Bytes visualizes the throughput over time (see Case 3 for the Bytes/s conversion).
#*# Step 2- Read the Expert Information
**Analyze → Expert Information** classifies the anomalies Wireshark detected:
-  **Warning** level- two kinds, both fundamentally reordering symptoms:
-   *Out-of-Order* - a segment arrives with a lower sequence number than one already seen.
-   *Previous segment not captured* - a gap in the capture / sequence space, usually because a segment was lost or not captured.
**Note** level - four kinds, all retransmission/ACK related:
-   "Spurious retransmission* - already-acknowledged data retransmitted again (the original was not actually lost; the retransmit was unnecessary).
-   *Fast retransmission* - a retransmit triggered by 3+ duplicate ACKs, without waiting for the retransmission timeout.
-   *Retransmission* - triggered by the retransmission timer expiring: the classic timeout retransmission.
-   *DupLicate ACK* - multiple segments carrying the same acknowledgment number; duplicate ACks are what drive* fast retransmission.
-   **Chat** level - *TCP Window Update™: here the client advertising changes in its receive window to the server. This is normal and, importantly, tells us the receiver is **not** starving the sender (so this is not a receive-window bottleneck - ruling out pattern #2 above).
The presence of retransmissions and duplicate ACKs - with a healthy receive window - already points at **packet loss driving congestion control**, not a slow receiver.
### Step 3-Look at the time-sequence graph (is slow start visible?)
Open **Statistics → TCP Stream Graphs → Time Sequence (Stevens) ** for the serversclient flow. The Yaxis is the sequence number, the x axis is time, so the
**slope is the throughput**.
-   Overall the slope is fairly steady - despite the reordering and retransmissions, the average speed is roughly constant (the "rate 11mst" hint).
-   Zooming into the first ~10 seconds exposes the structure: **steep segments are slow start** (cand growing fast, high slope); **gentle segments are congestion avoidance** (cund growing by ~1 MSS/RTT, low slope).
Key observation: **slow start is not a one-time startup event**. Each time congestion avoidance detects loss, TCP drops back toward slow start with a new (Lower) ssthresh, then c11mbs again. Repeated loss repeats this saw-tooth for the


whole transfer - which is exactly what caps the average throughput.
*## Step 4 - Inspect the initial (cold) slow start
Zoom further into the very first slow start (the initial one is also called
**cold start**). Marking each RTT shows that within each RTT, port 22 sends a burst of segments to the client (each segment's sequence number is one point), and **each RTT sends more segments than the previous one** - the hallmark of slow start's growth.
Note that growth is not necessarily "double per RTT"; it can be less, because TCP increases cand by one MSS per *data-acknowledging* ACK, and delayed/interval ACKs mean fewer such ACks per RTT (see
[tcp_congestion_control. md](tcp congestion control. md)).
*** Step 5 - Spot the retransmissions that trigger congestion avoidance
Some points sit *below* their predecessors on the Y axis. Since Y is the sequence number, a lower point means a *smaller* sequence number - an old segment sent again, i.e. a *retransmission**, Selecting the lowest such point locates segment 201, which the main pane confirms is a **TCP Fast Retransmission**, That retransmission is the moment the congestion-control mechanism sensed loss and dropped from slow start into congestion avoidance - halving cund and flattening the slope.
This is the direct, visual evidence tying the slow throughput to loss-driven congestion control: every loss event resets the climb.
## Step 6 - Distinguish a rate limiter from genuine network congestion
Loss can come from real congestion (buffers overflowing on a busy path) or from a
**policer** (a device configured to drop anything above a fixed byte rate). Clues that point to a policer rather than organic congestion:
-   The average throughput sits at a *suspiciously flat ceiling** (here ~200 KB/s)regardless of time of day or path load.
-   Loss appears **as soon as the sender tries to exceed that ceiling**, on an otherwise lightly loaded path, rather than only at peak times.
-  The receive window is healthy Window Updates are normal), so the receiver is not the limit.
All three hold here, so e loss is being induced deliberately.
### Conclusion
The root cause was a **misconfigured rate limit (traffic policer) on the dedicated line**. The limit is enforced by network hardware that permits only a fixed number of bytes per unit time and **drops** the excess. To the TCP sender, dropped packets are* "congestion," so congestion control proactively backs off into congestion avoidance (and repeatedly re-enters slow start on each loss) to protect throughput and avoid large-scale loss. The net effect is that TCP
*  *self-limits its rate** to just under the policer's threshold - producing exactly the throttled ~200 KB/s observed.
*  *Remediation.** Fix or raise the policer's rate on the network device; TCP will then grow cund past the old ceiling. To confirm the fix, re-run the transfer and re-check the Stevens graph: the saw-tooth should give way to a sustained steep slope, the Expert Information should show far fewer retransmissions/duplicate ACKS, and the I/0 Graph throughput should rise toward the line's true capacity.
*  *Takeaways，**
-   Start from "speed = window / RTT*and decide whether you are latency-bound,receiver-window-bound, or congestion-bound before touching anything.
-   Healthy TCP Window Updates rule out a receiver bottleneck; retransmissions plus duplicate ACKs point at loss-driven congestion control.
-   On a Stevens time-sequence graph, slope - throughput, steep - slow start,gentle = congestion avoidance, and points below the trend - retransmissions.
-   A flat throughput ceiling with loss appearing exactly at that ceiling is the signature of a traffic policer, not organic congestion.
*  * Case 2: Fair-sharing bandwidth with tc, and the receiver "data retention" effect
*  *Scenario,** Multiple clients connect to the same server, When one client's upload or download consumes most of the bandwidth, new clients cannot connect.
*  *Fix,** Use the tc' tool on the server to do flow control - dividing bandwidth evenly by client count and capping per-IP bandwidth.
*  ## How tc enforces the 11mit (via induced congestion)
*  te works by adjusting the qdisc send-buffer queue: data exceeding the queue is dropped. The sender detects the drops as "congestion" and proactively slows down, which produces the rate-11mlting effect. (This is the same mechanism as Case 1's hardware policer - loss is the universal "slow down" signal to a TCP sender-buthere it 1s applied deliberately and fairly per client/IP.)
at why does the receiver acknowledge only part of the data? ("data retention")
The data the receiver acknowledges is the data the **user process has read** from the kernel receive buffer. Data retention means the receive buffer is being written faster than it is being read. Three factors determine this:
1. **Receive-buffer / receive-window size.** The receive window may be too small
- e.g.
•/proc/sys/net/ipv4/tcp window scaling set to e caps the window at




65535 bytes.
retention" effect
2. **Read speed.** The user process may read too slowly; it may also fail to read everything in one call and need multiple reads, depending on its logic (worst case: it sleeps after each recv").
**Write speed into the receive buffer.** Maintained by the TCP stack; depends on the receive-window size and the read speed.
### Worked example (t1-t4)
1.    At t1, B tells A: "my receive window is 1000 bytes."
2.    At t2, A receives this and sends 1000 bytes - all now in-flight.
3.    At t3, B receives the 1000 bytes; the receive window (buffer) is fully used, so Wireshark reports Window Ful1." B then sends an ACK confirming 360 bytes.
4.    At t4, A receives B's ACK for 300 bytes and computes in-flight • 760 bytes, so A can send at most 300 more bytes.
This is the origin of the refined speed formula used in Case 3: when the receiver retains data, throughput tracks **acknowledged** data, not merely the window.
#*# The receiver's packet-processing flow (the pool analogy)
Think of the buffers (Ring Buffer and Receive Buffer) as a pool: packets arriving are like a faucet adding water; the application reading data is 11ke a faucet draining it. The differing rates of the two faucets cause the pool level (buffer occupancy) to vary.
"Data retention" is water accumulating in the pool - the drain is too slow, 1.e. the application is not reading the data promptly.
A primary reason the o5 provides buffers is to use space* to absorb the *time" mismatch between the two rates - "trading space for time."
### Conclusion & takeaways
trachieves fair bandwidth sharing by **inducing loss** on over-1imit traffic; TCP's own congestion response does the actual slowing.
-   A receiver acknowledges only what the **application has read"*, not what has arrived - so a slow consumer causes "data retention": a full receive buffer with little forward progress.
-  If a transfer is slow and the receive window is pinned full, look at the
**application's read rate* and the **receive-buffer size** before suspecting the network.
#* Case 3: Is "TCP Window Full" the cause of a slow cross-site copy? (receiver receive-window bottleneck)
*#*Reading the ExpertInformation
There are 69 Warning-level packets: "TCP Window specified by the receiver is now completely Full." Breaking that down:
1.    **TCP Window** - TCP has three windows: send, receive, and congestion. When unqualified, "TCP Window" means the receive window.
2.    **specified by the receiver** - this window belongs to the receiver, i.e. it is the receive window.
3.    **is now completely Full** - when in-flight data equals the receive-window size, the window is "full."
### Interpreting TCP Window Full"
This is information Wireshark "derives, not a field in the packet. Note the square brackets around TCP Window Full": Wireshark's own analysis is bracketed, while actual TCP header fields are not.
When in-flight data (Bytes in flight) equals the receive-window size, Wireshark reports TCP Window Full, In the example, segment 224's Window size shows 19200, not the 112000 of Bytes in flight - why? Because if the in-flight data's sender is A and receiver is B, the "full" window is **B's** receive window, not A's.
Segment 224 is A's packet, so it does not carry B's receive window.
To find B's receive window; since this is an SCP transfer, A is the client (port 38979) and B is the server (port 22), Find the most recent packet from B (source port 22) *before* this TCP Window Full packet; it carries B's latest receive window. That packet's receive window is 112000 - exactly equal to segment 224's Bytes in flight of 112000,
The full sequence:
1.    B sends segment 222 to A carrying B's receive window of 312000 bytes, As a pure ACK, it has no TCP payload and no in-flight data.
2.    The segment reaches A and enters A's receive buffer.
3.    From segment 222, A learns B's receive window is 212000 bytes; having enough queued data, A chooses to f111 the window - sending 212000 bytes at once.
4.    A sends those 112000 bytes as segment 224, which also carries A's own recedve window of 19200 bytes. Since this transfer is mainly A → B, B's packets to A are essentially pure ACKs with zero payload. (In the extreme, even if A'S receive window were e, as long as B's replies carry no payload they can keep communicating.)
5.    Segment 224 reaches B and exactly f111s B's 112000-byte recedve window.Wireshark reads B's receive window from segment 222 and the in-flight bytes from segment 224; since they are equal, it flags TCP Window Full (shown on segment 224)-
### Verifying TCP Window Full yourself
**Next Sequence Number (NextSea** points to the position fust past the last byte



your Selt
of the current TCP segment (not including that byte itself:
NextSeq = Seg + Len
i.e. the sequence number of the next TCP packet. compute in-flight bytes manually:
Bytes_in_flight = latest Nextseq - Latest_ACK_from_receiver
Method:
1.    Find the Nextseq of the most recently sent segment - call it X.
2.    Find the most recent ACK received before that send - cal1 its ACK number y.
3.    X - Y gives the in-flight bytes.
This is useful for assessing performance. For example, when the window has only 0.1% space left and no TCP Window Full is reported, the sender need not send a
latency and keeps efficiency up.
tiny segment just for that 0.1%; waiting for the window to grow avoids adding
# Effect on throughput
TCP Window Full occurs when the receiver's receive window is smaller than the sender*s sending capability, so the bottleneck is the receiver - and it does affect throughput.
##* Two ways to view transfer speed in Wireshark
1.    **1/0 Graph.** Select the "All Bytes" metric and set the Y axis to Bytes.Adjust the Interval for granularity. With a 100 ms interval, the Y axis is Bytes/100 ms, so multiply by 10 to get Bytes/s. (Also compare the Interval and SMA period settings if your graph differs.)
2.    **TCP Stream Graphs → Window Scaling.** Because the receive, congestion, and send windows all start small and grow gradually, whichever grows fastest, the other two are constrained. When the receive window becomes the bottleneck, TCP Window Full appears.
Two key points for the Window Scaling graph:
1.    **Get the data direction right.** This transfer sends from client to SSH server, so confirm it is from a high port to port 22. Reversed, the graph shows the server's ACks instead of the transfer you want to analyze.
2.    **Locate TCP Window Full.** The Receive Window is "stair-stepped," holding at a plateau briefly after each change; Bytes Out (Bytes in flight) may touch that plateau - each touch is a TCP Window Full. In the graph, the blue line is Bytes Out and the green line is the Receive Window; click where they meet to locate the events.
## Verifying the throughput formula
Add custom columns in Wireshark to compare data:
1.    Acknowledgement Number
2.    Next Sequence Number
3.    Calculated Window Size
4.    Bytes in flight
Filter the sender's segments (source port 38979) to inspect Bytes in flight:
-112000 bytes early on, with Calculated Window Size growing to 445312 bytes mid-transfer and 863800 bytes near the end. Averaging the three phases gives ~400 KE; dividing by an RTT of 0.029 s yields 400 KB / 0.029 s • 13790 KB/s - far above the 300-400 KB/s seen in the I/0 Graph.
Is all of the Bytes-in-flight really "on the wire" both ways the whole time?
Consider t1-t, where t2-4 is one round trip: the data actually transferred in that round trip is 300 bytes, not 1000, So velocity - acknowledged_data / RTT.
The window / RTT" form applies only when there is no data retention - 1.e. when in-flight data is acknowledged promptly on arrival:
1.    B advertises a receive window of 1000.
2.    A sends 1000 bytes; B's receive window is now full.
3.    B acknowledges 300 bytes,
4.    A's in-flight bytes drop from 1000 to 700(300 Just acknowledged),
### Why retention occurs
In one interval the server (B) acknowledged only 22400 bytes (2495254 - 1472854), while the in-flight data stayed high at ~660 KB. So the data actually transferred is 22400 bytes, not the "Inflated" 660 KB. The in-flight count does not shrink because as soon as 22400 bytes are acknowledged, the client immediately sends the same amount again, maintaining the in-flight size.
### Conclusion
1.    Wireshark reports TCP Window Full because one end's in-flight data equals the other end's receive window.
2.    TCP'S Next Sequence Number equals sequence number plus segment length:
•NextSeg = Seq + Len.
3. In-flight bytes can be computed manually:
*Bytes_in_flight - latest_NextSeq - latest_ACK_from_receiver'.



4. Method to compute in-flight bytes:
*    Find the NextSeq of the most recently sent segment - X.
*    Find the most recent ACK received before that send - Y.
*    X - Ygives the in-flight bytes.
5. TCP Window Full does affect transfer speed.
When the receiver exhibits **data retention**, the formula "speed - window / RTT"
breaks down and should be refined to **speed = acknowledged data / RTT**, which
also handles the retention case.
### Takeaways
-   "TCP Window Full" is a Wireshark-*derived* flag (note the square brackets), not a TCP header field; it fires when one end's in-flight data equals the other end's advertised receive window.
-   A persistently full receive window means the **receiver is the bottleneck** - its application is not draining the buffer fast enough (ties back to Case 2's data retention).
-   Compute in-flight bytes yourself with •Bytes_in_flight = NextSeq - ACK to judge how close you really are to the window 11mit.
-   Measure throughput with the **acknowledged-data** formula, not window/RTT, when retention is present otherwise you overestimate the real transfer rate.
san Wireshark techniques used
1.
2.
*  *Statistics → I/0 Graph** - directly visualizes transfer speed.
*  *Statistics → TCP Stream Graphs → Window Scaling** - visualizes the TCP
Window Full history curve.
#* Case 4: "Connection reset by peer" (diagnosing a TCP RST by correlating app logs with the capture)
**Symptom.** The application layer reports a connection was reset - e.g. an Nginx error log line like:
•**text
2815/12/01 15:49:48 [info] 20521#0: *55077498 rec() failed
(104: Connection reset by peer) while sending to client, client: 10.255.252.31, server: manager.example.com, request: POST/WebPageAlipay/weixin/notify_url.htm HTTP/1.1",
upstream: "http://10.4.36.207:8080/WebPageAlipay/weixin/notify_url.htm"s host: "manager.example.com"
...
Three questions typically need answering:
1.    Does this reset affect our business - did the transaction actually succeed?
2.    At what stage did the reset happen - is it a normal TCP teardown or not?
3.    What can we do to avoid it?
### Why the application layer alone cannot tell you
Network layering means each layer minds its own business, which is a strength - and here a weakness. The application only knows what the oS told it: "your connection was reset." *why* it was reset, the application cannot see. Only the os knows, and the ojust handles it, increments an internal reset counter by one, and does **not** record the before/after context of the reset.
What almost certainly happened is that the **peer replied with a TCP RST** (the
"reset®), terminating the connection. To find the cause you must correlate the application-layer information with the transport- and network-layer information beneath it.
**Application-layer information**
-   Application logs - success logs, error logs, etc.
-   Application performance data - RPS (requests per second), transaction time, etc.
-   Application payload - e.g. HTTP request/response headers and bodies.
**Transport / network-layer information**
-   *Transport (TCP);* Sequence Number, Acknowledgement Number, MSS, Recelve Window, Congestion Window, latency, duplicate ACKs (DupAck), Selective ACK, retransmissions, packet loss, etc.
-   *Network (IP):* TTL, MTU, hop count, routing tables, etc.
*   Terminology: **upstream**means the back-end server, In gateways/reverse
*   proxies such as Nginx and Envoy, "upstream" refers to the back-end side.
#** Step 1 - Filter the capture down to the RST
start with address filters to isolate the peer, then add the RST flag.
Address filters:
•*text
ip.addr eq my ip
I
my_1p
* packets whose source OR destination IP 1s my_$p
# packets whose source IP 1s my -p
# packets whose destination IP is my ip
RST filter - "tcp.flags" exposes the TCP flag bits (SYN, ACK, FIN, PSH, RST, -):
•*•text
tcp.flags.reset eq 1

Combined (RSTS involving the peer):
***text
3p.addr eg 18.255.252.31 and tcp.flags.reset eq 1
From any one packet, get the whole flow: right-click **Follow → TCP Stream** to see every packet in the TCP stream it belongs to.
### Step 2 - Know the stages so you can classify the RST
How the application interacts with the kernel TCP stack:
***text
client (active open)
socket()
connect()
Server (passive open)
socket()
bind()
listen()
accept ()
The server's user-space program obtains a usable connection from the return of accept), and accept) returns successfully only after the three-way handshake completes. So an RST can land in three distinct stages - **handshake**,
**established (data transfer)**, or **teardown (the four-way wave)** - and which stage it is changes the meaning. Narrow It down by excluding the handshake/wave boundary packets (where seq/ack" are still 1):
Exclude handshake-stage RSTS:
***text
ip.addr eq 10.255.252.31 and tcp.flags.reset eq 1
..and i(tcp.seg eg 1 and tcp.ack eq 1)
Exclude teardown-stage RSTs:
**text
ip.addr eq 10.255.252.31 and tcp.flags.reset eq 1
..and i(tcp.seq eq 1 or tcp.ack eq 1)
A RST during the handshake often means the port is closed / not listening or the backlog overflowed; a RST during data transfer or teardown usually means the peer aborted an in-progress or half-closed connection.
*  **Note - relative vs absolute sequence numbers.** These •tcp.seq eq 1° /
*   "tcp.ack eq 1 filters rely on Wireshark's **relative** sequence numbers, which
*   start at 1 for each flow the default: *Preferences → Protocols → TCP →
"Relative sequence numbers"*). They are a practical heuristic for isolating the
*   handshake/teardown boundary, not an exact rule - a data-stage packet can also
*   happen to carry a relative seq/ack of 1 in edge cases. If relative sequence numbers are turned off, these filters match the raw ISN instead and will not
*   work as intended. When in doubt, confirm the stage by following the stream
*   (**Follow→TCP Stream**) and reading the actual SYN/FIN/RST flags rather than
*   trusting the seq/ack heuristic alone.
### Step 3- Correlate the RST with the application transaction
Tie the packets to the business transaction two ways:
1, **By payload,** The packets carrying the request should contain request-related data (strings, numbers). This only works if the payload is not specially encoded - with TLS, the on-the-wire bytes differ completely from the decoded application data, so payload matching won't work.
2, **By time,** The packets' timestamps should line up with the log timestamps.
Filter to the time window around the logged error with "frame.time":
***text
frame, time> "decel, 291515;49:48 and frame, time <- "dec09, 2015 15:49:49
Then combine the time window with the address+ RST filter (excluding handshake/wave) for a precise hit:
**text
frame.time "dece2, 2815 15:49:48 and frame,time<- dec03, 2015 15:49:49" and ip.addr eq10.255.252.31 and tcp.flags.reset eq & and 1(tcp.seg eq 1 or tcp.ack eq )
This pins the exact RST packet that corresponds to the Nginx
* rec() failed (184: Connection reset by peer) 10g 11ne, so you can read its sequence/ack numbers and see what data preceded it answering whether the transaction's request/response had actually completed before the reset.
I
### Step 4- Account for every hop in the path
A reset is not necessarily sent by the application peer. The path has many elements- client, server, intermediate routers/switches, firewalls, Load balancers, and reverse proxies - and **any** of them can inject a RST:
- A **firewall** or security device dropping a connection it deems idle or disallowed, sending a RST to both ends.


- A
**load balancer / reverse proxy** resetting an idle or over-limit connection, or resetting the client side when its upstream connection fails.
The **server application** closing abruptly (crash, or close() with unread data in its receive buffer, which forces a RST instead of a clean FIN).
- Writing to a socket the peer has **already closed** (a RST follows the peer's
FIN)-
- **'SO_LINGER" with a zero timeout**, which makes close() emit a RST rather than a FIN.
Because each hop can be the culprit, capture at more than one point when possible (e-g- at the client, at the LB, and at the upstream) and compare: the hop where the RST first appears is the one that originated it. Matching the RST's IP TTL against the expected hop count is another clue to where it came from.
### Conclusion & takeaways
-   "Connection reset by peer" (errno 104, "ECONNRESET") means a **TCP RST** tore the connection down; the application layer records only the symptom, so you must go to the capture for the cause.
-   Diagnose by **correlating layers**: match the application log's timestamp and payload to the exact RST packet via "frame.time" + "ip.addr +
*  tcp.flags.reset eg 1',
*  *Classify the stage** (handshake / data / teardown) by excluding the
*  seq/ack == 1° boundary packets - the stage tells you whether it is a normal abort or a real failure,
-**Suspect every hop*, not just the application peer - firewalls, LBs, and reverse proxies commonly inject RSTs, Multi-point capture localizes the origin.
- To **avoid*spurious resets; align idle-timeout settings across client, proxy. and upstream; enable keep-alives where appropriate; avoid "SO_LINGER(Q) unless an abortive close is intended; and ensure the peer fully drains its receive buffer before closing.
### Wireshark filters used
***text
ip.addr eq <ip>
tcp.flags.reset eq 1
(tcp-seq eq 1 and tcp.ack eq 1)
! (tcp.seq eq 1 or tcp.ack eq 1)
frame.time > "..." and frame.time <-"..."
* by address (src on dst)
# RST packets only
# exclude handshake-stage RSTS
# exclude teardown-stage RSTS
* by time window


