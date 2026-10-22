"""Stage 4: the sea chart. Bakes map/terrain/colormap_water.dds for the mod.

The colour is an atlas tint of the water: shallows along every coast, the
continental shelf and the ocean floor from Natural Earth bathymetry, enclosed
seas a little lighter and warmer. Alpha carries the distance to the coast, which
the map shader uses for the shoreline; 255 is left to textures that have none.
"""
import json, struct, os, numpy as np, cv2, geo
from scipy import ndimage
from PIL import Image
V2='/Users/stefanbidzamov/Games/Victoria2/'
OUT=V2+'mod/katechon2010/'
W,H=geo.W,geo.H
SCALE=2               # the chart is stored at half the resolution of the province map
COAST_RANGE=64.0      # pixels of the province map covered by the alpha channel
ALPHA_MAX=250         # must match map_f.glsl
ISOBATHS=['K_200','J_1000','I_2000','H_3000','G_4000','F_5000']
# depth 0 (the coast) .. 1 (the ocean floor) -> colour, in the shader's working space
RAMP=[(0.00,(0.245,0.365,0.400)),(0.14,(0.200,0.315,0.360)),(0.32,(0.160,0.268,0.325)),
      (0.55,(0.130,0.228,0.288)),(0.78,(0.114,0.206,0.262)),(1.00,(0.106,0.194,0.248))]
BANDS=9               # tint steps between the coast and the ocean floor
WARM=(0.030,0.024,-0.012)   # added in full to the most enclosed water
# The game adds open water above and below the province map, where this texture
# does not reach. Towards its first and last rows the chart settles into a plain
# wash from SHALLOW at the coast to POLAR, which map_f.glsl carries on from there.
SHALLOW=RAMP[0][1]
POLAR=(0.114,0.206,0.262)
EDGE_ROWS=96

def smoothstep(a,b,x):
    t=np.clip((x-a)/(b-a),0.0,1.0)
    return t*t*(3.0-2.0*t)
def draw(img,geom):
    for poly in ([geom['coordinates']] if geom['type']=='Polygon' else geom['coordinates']):
        pts=[]
        for ring in poly:
            r=np.asarray(ring,dtype=float)
            ref=np.median(r[:,0])
            lon=((r[:,0]-ref+180)%360)-180+ref
            if lon.mean()<-170: lon=lon+360
            x,y=geo.to_pixel(np.clip(lon,-170,190),r[:,1])
            x=x+(lon-np.clip(lon,-170,190))/360*W
            pts.append(np.stack([x+W,y],1).round().astype(np.int32))
        cv2.fillPoly(img,pts,1)

land=np.load('province_ids.npy')>0
# distance from each water pixel to the coast; the map wraps east-west
PAD=256
coast=ndimage.distance_transform_edt(~np.pad(land,((0,0),(PAD,PAD)),mode='wrap'))[:,PAD:-PAD].astype(np.float32)
# how much land lies around: high in the Mediterranean or the Black Sea, zero in mid-ocean
small=cv2.resize(land.astype(np.float32),(W//8,H//8),interpolation=cv2.INTER_AREA)
enclosed=cv2.resize(ndimage.gaussian_filter(small,sigma=110/8,mode=('nearest','wrap')),(W,H),interpolation=cv2.INTER_LINEAR)
enclosed=smoothstep(0.40,0.68,enclosed)
# the ocean floor: how many isobaths lie above each pixel
floor=np.zeros((H,W),np.float32)
for name in ISOBATHS:
    img=np.zeros((H,W*3),np.uint8)
    for f in json.load(open('ne_10m_bathymetry_%s.geojson'%name))['features']:
        if f['geometry'] is not None: draw(img,f['geometry'])
    floor+=np.maximum(np.maximum(img[:,:W],img[:,W:2*W]),img[:,2*W:])
# the rows above and below the charted latitudes continue the last charted one
ys=np.nonzero(floor.max(axis=1)>0)[0]
floor[:ys[0]]=floor[ys[0]]; floor[ys[-1]+1:]=floor[ys[-1]]
floor=ndimage.gaussian_filter(floor/len(ISOBATHS),sigma=7,mode=('nearest','wrap'))   # wide enough that the outlines of the source do not show close to

depth=(1.0-np.exp(-coast/16.0))*(0.30+0.70*floor)*(1.0-0.30*enclosed)
# layer tints as on a printed chart: steps with a soft edge, laid over the smooth gradient
step=depth*BANDS
depth=0.65*depth+0.35*(np.floor(step)+smoothstep(0.25,0.75,step-np.floor(step)))/BANDS
stops=[s for s,_ in RAMP]
rgb=np.dstack([np.interp(depth,stops,[c[k] for _,c in RAMP]) for k in range(3)]).astype(np.float32)
rgb+=(enclosed*(1.0-0.5*depth))[...,None]*np.asarray(WARM,np.float32)
plain=np.asarray(SHALLOW,np.float32)+(1.0-np.exp(-coast/16.0))[...,None]*(np.asarray(POLAR,np.float32)-np.asarray(SHALLOW,np.float32))
rows=np.arange(H,dtype=np.float32)
charted=smoothstep(0.0,EDGE_ROWS,np.minimum(rows,H-1-rows))[:,None,None]
rgb=plain+charted*(rgb-plain)
# rivers are tinted with this texture too, so the land carries the colour of the shallows
alpha=np.sqrt(np.clip(coast/COAST_RANGE,0.0,1.0))*(ALPHA_MAX/255.0)
level=cv2.resize(np.dstack([rgb[...,::-1],alpha]),(W//SCALE,H//SCALE),interpolation=cv2.INTER_AREA)   # BGRA

Image.fromarray((np.clip(level[::-1,:,2::-1],0,1)*255+0.5).astype(np.uint8),'RGB').save('sea_check.png')   # north up, for the eye
mips=[]
while True:
    mips.append((np.clip(level,0,1)*255+0.5).astype(np.uint8).tobytes())
    h,w=level.shape[:2]
    if w==1 and h==1: break
    level=cv2.resize(level,(max(1,w//2),max(1,h//2)),interpolation=cv2.INTER_AREA).reshape(max(1,h//2),max(1,w//2),4)
w0,h0=W//SCALE,H//SCALE
header=struct.pack('<4s7I44x',b'DDS ',124,0x1|0x2|0x4|0x8|0x1000|0x20000,h0,w0,w0*4,0,len(mips))
header+=struct.pack('<2I4s5I',32,0x41,b'\0\0\0\0',32,0xff0000,0xff00,0xff,0xff000000)
header+=struct.pack('<5I',0x1000|0x8|0x400000,0,0,0,0)
os.makedirs(OUT+'map/terrain',exist_ok=True)
with open(OUT+'map/terrain/colormap_water.dds','wb') as f:
    f.write(header)
    for m in mips: f.write(m)
print('sea chart',w0,h0,'mips',len(mips),'depth mean over water',float(depth[~land].mean()))
