import matplotlib.pyplot as plt
from mpl_toolkits.basemap import Basemap

# Example list of hops: (IP, City, Country, Latitude, Longitude)
hops = [
    ("122.184.77.145", "Chennai", "IN", 13.0827, 80.2707),
    ("116.119.109.124", "London", "GB", 51.5074, -0.1278),
    ("116.119.119.184", "London", "GB", 51.5074, -0.1278),
    ("182.79.237.206", "London", "GB", 51.5074, -0.1278),
    # Add more hops as needed
]

# Prepare the map
fig = plt.figure(figsize=(12, 12))
m = Basemap(
    projection='merc',
    llcrnrlat=-40, urcrnrlat=60,     # Latitude range: southern Africa to northern India
    llcrnrlon=-25, urcrnrlon=110,    # Longitude range: West Africa to Southeast Asia
    resolution='l'
)

m.drawcoastlines()
m.drawcountries()
m.fillcontinents(color='white', lake_color='#d2dff7')
m.drawmapboundary(fill_color='#d2dff7')
m.drawparallels(range(-90, 91, 30), labels=[1, 0, 0, 0])
m.drawmeridians(range(-180, 181, 60), labels=[0, 0, 0, 1])

# Plot routers and paths
prev_x, prev_y = None, None
for ip, city, country, lat, lon in hops:
    x, y = m(lon, lat)
    m.plot(x, y, 'ro', markersize=6)

    if prev_x is not None:
        m.plot([prev_x, x], [prev_y, y], 'b-', linewidth=2)

    prev_x, prev_y = x, y

plt.title("Traceroute Path Visualization")
plt.tight_layout()
plt.show()