"""Stage 6: the relief of the land. Bakes map/terrain/colormap.dds for the mod.

The colour is the base game's terrain colormap, untouched. Alpha carries the
shading of the real ground, worked out here from elevations (ETOPO2v2, NOAA,
public domain) brought onto the game map through the same warp as the
borders: half is level country, below it slopes turned from the light, above
it slopes facing it. 255 is left to colormaps that carry none; map_f.glsl
reads it.
"""
import os, struct, zipfile, urllib.request, numpy as np, cv2, geo
from scipy.interpolate import griddata
from PIL import Image
Image.MAX_IMAGE_PIXELS=None
V2='/Users/stefanbidzamov/Games/Victoria2/'
OUT=V2+'mod/katechon2010/'
W,H=geo.W,geo.H
ALPHA_MAX=250         # must match map_f.glsl
SRC='ETOPO2v2c_i2_LSB'
SW,SH=10800,5400      # two minutes of arc to the cell, from 90N 180W, rows running south
if not os.path.exists(SRC+'.bin'):
    if not os.path.exists(SRC+'.zip'):
        req=urllib.request.Request('https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2/ETOPO2v2-2006/ETOPO2v2c/raw_binary/%s.zip'%SRC,headers={'User-Agent':'katechon-build'})
        open(SRC+'.zip','wb').write(urllib.request.urlopen(req,timeout=600).read())
    with zipfile.ZipFile(SRC+'.zip') as z:
        name=[n for n in z.namelist() if n.lower().endswith('.bin')][0]
        open(SRC+'.bin','wb').write(z.read(name))
height=np.maximum(np.fromfile(SRC+'.bin',dtype='<i2').reshape(SH,SW).astype(np.float32),0.0)   # metres; the sea is level

# where on the earth each pixel of the game map lies: the warp, turned round
lons=np.arange(-170,190.01,1.0); lats=np.arange(89,-60.01,-1.0)
LO,LA=np.meshgrid(lons,lats)
px,py=geo.to_pixel(LO.ravel(),LA.ravel())
pts=np.concatenate([np.stack([px+k*W,py],1) for k in (-1,0,1)])
lo=np.concatenate([LO.ravel()+k*360 for k in (-1,0,1)]); la=np.tile(LA.ravel(),3)
w,h=W,H
gx,gy=np.meshgrid((np.arange(w//8)+0.5)*8,(np.arange(h//8)+0.5)*8)
lon=griddata(pts,lo,(gx,gy),method='linear'); lat=griddata(pts,la,(gx,gy),method='linear')
lon=np.where(np.isnan(lon),griddata(pts,lo,(gx,gy),method='nearest'),lon); lat=np.where(np.isnan(lat),griddata(pts,la,(gx,gy),method='nearest'),lat)
lon=cv2.resize(lon.astype(np.float32),(w,h),interpolation=cv2.INTER_LINEAR); lat=cv2.resize(lat.astype(np.float32),(w,h),interpolation=cv2.INTER_LINEAR)
sx=((lon+180.0)%360.0)/360.0*SW-0.5; sy=np.clip((90.0-lat)/180.0*SH-0.5,0,SH-1)
ground=cv2.remap(height,sx.astype(np.float32),sy.astype(np.float32),cv2.INTER_CUBIC,borderMode=cv2.BORDER_WRAP)
land=np.load('province_ids.npy')>0
# which way is north in the rows of the map, and how far a pixel is on the ground
north=-1.0 if np.nanmean(lat[:h//4])>np.nanmean(lat[-h//4:]) else 1.0
metres=40075000.0/w*np.cos(np.radians(np.clip(lat,-75,75)))        # per pixel, east-west

def shading(e,exaggeration):
    """How a surface takes a light from the north-west, level ground being nought."""
    ex=(np.roll(e,-1,1)-np.roll(e,1,1))*0.5/metres*exaggeration
    ey=np.gradient(e,axis=0)/metres*exaggeration*north                 # rise towards the north
    light=np.array([-0.56,0.56,0.61]); light/=np.linalg.norm(light)   # from the west, the north, and above
    n=np.sqrt(ex*ex+ey*ey+1.0)
    return (-ex*light[0]-ey*light[1]+light[2])/n-light[2]
# Two readings of the same ground: its ridges and valleys as they are, and its
# great masses (a range, a plateau edge) from the ground smoothed out. Level
# country comes out as nought in both, so plains and deserts stay calm.
fine=shading(cv2.GaussianBlur(ground,(0,0),0.7),9.0)
mass=shading(cv2.GaussianBlur(ground,(0,0),4.0),22.0)
d=0.62*fine+0.38*mass
lit=np.where(d<0,d/abs(np.percentile(d[land],0.4)),d/np.percentile(d[land],99.6))
lit=np.sign(lit)*np.maximum(np.abs(lit)-0.035,0)/0.965    # what is all but level is level
lit=np.clip(lit,-1,1)*land
alpha=(0.5+0.5*lit)*(ALPHA_MAX/255.0)
level=0.0

raw=open(V2+'map/terrain/colormap.dds','rb').read()
bh,bw=struct.unpack('<II',raw[12:20])
base=np.frombuffer(raw[128:128+bw*bh*4],np.uint8).reshape(bh,bw,4)[...,:3].astype(np.float32)/255.0
lvl=np.dstack([cv2.resize(base,(w,h),interpolation=cv2.INTER_CUBIC),alpha]).astype(np.float32)   # BGRA

Image.fromarray((np.clip(alpha if north<0 else alpha[::-1],0,1)*255).astype(np.uint8),'L').save('relief_check.png')   # north up, for the eye
mips=[]
while True:
    mips.append((np.clip(lvl,0,1)*255+0.5).astype(np.uint8).tobytes())
    hh,ww=lvl.shape[:2]
    if ww==1 and hh==1: break
    lvl=cv2.resize(lvl,(max(1,ww//2),max(1,hh//2)),interpolation=cv2.INTER_AREA).reshape(max(1,hh//2),max(1,ww//2),4)
header=struct.pack('<4s7I44x',b'DDS ',124,0x1|0x2|0x4|0x8|0x1000|0x20000,h,w,w*4,0,len(mips))
header+=struct.pack('<2I4s5I',32,0x41,b'\0\0\0\0',32,0xff0000,0xff00,0xff,0xff000000)
header+=struct.pack('<5I',0x1000|0x8|0x400000,0,0,0,0)
os.makedirs(OUT+'map/terrain',exist_ok=True)
with open(OUT+'map/terrain/colormap.dds','wb') as f:
    f.write(header)
    for m in mips: f.write(m)
print('relief',w,h,'level',float(level),'in shade',float((lit<-0.15).sum()/land.sum()))
