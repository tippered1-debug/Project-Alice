import json, re, numpy as np, cv2, math, sys
from PIL import Image
Image.MAX_IMAGE_PIXELS=None
V2='/Users/stefanbidzamov/Games/Victoria2/'
prov=np.asarray(Image.open(V2+'map/provinces.bmp').convert('RGB'))
H,W=prov.shape[:2]
key=(prov[...,0].astype(np.uint32)<<16)|(prov[...,1].astype(np.uint32)<<8)|prov[...,2]
defs={}
for line in open(V2+'map/definition.csv',encoding='latin-1'):
    p=line.strip().split(';')
    if len(p)>=4 and p[0].isdigit():
        defs[int(p[0])]=(int(float(p[1]))<<16)|(int(float(p[2]))<<8)|int(float(p[3]))
dm=open(V2+'map/default.map',encoding='latin-1').read()
sea=set(int(x) for x in re.search(r'sea_starts\s*=\s*\{([^}]*)\}',dm,re.S).group(1).split())
seacolors=np.array([defs[i] for i in sea if i in defs],dtype=np.uint32)
landcolors=np.array([c for i,c in defs.items() if i not in sea],dtype=np.uint32)
land=np.isin(key,landcolors)
np.save('v2_land.npy',land); np.save('v2_key.npy',key)
print('land frac',land.mean(), 'sea provs',len(sea),'defs',len(defs))
S=8
small=cv2.resize(land.astype(np.uint8),(W//S,H//S),interpolation=cv2.INTER_AREA)>0
ne=json.load(open('ne_110m_land.geojson'))
rings=[]
for f in ne['features']:
    g=f['geometry']; polys=[g['coordinates']] if g['type']=='Polygon' else g['coordinates']
    for poly in polys: rings.append(np.array(poly[0]))
def miller(lat): return 1.25*np.log(np.tan(np.pi/4+0.4*np.radians(lat)))
def raster(lon0,top,bot,f):
    w,h=W//S,H//S
    img=np.zeros((h,w*3),np.uint8)
    ft,fb=f(top),f(bot)
    for r in rings:
        x=((r[:,0]-lon0)/360.0)*w
        y=(ft-f(r[:,1]))/(ft-fb)*h
        for k in (0,1,2):
            pts=np.stack([x+k*w-w,y],1).astype(np.int32) if False else np.stack([x+ (k-1)*w + w ,y],1).astype(np.int32)
            cv2.fillPoly(img,[pts],1)
    return (img[:,0:w]|img[:,w:2*w]|img[:,2*w:3*w])>0
best=None
for name,f in (('equirect',lambda a:np.asarray(a,dtype=float)),('miller',miller)):
    for lon0 in np.arange(-180,-150,2.0):
        for top in np.arange(72,90,2.0):
            for bot in np.arange(-70,-50,2.0):
                m=raster(lon0,top,bot,f)
                iou=(m&small).sum()/((m|small).sum())
                if best is None or iou>best[0]: best=(iou,name,lon0,top,bot)
    print(name,best)
print(best)
