import math
import random
import heapq
import re

# Haversine Distance
# This function computes the great-circle distance between two geographic
# coordinates using the Haversine formula. We use this as the link cost
# between routers based on their latitude and longitude.

def haversine(coord1, coord2):
    """
    Compute great‐circle distance between two (lat, lon) pairs in km.
    Uses Haversine formula :contentReference[oaicite:7]{index=7}.
    """
    lat1, lon1 = coord1
    lat2, lon2 = coord2
    R = 6371.0  # Earth radius in km

    φ1, φ2 = math.radians(lat1), math.radians(lat2)
    Δφ = math.radians(lat2 - lat1)
    Δλ = math.radians(lon2 - lon1)

    a = math.sin(Δφ/2)**2 + math.cos(φ1)*math.cos(φ2)*math.sin(Δλ/2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return R * c

# Graph Builder
# We build a complete undirected graph of routers where each edge weight
# is the Haversine distance. Then we randomly drop a fraction T of edges
# to simulate network sparsity.

def build_graph(nodes, T):
    """
    nodes: dict {id: (lat, lon)}
    T: fraction of edges to drop (0 <= T < 1)
    Returns adjacency list: {i: {j: weight, ...}, ...}
    """
    N = len(nodes)
    # Initial complete graph
    edges = []
    for i in nodes:
        for j in range(i+1, N+1):
            dist = haversine(nodes[i], nodes[j])
            edges.append( (i, j, dist) )

    # Drop fraction T of edges at random :contentReference[oaicite:8]{index=8}
    keep_count = int(len(edges) * (1 - T))
    kept = set(random.sample(edges, keep_count))

    # Build adjacency
    graph = {i: {} for i in nodes}
    for i, j, w in kept:
        graph[i][j] = w
        graph[j][i] = w
    return graph

#Flooding Simulation
# We simulate the flooding of Link-State Advertisements (LSAs) in synchronous rounds.
# Each router initially knows only its own ID. In each round, routers send new LSAs
# to all neighbors, and we count messages until every router knows all IDs.

def simulate_link_state(graph):
    '''Simulate synchronous flooding of LSAs:
      - graph: adjacency list of routers
    Returns:
      total_msgs: total LSA messages sent during flooding
      rounds: number of rounds until all routers have complete LSDB
      lsdb: dict mapping router_id -> set of known router IDs'''
    routers = list(graph.keys())
    N = len(routers)

    # Initialize each router's LSDB and its inbox of newly received LSAs
    lsdb = {r: {r} for r in routers} # each router knows itself
    inbox = {r: {r} for r in routers} # newly to send LSAs

    total_msgs = 0
    rounds = 0

    while True:
        rounds += 1
        new_inbox = {r: set() for r in routers}
        for r in routers:
            # router r sends all LSAs in inbox[r] to every neighbor
            nbrs = graph[r].keys()
            total_msgs += len(inbox[r]) * len(nbrs) # Count messages: one per LSA per neighbor
            for lsa in inbox[r]:
                for nb in nbrs:
                    if lsa not in lsdb[nb]: # If neighbor hasn't seen this LSA, add it
                        lsdb[nb].add(lsa)
                        new_inbox[nb].add(lsa)

        # # check convergence
        # if all(len(lsdb[r]) == N for r in routers):
        #     break
        # inbox = new_inbox

       # 1) Normal convergence: all routers know every LSA
        if all(len(lsdb[r]) == N for r in routers):
            break

        # 2) Deadlock check: no new LSAs arrived but LSDBs are still incomplete
        if all(len(new_inbox[r]) == 0 for r in routers):
            print("Warning: network is disconnected; flooding has stalled.")
            break

        # 3) Prepare for next round
        inbox = new_inbox

    return total_msgs, rounds, lsdb

# Dijkstra & Forwarding Tables
# After flooding, each router has full topology, so we run Dijkstra
# from each router to compute shortest paths and build forwarding tables.

def dijkstra(graph, src):
    """
    Compute shortest paths from src to all other nodes using Dijkstra's algorithm.
    Returns:
      dist: dict mapping node -> min cost from src
      prev: dict mapping node -> predecessor on shortest path
    """
    dist = {v: math.inf for v in graph}
    prev = {v: None for v in graph}
    dist[src] = 0
    pq = [(0, src)]

    while pq:
        d, u = heapq.heappop(pq)
        if d > dist[u]:
            continue
        for v, w in graph[u].items():
            alt = d + w
            if alt < dist[v]:
                dist[v], prev[v] = alt, u
                heapq.heappush(pq, (alt, v))
    return dist, prev

# Build Forwarding Tables
# For each router, we use the predecessor map to determine the first hop
# on the shortest path to each destination.
def build_forwarding_tables(graph):
    """
    For each router r in the graph:
      - Run Dijkstra from r
      - Backtrack from each dest to find the first hop neighbor
    Returns a dict mapping r -> {dest: next_hop, ...}
    """
    fwd_table = {}
    for r in graph:
        _, prev = dijkstra(graph, r)
        table = {}
        for dest in graph:
            if dest == r: continue
            # backtrack from dest to find next hop
            hop = dest
            while prev[hop] != r:
                hop = prev[hop]
                if hop is None:
                    break
            table[dest] = hop
        fwd_table[r] = table
    return fwd_table

# Print Forwarding Table (Part b)
def get_forwarding_table(fwd_table, router_id):
    """
    Print the forwarding table for a given router:
      Destination -> Next Hop
    """
    print(f"Forwarding table for Router {router_id}:")
    print("Destination -> Next hop")
    for dst, nh in sorted(fwd_table[router_id].items()):
        print(f"  {dst:3d}      ->   {nh}")

# Retrieve Route (Part c
def get_route(fwd_table, src, dst):
    """
    Reconstruct the end-to-end path from src to dst by following next hops.
    Returns the path as a list of router IDs.
    """
    path = [src]
    cur = src
    while cur != dst:
        cur = fwd_table[cur].get(dst)
        if cur is None:
            print(f"No route from {src} to {dst}")
            return []
        path.append(cur)
    print(f"Route from {src} to {dst}: {' -> '.join(map(str,path))}")
    return path

# === Example Usage ===

if __name__ == "__main__":
    
    #Choose T, e.g., T = 0.3
    
    T = 0.3
    # where we’ll store id → (lat, lon)
    nodes = {}

    with open("traceroute_ip_cache.txt") as f:
        for idx, line in enumerate(f, start=1):
            line = line.strip()
            if not line or 'loc:' not in line:
                continue

            # regex to pull out the two floats after "loc:"
            m = re.search(r'loc:\s*([-\d\.]+)\s*,\s*([-\d\.]+)', line)
            if not m:
                raise ValueError(f"Could not parse loc from line {idx}: {line}")

            lat, lon = float(m.group(1)), float(m.group(2))
            nodes[idx] = (lat, lon)

    # add the final special node as ID = n+1, this is the local node: with location 12.99151, 80.23362, IIT Madras
    next_id = len(nodes) + 1
    nodes[next_id] = (12.99151, 80.23362)

    #Build graph, simulate flooding, build tables
    G = build_graph(nodes, T)
    msgs, rnds, lsdb = simulate_link_state(G)
    print(f"Total LSAs sent: {msgs}")
    print(f"Rounds until convergence: {rnds}")
    print(f"LSDB entries per router: {len(next(iter(lsdb.values())))}")

    fwd = build_forwarding_tables(G)
    # get_forwarding_table(fwd, 245)
    # get_route(fwd, 1, 4)
