<!-- docs/cpp/SA/system_qos_interview.md -->
Qos should be an explicit requirement-clarification area. If it is not mentioned, do not assume that all traffic has identical treatment.
Clarify:
-   Are all packets equal priority?
-   Are some domains, protocols, tunnels, or consumers more important?
-   Is bandwidth guaranteed per tunnel, consumer, group, or tenant?
-   Is there a maximum acceptable latency or tail latency?
-   Is packet loss acceptable during overload?
-   Should distributed traffic take priority over replicated traffic?
-   Should replicated traffic be reduced or dropped first?
-   Is fairness required between consumer groups?
-   Should one high-volume tunnel be prevented from starving others?
-   Are burst sizes and rate limits defined?
-   Are packets reordered when queues or priorities are used?
-   Are there separate limits for control-plane and data-plane traffic?
For VTB, the most important Qos decision may be how to handle overload between distributed and replicated traffic. For example:
-   Distributed forwarding is mandatory and receives higher priority.
-   Replicated forwarding is best effort.
-  When queues or buffer pools are exhausted, replica traffic is shed first.
-   A slow replicated consumer must not block the distributed path.
-   Each consumer or tunnel has bounded queues to prevent starvation.
Also clarify whether Qos applies to:
1.    **Ingress traffic** - controlling packets received by PVTB.
2.    **Internal processing** - scheduling workers and queues.
.3
**Egress traffic** - controlling transmission to consumers.
4. **Replication**- deciding which copies are generated or dropped.
I
If the requirement truly has no gos, document an explicit default, such as:
*   All traffic is best effort. No bandwidth, latency, ordering, or delivery guarantees are
*   provided. Queues are bounded, and packets are dropped according to the defined overload
*   policy.
That statement is preferable to leaving Qos unspecified, because the implementation still needs a defined behavior when traffic exceeds capacity.


