import math
import random
import re

# Haversine Distance
# This function computes the great-circle distance between two geographic
# coordinates using the Haversine formula. We use this as the link cost
# between routers based on their latitude and longitude.
def haversine(coord1, coord2):
    lat1, lon1 = coord1
    lat2, lon2 = coord2
    R = 6371.0

    # Convert degrees to radians
    φ1, φ2 = math.radians(lat1), math.radians(lat2)
    Δφ = math.radians(lat2 - lat1)
    Δλ = math.radians(lon2 - lon1)

    # Haversine formula
    a = math.sin(Δφ/2)**2 + math.cos(φ1)*math.cos(φ2)*math.sin(Δλ/2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return R * c

# Graph Builder
# We build a complete undirected graph of routers where each edge weight
# is the Haversine distance. Then we randomly drop a fraction T of edges
# to simulate network sparsity.
def build_graph(nodes, T):
    '''Construct adjacency list for a set of nodes:
      - nodes: dict mapping router_id -> (lat, lon)
      - T: fraction of edges to remove (0 <= T < 1)
    Returns a dict where graph[i][j] = cost.'''
    N = len(nodes)
    edges = []
    for i in nodes:
        for j in range(i+1, N+1):
            dist_ij = haversine(nodes[i], nodes[j])
            edges.append((i, j, dist_ij))

    # Determine how many edges to keep
    keep = int(len(edges) * (1 - T))
    kept = set(random.sample(edges, keep))
    graph = {i: {} for i in nodes}
    for i, j, w in kept:
        graph[i][j] = w
        graph[j][i] = w
    return graph

# Distance Vector Simulation 
# The DV simulation iteratively exchanges full distance vectors
# between neighbors until no router updates its table.
def simulate_distance_vector(graph):
    """
    Run synchronous Distance-Vector routing simulation:
      - graph: adjacency list with link costs
    Returns:
      total_msgs: total vectors sent
      rounds: number of rounds until convergence
      dist: dict of dicts with final distances
      next_hop: dict of dicts with first-hop entries
    """
    routers = list(graph.keys())
    N = len(routers)
    INF = float('inf')

    # Initialize dist and next_hop tables
    dist = {r: {d: INF for d in routers} for r in routers}
    next_hop = {r: {d: None for d in routers} for r in routers}
    for r in routers:
        dist[r][r] = 0
        for n, w in graph[r].items():
            dist[r][n] = w # direct neighbor cost
            next_hop[r][n] = n   # neighbor is next hop

    total_msgs = 0
    rounds = 0

    while True:
        rounds += 1
        updates = 0
        # Each router sends its vector to each neighbor
        for r in routers:
            for n, cost_rn in graph[r].items():
                total_msgs += 1
                # neighbor n receives r's vector
                for d in routers:
                    # Relaxation step: consider path r->d via r->n link
                    new_cost = cost_rn + dist[r][d]
                    if new_cost < dist[n][d]:
                        dist[n][d] = new_cost
                        next_hop[n][d] = next_hop[n][r] or r
                        updates += 1
        if updates == 0: # Terminate when no updates occur in a full round
            break

    return total_msgs, rounds, dist, next_hop

# Forwarding Table & Path Functions
def get_forwarding_table(next_hop, router_id):
    print(f"Forwarding table for Router {router_id}:")
    print("Destination -> Next hop")
    for dst, nh in sorted(next_hop[router_id].items()):
        if dst == router_id: continue
        print(f"  {dst:3d}      ->   {nh}")

def get_route(next_hop, src, dst):
    path = [src]
    cur = src
    while cur != dst:
        cur = next_hop[cur].get(dst)
        if cur is None:
            print(f"No route from {src} to {dst}")
            return []
        path.append(cur)
    print(f"Route: {' -> '.join(map(str,path))}")
    return path

if __name__ == "__main__":
    
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

    # add the final special node as ID = n+1
    next_id = len(nodes) + 1
    nodes[next_id] = (12.99151, 80.23362)
    T = 0.3  # drop 30% of edges

    G = build_graph(nodes, T)
    msgs, rnds, dist, next_hop = simulate_distance_vector(G)
    print(f"Total DV messages sent: {msgs}")
    print(f"Rounds until convergence: {rnds}")
    # # b) forwarding table
    # get_forwarding_table(next_hop, 1)
    # # c) path from 1 to 3
    # get_route(next_hop, 1, 3)
